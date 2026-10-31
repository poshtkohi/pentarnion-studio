#define PENTA_EVOLUTION_MODULE_BUILD
#include "evolution_api.h"
#include <pagmo/algorithm.hpp>
#include <pagmo/algorithms/cmaes.hpp>
#include <pagmo/algorithms/nsga2.hpp>
#include <pagmo/algorithms/sade.hpp>
#include <pagmo/algorithms/sga.hpp>
#include <pagmo/bfe.hpp>
#include <pagmo/population.hpp>
#include <pagmo/problem.hpp>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <numeric>
#include <random>
#include <stdexcept>
#include <vector>
namespace {
using Vector=pagmo::vector_double;using Clock=std::chrono::steady_clock;
struct Failure:std::runtime_error{int status;Failure(int s,const std::string& text):std::runtime_error(text),status(s){}};
void message(char* out,uint64_t capacity,const char* text)noexcept{if(out&&capacity){const auto n=std::min<uint64_t>(std::strlen(text),capacity-1);std::memcpy(out,text,n);out[n]=0;}}
struct Context{
    const penta_evolution_host_v1& host;const penta_evolution_request_v1& q;Clock::time_point start=Clock::now();
    std::atomic_uint64_t evaluations{0};std::mutex mutex;Vector best,bestF;bool target=false,userStop=false,initialized=false;
    Context(const penta_evolution_host_v1& h,const penta_evolution_request_v1& request):host(h),q(request){}
    double elapsed()const{return std::chrono::duration<double>(Clock::now()-start).count();}
    void notify(uint32_t phase,uint64_t generation){
        if(host.struct_size<sizeof(penta_evolution_host_v1)||!host.output)return;
        penta_iteration_snapshot_v1 snapshot{sizeof(snapshot),PENTA_ITERATION_ABI_V1,phase,PENTA_ITERATION_EVENT_GENERATION,PENTA_ITERATION_HAS_OBJECTIVE|PENTA_ITERATION_ACCEPTED,0,generation,evaluations.load(),1,best.size(),best.data(),bestF[0],0,0,elapsed()};char error[2048]{};const int action=host.output(host.context,&snapshot,error,sizeof(error));if(action<0)throw Failure(PENTA_EVOLUTION_CALLBACK,error[0]?error:"OutputFcn failed.");if(action>0&&phase!=PENTA_ITERATION_DONE)userStop=true;initialized=true;
    }
    void interrupt()const{if(host.cancelled&&host.cancelled(host.context))throw Failure(PENTA_EVOLUTION_CANCELLED,"Evolution cancelled.");if(elapsed()>=q.maximum_wall_seconds)throw Failure(PENTA_EVOLUTION_BUDGET,"wall_time_limit");}
    Vector physical(const Vector& z)const{Vector x(z.size());for(std::size_t j=0;j<z.size();++j){const double u=std::clamp(z[j],0.,1.);if(q.log_coordinates&&q.log_coordinates[j])x[j]=std::exp(std::log(q.lower[j])+u*(std::log(q.upper[j])-std::log(q.lower[j])));else x[j]=q.lower[j]+u*(q.upper[j]-q.lower[j]);x[j]=std::clamp(x[j],q.lower[j],q.upper[j]);}return x;}
    Vector normalized(const double* x)const{Vector z(q.dimension);for(std::size_t j=0;j<z.size();++j)z[j]=q.log_coordinates&&q.log_coordinates[j]?(std::log(x[j])-std::log(q.lower[j]))/(std::log(q.upper[j])-std::log(q.lower[j])):(x[j]-q.lower[j])/(q.upper[j]-q.lower[j]);return z;}
    Vector evaluate(const Vector& z){
        interrupt();auto old=evaluations.load();while(old<q.max_evaluations&&!evaluations.compare_exchange_weak(old,old+1)){}if(old>=q.max_evaluations)throw Failure(PENTA_EVOLUTION_BUDGET,"evaluation_limit");
        char error[2048]{};if(host.charge&&host.charge(host.context,q.dimension+q.objectives,error,sizeof(error)))throw Failure(PENTA_EVOLUTION_BUDGET,error[0]?error:"Host work budget exhausted.");const auto x=physical(z);Vector f(q.objectives);const int status=q.evaluate(q.callback_context,x.data(),q.dimension,f.data(),q.objectives,error,sizeof(error));if(status)throw Failure(PENTA_EVOLUTION_CALLBACK,error[0]?error:"Native evolutionary objective failed.");for(const double value:f)if(!std::isfinite(value))throw Failure(PENTA_EVOLUTION_DOMAIN,"Evolutionary objective must return finite real values.");
        {std::lock_guard lock(mutex);if(bestF.empty()||std::lexicographical_compare(f.begin(),f.end(),bestF.begin(),bestF.end())||(f==bestF&&x<best)){best=x;bestF=f;}if(q.objectives==1&&q.target_enabled&&f[0]<=q.target_objective)target=true;}
        return f;
    }
};
struct Problem{
    Context* context=nullptr;
    Vector fitness(const Vector& x)const{return context->evaluate(x);}
    std::pair<Vector,Vector> get_bounds()const{return {Vector(context->q.dimension,0.),Vector(context->q.dimension,1.)};}
    pagmo::vector_double::size_type get_nobj()const{return context->q.objectives;}
    pagmo::thread_safety get_thread_safety()const{return pagmo::thread_safety::constant;}
};
struct Batch{
    Vector operator()(const pagmo::problem& problem,const Vector& flat)const{
        auto* context=problem.extract<Problem>()->context;const auto n=context->q.dimension,m=context->q.objectives,count=flat.size()/n;Vector output(count*m);
        struct Work{Context* context;const Vector* flat;Vector* output;uint64_t n,m;std::mutex mutex;std::exception_ptr failure;};Work work{context,&flat,&output,n,m,{}, {}};
        auto task=[](void* raw,uint64_t begin,uint64_t end,char* error,uint64_t capacity)->int{auto& w=*static_cast<Work*>(raw);try{for(uint64_t row=begin;row<end;++row){Vector x(w.flat->begin()+row*w.n,w.flat->begin()+(row+1)*w.n);const auto f=w.context->evaluate(x);std::copy(f.begin(),f.end(),w.output->begin()+row*w.m);}return 0;}catch(const std::exception& e){{std::lock_guard lock(w.mutex);if(!w.failure)w.failure=std::current_exception();}message(error,capacity,e.what());return PENTA_NUMERIC_INTERNAL;}catch(...){{std::lock_guard lock(w.mutex);if(!w.failure)w.failure=std::current_exception();}message(error,capacity,"Native evolutionary batch failed.");return PENTA_NUMERIC_INTERNAL;}};
        char error[2048]{};const int status=context->host.parallel_for(context->host.context,count,256,task,&work,error,sizeof(error));if(work.failure)std::rethrow_exception(work.failure);if(status)throw Failure(status==PENTA_NUMERIC_CANCELLED?PENTA_EVOLUTION_CANCELLED:PENTA_EVOLUTION_INTERNAL,error[0]?error:"Evolutionary batch dispatch failed.");return output;
    }
    pagmo::thread_safety get_thread_safety()const{return pagmo::thread_safety::constant;}
};
bool dominates(const Vector& a,const Vector& b){bool strict=false;for(std::size_t j=0;j<a.size();++j){if(a[j]>b[j])return false;strict|=a[j]<b[j];}return strict;}
int solve(const penta_evolution_host_v1* host,const penta_evolution_request_v1* request,penta_evolution_result_v1* result,char* error,uint64_t capacity)noexcept{
    try{
        if(!host||host->struct_size<offsetof(penta_evolution_host_v1,output)||host->abi_version!=PENTA_EVOLUTION_ABI_V1||!host->parallel_for||!request||request->struct_size<sizeof(*request)||!result||result->struct_size<sizeof(*result))throw Failure(PENTA_EVOLUTION_VERSION,"Invalid evolutionary API structures.");
        const auto& q=*request;auto& r=*result;
        if(q.algorithm<1||q.algorithm>5||q.dimension<1||q.dimension>128||q.objectives<1||q.objectives>16||!q.initial||!q.lower||!q.upper||!q.evaluate||q.population<8||q.population>4096||q.population%4||q.restarts>4||q.max_generations<1||q.max_generations>1000000||q.max_evaluations<q.population||q.max_evaluations>10000000||q.history_capacity>100000||!std::isfinite(q.maximum_wall_seconds)||q.maximum_wall_seconds<=0||!std::isfinite(q.ftol)||q.ftol<0||!std::isfinite(q.xtol)||q.xtol<0||(q.target_enabled&&!std::isfinite(q.target_objective)))throw Failure(PENTA_EVOLUTION_ARGUMENT,"Invalid evolutionary dimensions, budgets or settings; population must be a multiple of four in8..4096.");
        if((q.algorithm==5&&(q.objectives<2||q.target_enabled))||(q.algorithm!=5&&q.objectives!=1)||(q.algorithm!=3&&q.restarts))throw Failure(PENTA_EVOLUTION_ARGUMENT,"NSGA-II requires multiple objectives and has no scalar target; other methods require one. Restarts apply to CMA-ES only.");
        for(uint64_t j=0;j<q.dimension;++j)if(!std::isfinite(q.initial[j])||!std::isfinite(q.lower[j])||!std::isfinite(q.upper[j])||!(q.lower[j]<q.upper[j])||!std::isfinite(q.upper[j]-q.lower[j])||q.initial[j]<q.lower[j]||q.initial[j]>q.upper[j]||(q.log_coordinates&&q.log_coordinates[j]&&q.lower[j]<=0))throw Failure(PENTA_EVOLUTION_ARGUMENT,"Evolution requires finite strict bounds, an initial point inside them and positive log-coordinate bounds.");
        const auto maximumPopulation=q.algorithm==3?std::min<uint64_t>(4096,uint64_t(q.population)<<q.restarts):q.population;
        if(!r.solution||r.solution_capacity<q.dimension||(q.history_capacity&&(!r.history||r.history_capacity<q.history_capacity*4))||!r.population_x||r.population_x_capacity<maximumPopulation*q.dimension||!r.population_f||r.population_f_capacity<maximumPopulation*q.objectives||!r.pareto_x||r.pareto_x_capacity<maximumPopulation*q.dimension||!r.pareto_f||r.pareto_f_capacity<maximumPopulation*q.objectives)throw Failure(PENTA_EVOLUTION_ARGUMENT,"Evolution output buffers are insufficient.");
        r.evaluations=r.generations=r.history_rows=r.population_rows=r.pareto_rows=0;r.converged=r.restarts_completed=0;
        Context context(*host,q);std::mt19937_64 random(q.seed);pagmo::problem problem{Problem{&context}};pagmo::population population(problem,0,q.seed);uint32_t size=q.population,restart=0;uint64_t generations=0,stagnation=0;double previous=std::numeric_limits<double>::infinity();std::string status="generation_limit";bool converged=false;
        auto initialize=[&]{population=pagmo::population(problem,0,q.seed+restart);Vector flat(size*q.dimension);const auto first=context.best.empty()?context.normalized(q.initial):context.normalized(context.best.data());std::copy(first.begin(),first.end(),flat.begin());for(uint64_t row=1;row<size;++row)for(uint64_t j=0;j<q.dimension;++j)flat[row*q.dimension+j]=static_cast<double>(random()>>11)*0x1.0p-53;const auto values=Batch{}(problem,flat);for(uint64_t row=0;row<size;++row)population.push_back(Vector(flat.begin()+row*q.dimension,flat.begin()+(row+1)*q.dimension),Vector(values.begin()+row*q.objectives,values.begin()+(row+1)*q.objectives));};
        auto algorithm=[&]()->pagmo::algorithm{const auto seed=q.seed+restart*7919u;switch(q.algorithm){case 1:return pagmo::algorithm{pagmo::sade(1,2,1,0,0,true,seed)};case 2:return pagmo::algorithm{pagmo::sade(1,2,2,0,0,true,seed)};case 3:{pagmo::cmaes method(1,-1,-1,-1,-1,.3,0,0,true,true,seed);method.set_bfe(pagmo::bfe{Batch{}});return pagmo::algorithm{method};}case 4:return pagmo::algorithm{pagmo::sga(1,.9,15,1./q.dimension,20,2,"sbx","polynomial","tournament",seed)};default:{pagmo::nsga2 method(1,.9,15,1./q.dimension,20,seed);method.set_bfe(pagmo::bfe{Batch{}});return pagmo::algorithm{method};}}};
        pagmo::algorithm method;auto history=[&](double spread){if(r.history_rows<q.history_capacity){auto* row=r.history+r.history_rows*4;row[0]=static_cast<double>(generations);row[1]=static_cast<double>(context.evaluations.load());row[2]=context.bestF.empty()?0:context.bestF[0];row[3]=spread;++r.history_rows;}};
        try{
            initialize();method=algorithm();history(1);context.notify(PENTA_ITERATION_INIT,0);
            for(;generations<q.max_generations;){if(context.userStop){status="output_function_stop";break;}context.interrupt();if(context.target){status="target_satisfied";converged=true;break;}if(context.evaluations.load()+size>q.max_evaluations){status="evaluation_limit";break;}population=method.evolve(population);++generations;
                double spread=0,fspread=0;for(uint64_t j=0;j<q.dimension;++j){double lo=1,hi=0;for(const auto& x:population.get_x()){lo=std::min(lo,x[j]);hi=std::max(hi,x[j]);}spread=std::max(spread,hi-lo);}if(q.objectives==1){double lo=std::numeric_limits<double>::infinity(),hi=-lo;for(const auto& f:population.get_f()){lo=std::min(lo,f[0]);hi=std::max(hi,f[0]);}fspread=hi-lo;if(context.bestF[0]<previous-q.ftol*(1+std::abs(context.bestF[0]))){previous=context.bestF[0];stagnation=0;}else ++stagnation;}history(spread);
                context.notify(PENTA_ITERATION_ITER,generations);if(context.userStop){status="output_function_stop";break;}
                if(context.target){status="target_satisfied";converged=true;break;}
                const bool collapsed=q.objectives==1&&generations>=10&&q.ftol>0&&q.xtol>0&&fspread<=q.ftol*(1+std::abs(context.bestF[0]))&&spread<=q.xtol;
                if(q.algorithm==3&&(collapsed||stagnation>=30)&&restart<q.restarts&&size<4096){const auto next=std::min<uint32_t>(4096,size*2);if(context.evaluations.load()+next<=q.max_evaluations){size=next;++restart;initialize();method=algorithm();stagnation=0;continue;}}
                if(collapsed){status="population_tolerance";converged=true;break;}
            }
        }catch(const Failure& failure){if(failure.status!=PENTA_EVOLUTION_BUDGET||context.best.empty())throw;status=failure.what();}
        if(context.best.empty())throw Failure(PENTA_EVOLUTION_INTERNAL,"Evolution produced no finite candidate.");
        if(!context.initialized)context.notify(PENTA_ITERATION_INIT,generations);
        if(context.userStop){status="output_function_stop";converged=false;}
        std::copy(context.best.begin(),context.best.end(),r.solution);r.objective_value=context.bestF[0];r.evaluations=context.evaluations;r.generations=generations;r.restarts_completed=restart;r.converged=converged;r.elapsed_seconds=context.elapsed();message(r.status,sizeof(r.status),status.c_str());message(r.message,sizeof(r.message),q.objectives>1?"x/objective is a lexicographic representative; use pareto_x and pareto_f for tradeoffs. A finite search does not certify global optimality.":"Best evaluated candidate preserved independently of population. A finite search does not certify global optimality.");
        r.population_rows=population.size();std::vector<uint64_t> front;for(uint64_t row=0;row<population.size();++row){const auto x=context.physical(population.get_x()[row]);std::copy(x.begin(),x.end(),r.population_x+row*q.dimension);std::copy(population.get_f()[row].begin(),population.get_f()[row].end(),r.population_f+row*q.objectives);bool dominated=false;for(uint64_t other=0;other<population.size()&&!dominated;++other)if(other!=row)dominated=dominates(population.get_f()[other],population.get_f()[row]);if(!dominated)front.push_back(row);}
        std::sort(front.begin(),front.end(),[&](uint64_t a,uint64_t b){return population.get_f()[a]!=population.get_f()[b]?population.get_f()[a]<population.get_f()[b]:population.get_x()[a]<population.get_x()[b];});for(uint64_t row=0;row<front.size();++row){const auto source=front[row];std::copy_n(r.population_x+source*q.dimension,q.dimension,r.pareto_x+row*q.dimension);std::copy_n(r.population_f+source*q.objectives,q.objectives,r.pareto_f+row*q.objectives);}r.pareto_rows=front.size();context.notify(PENTA_ITERATION_DONE,generations);return PENTA_EVOLUTION_OK;
    }catch(const Failure& failure){message(error,capacity,failure.what());return failure.status;}catch(const std::exception& failure){message(error,capacity,failure.what());return PENTA_EVOLUTION_INTERNAL;}catch(...){message(error,capacity,"Unknown evolutionary backend failure.");return PENTA_EVOLUTION_INTERNAL;}
}
const penta_evolution_api_v1 api{sizeof(api),PENTA_EVOLUTION_ABI_V1,"penta_evolution","pagmo2-2.19.1-eigen5-config-patch;bounded-v1-output",31|PENTA_ITERATION_CAPABILITY_V1,solve};
}
extern "C" PENTA_EVOLUTION_EXPORT const penta_evolution_api_v1* penta_evolution_query_v1(uint32_t abi){return abi==PENTA_EVOLUTION_ABI_V1?&api:nullptr;}
