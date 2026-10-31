#define PENTA_OPT_MODULE_BUILD
#include "opt_api.h"
#include <nlopt.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>
namespace {
using Clock=std::chrono::steady_clock;
void text(char* out,uint64_t capacity,const std::string& value){if(out&&capacity){const auto n=std::min<uint64_t>(capacity-1,value.size());std::memcpy(out,value.data(),static_cast<std::size_t>(n));out[n]=0;}}
struct Error:std::runtime_error{int status;Error(int code,const std::string& value):std::runtime_error(value),status(code){}};
std::mutex& rngMutex(){static std::mutex value;return value;}
struct Cache {std::vector<double> x,values,jacobian;bool valid=false,derivatives=false;};
struct Context {
    const penta_opt_host_v1& host;const penta_opt_request_v1& request;penta_opt_result_v1& report;nlopt_opt solver=nullptr;
    Clock::time_point start=Clock::now();int failure=0;std::string error;std::vector<double> scale,x,bestX;Cache objective,inequality,equality;
    double bestObjective=std::numeric_limits<double>::infinity(),bestViolation=std::numeric_limits<double>::infinity();bool userStop=false,initialized=false;
    Context(const penta_opt_host_v1& h,const penta_opt_request_v1& q,penta_opt_result_v1& r):host(h),request(q),report(r){}
    double elapsed()const{return std::chrono::duration<double>(Clock::now()-start).count();}
    bool stop(int code,const std::string& why){failure=code;error=why;if(solver)nlopt_force_stop(solver);return false;}
    bool check(){if(failure||userStop)return false;if(host.cancelled&&host.cancelled(host.context))return stop(PENTA_OPT_CANCELLED,"Native optimization cancelled.");if(elapsed()>=request.maximum_wall_seconds)return stop(PENTA_OPT_BUDGET,"Optimization wall-time budget reached.");return true;}
    bool notify(uint32_t phase,const std::vector<double>& point,double value){if(host.struct_size<sizeof(penta_opt_host_v1)||!host.output)return true;penta_iteration_snapshot_v1 snapshot{sizeof(snapshot),PENTA_ITERATION_ABI_V1,phase,PENTA_ITERATION_EVENT_EVALUATION,PENTA_ITERATION_HAS_OBJECTIVE,0,0,report.evaluations,1,point.size(),point.data(),value,0,0,elapsed()};char message[2048]{};const int action=host.output(host.context,&snapshot,message,sizeof(message));if(action<0)return stop(PENTA_OPT_CALLBACK,message[0]?message:"OutputFcn failed.");if(action>0&&phase!=PENTA_ITERATION_DONE){userStop=true;if(solver)nlopt_force_stop(solver);}return true;}
    bool evaluate(Cache& cache,penta_opt_evaluate_v1 callback,void* opaque,uint64_t m,const double* z,bool derivative,bool objectiveCall){
        if(!check())return false;
        const auto n=static_cast<std::size_t>(request.dimension);
        for(std::size_t c=0;c<n;++c){x[c]=z[c]*scale[c];if(!std::isfinite(x[c]))return stop(PENTA_OPT_DOMAIN,"Scaled optimization parameters overflowed.");}
        if(cache.valid&&cache.x==x&&(!derivative||cache.derivatives))return true;
        if(report.evaluations>=request.max_evaluations)return stop(PENTA_OPT_BUDGET,"Optimization native callback evaluation budget reached.");
        cache.x=x;cache.values.resize(static_cast<std::size_t>(m));cache.jacobian.resize(static_cast<std::size_t>(m)*n);char message[2048]{};
        if(host.charge&&host.charge(host.context,m*(derivative?n:1),message,sizeof(message)))return stop(PENTA_OPT_BUDGET,message);
        ++report.evaluations;if(objectiveCall)++report.objective_evaluations;else ++report.constraint_evaluations;if(derivative)++report.gradient_evaluations;
        const int code=callback(opaque,x.data(),n,cache.values.data(),m,derivative?cache.jacobian.data():nullptr,message,sizeof(message));
        if(code)return stop(PENTA_OPT_CALLBACK,message[0]?message:"Native optimization callback failed.");
        if(std::any_of(cache.values.begin(),cache.values.end(),[](double v){return !std::isfinite(v);})||(derivative&&std::any_of(cache.jacobian.begin(),cache.jacobian.end(),[](double v){return !std::isfinite(v);})))return stop(PENTA_OPT_DOMAIN,"Native objective, constraint or derivative is nonfinite.");
        cache.valid=true;cache.derivatives=derivative;return true;
    }
    bool constraints(const double* z){
        if(request.inequality_count&&!evaluate(inequality,request.inequality,request.inequality_context,request.inequality_count,z,false,false))return false;
        if(request.equality_count&&!evaluate(equality,request.equality,request.equality_context,request.equality_count,z,false,false))return false;
        return true;
    }
    double violation()const{double v=0;for(double value:inequality.values)v=std::max(v,value);for(double value:equality.values)v=std::max(v,std::abs(value));return v;}
    static double objectiveFunction(unsigned n,const double* z,double* gradient,void* opaque)noexcept{
        auto& context=*static_cast<Context*>(opaque);try{
            if(gradient)std::fill_n(gradient,n,0);
            const auto evaluations=context.report.objective_evaluations;
            if(!context.evaluate(context.objective,context.request.objective,context.request.objective_context,1,z,gradient!=nullptr,true)||!context.constraints(z))return std::numeric_limits<double>::max();
            const double value=context.objective.values[0],violation=context.violation();
            if(gradient)for(unsigned c=0;c<n;++c)gradient[c]=context.objective.jacobian[c]*context.scale[c];
            const bool feasible=violation<=context.request.constraint_tolerance,bestFeasible=context.bestViolation<=context.request.constraint_tolerance;
            if(context.bestX.empty()||(feasible&&!bestFeasible)||(feasible==bestFeasible&&(feasible?value<context.bestObjective:violation<context.bestViolation||(violation==context.bestViolation&&value<context.bestObjective)))){context.bestX=context.objective.x;context.bestObjective=value;context.bestViolation=violation;}
            if(context.report.objective_evaluations!=evaluations&&context.report.history_rows<context.request.history_capacity){auto* row=context.report.history+3*context.report.history_rows++;row[0]=static_cast<double>(context.report.evaluations);row[1]=value;row[2]=violation;}
            if(context.report.objective_evaluations!=evaluations){const auto phase=context.initialized?PENTA_ITERATION_ITER:PENTA_ITERATION_INIT;context.initialized=true;context.notify(phase,context.objective.x,value);}
            return value;
        }catch(const std::exception& error){context.stop(PENTA_OPT_INTERNAL,error.what());return std::numeric_limits<double>::max();}catch(...){context.stop(PENTA_OPT_INTERNAL,"Unknown native optimization callback failure.");return std::numeric_limits<double>::max();}
    }
    struct Constraint {Context* owner;bool equality;};
    static void constraintFunction(unsigned m,double* values,unsigned n,const double* z,double* gradient,void* opaque)noexcept{
        auto& c=*static_cast<Constraint*>(opaque);auto& context=*c.owner;try{
            if(gradient)std::fill_n(gradient,static_cast<std::size_t>(m)*n,0);
            auto& cache=c.equality?context.equality:context.inequality;auto callback=c.equality?context.request.equality:context.request.inequality;void* data=c.equality?context.request.equality_context:context.request.inequality_context;
            if(!context.evaluate(cache,callback,data,m,z,gradient!=nullptr,false)){std::fill_n(values,m,std::numeric_limits<double>::max());return;}
            std::copy(cache.values.begin(),cache.values.end(),values);if(gradient)for(unsigned row=0;row<m;++row)for(unsigned col=0;col<n;++col)gradient[row*n+col]=cache.jacobian[row*n+col]*context.scale[col];
        }catch(const std::exception& error){context.stop(PENTA_OPT_INTERNAL,error.what());std::fill_n(values,m,std::numeric_limits<double>::max());}catch(...){context.stop(PENTA_OPT_INTERNAL,"Unknown native constraint failure.");std::fill_n(values,m,std::numeric_limits<double>::max());}
    }
};
struct Destroy {void operator()(nlopt_opt_s* value)const{nlopt_destroy(value);}};
int solve(const penta_opt_host_v1* host,const penta_opt_request_v1* request,penta_opt_result_v1* output,char* error,uint64_t capacity)noexcept{
    try{
        if(!host||!request||!output||host->struct_size<offsetof(penta_opt_host_v1,output)||host->abi_version!=PENTA_OPT_ABI_V1||request->struct_size<sizeof(*request)||output->struct_size<sizeof(*output))throw Error(PENTA_OPT_VERSION,"Optimization ABI/structure mismatch.");
        const auto& q=*request;auto& r=*output;const auto n=static_cast<std::size_t>(q.dimension);
        if(!q.objective||!q.initial||n<1||n>128||q.algorithm<1||q.algorithm>8||q.max_evaluations<1||q.max_evaluations>10000000||q.history_capacity>100000||q.inequality_count>128||q.equality_count>128||(q.inequality_count&&!q.inequality)||(q.equality_count&&!q.equality))throw Error(PENTA_OPT_ARGUMENT,"Invalid optimization callbacks, dimensions, algorithm or budgets.");
        if(!r.solution||r.solution_capacity<n||(q.history_capacity&&(!r.history||r.history_capacity<3*q.history_capacity)))throw Error(PENTA_OPT_ARGUMENT,"Insufficient host optimization result storage.");
        for(double value:{q.ftol_relative,q.ftol_absolute,q.xtol_relative,q.xtol_absolute,q.constraint_tolerance})if(!std::isfinite(value)||value<0)throw Error(PENTA_OPT_ARGUMENT,"Optimization tolerances require finite nonnegative values.");
        if(!std::isfinite(q.maximum_wall_seconds)||q.maximum_wall_seconds<=0||!std::isfinite(q.initial_step)||q.initial_step<0||q.population>1000000)throw Error(PENTA_OPT_ARGUMENT,"Invalid optimization wall budget, initial step or population.");
        const bool constrained=q.inequality_count||q.equality_count;if(constrained&&q.algorithm!=4&&q.algorithm!=5&&q.algorithm!=8)throw Error(PENTA_OPT_ARGUMENT,"Nonlinear constraints require slsqp, cobyla or isres.");
        if(q.algorithm==2&&n<2)throw Error(PENTA_OPT_ARGUMENT,"BOBYQA requires at least two real coordinates; use nelder_mead or lbfgs for a scalar problem.");
        if(q.population&&q.algorithm!=7&&q.algorithm!=8)throw Error(PENTA_OPT_ARGUMENT,"population applies only to crs2 or isres.");
        if(q.initial_step>0&&q.algorithm!=2&&q.algorithm!=3&&q.algorithm!=5)throw Error(PENTA_OPT_ARGUMENT,"initial_step applies only to bobyqa, nelder_mead or cobyla.");
        const nlopt_algorithm algorithms[]{NLOPT_LD_LBFGS,NLOPT_LN_BOBYQA,NLOPT_LN_NELDERMEAD,NLOPT_LD_SLSQP,NLOPT_LN_COBYLA,NLOPT_GN_DIRECT_L,NLOPT_GN_CRS2_LM,NLOPT_GN_ISRES};
        std::unique_lock randomLock(rngMutex());nlopt_srand(q.seed);std::unique_ptr<nlopt_opt_s,Destroy> optimizer(nlopt_create(algorithms[q.algorithm-1],static_cast<unsigned>(n)));if(!optimizer)throw Error(PENTA_OPT_INTERNAL,"Cannot allocate NLopt optimizer.");
        Context context(*host,q,r);context.solver=optimizer.get();context.scale.assign(n,1);context.x.resize(n);if(q.parameter_scale)context.scale.assign(q.parameter_scale,q.parameter_scale+n);std::vector<double> z(n),lower(n,-std::numeric_limits<double>::infinity()),upper(n,std::numeric_limits<double>::infinity());
        for(std::size_t c=0;c<n;++c){const double scale=context.scale[c],lo=q.lower?q.lower[c]:lower[c],hi=q.upper?q.upper[c]:upper[c];if(!std::isfinite(scale)||scale<=0||std::isnan(lo)||std::isnan(hi)||lo>=hi||!std::isfinite(q.initial[c])||q.initial[c]<lo||q.initial[c]>hi)throw Error(PENTA_OPT_ARGUMENT,"Invalid parameter scale, bounds or initial optimization point.");if(q.algorithm>=6&&(!std::isfinite(lo)||!std::isfinite(hi)))throw Error(PENTA_OPT_ARGUMENT,"Global optimization requires finite lower and upper bounds on every coordinate.");z[c]=q.initial[c]/scale;lower[c]=lo/scale;upper[c]=hi/scale;if(!std::isfinite(z[c])||(std::isfinite(lo)&&!std::isfinite(lower[c]))||(std::isfinite(hi)&&!std::isfinite(upper[c])))throw Error(PENTA_OPT_ARGUMENT,"Optimization scaling overflows the finite real range.");}
        auto require=[&](nlopt_result code,const char* operation){if(code<0)throw Error(PENTA_OPT_ARGUMENT,std::string(operation)+": "+(nlopt_get_errmsg(optimizer.get())?nlopt_get_errmsg(optimizer.get()):"NLopt rejected an option"));};
        require(nlopt_set_lower_bounds(optimizer.get(),lower.data()),"lower bounds");require(nlopt_set_upper_bounds(optimizer.get(),upper.data()),"upper bounds");require(nlopt_set_min_objective(optimizer.get(),Context::objectiveFunction,&context),"objective");require(nlopt_set_ftol_rel(optimizer.get(),q.ftol_relative),"ftol_relative");require(nlopt_set_ftol_abs(optimizer.get(),q.ftol_absolute),"ftol_absolute");require(nlopt_set_xtol_rel(optimizer.get(),q.xtol_relative),"xtol_relative");require(nlopt_set_xtol_abs1(optimizer.get(),q.xtol_absolute),"xtol_absolute");require(nlopt_set_maxeval(optimizer.get(),static_cast<int>(q.max_evaluations)),"max_evaluations");require(nlopt_set_maxtime(optimizer.get(),q.maximum_wall_seconds),"maximum_wall_seconds");
        if(q.initial_step>0)require(nlopt_set_initial_step1(optimizer.get(),q.initial_step),"initial_step");
        if(q.population)require(nlopt_set_population(optimizer.get(),q.population),"population");
        Context::Constraint inequality{&context,false},equality{&context,true};std::vector<double> inequalityTolerances(static_cast<std::size_t>(q.inequality_count),q.constraint_tolerance),equalityTolerances(static_cast<std::size_t>(q.equality_count),q.constraint_tolerance);
        if(q.inequality_count)require(nlopt_add_inequality_mconstraint(optimizer.get(),static_cast<unsigned>(q.inequality_count),Context::constraintFunction,&inequality,inequalityTolerances.data()),"inequality constraints");
        if(q.equality_count)require(nlopt_add_equality_mconstraint(optimizer.get(),static_cast<unsigned>(q.equality_count),Context::constraintFunction,&equality,equalityTolerances.data()),"equality constraints");
        r.evaluations=0;r.objective_evaluations=0;r.constraint_evaluations=0;r.gradient_evaluations=0;r.history_rows=0;r.converged=0;r.feasible=0;double objective=0;const auto flag=nlopt_optimize(optimizer.get(),z.data(),&objective);r.backend_flag=flag;
        if(context.failure&&context.failure!=PENTA_OPT_BUDGET)throw Error(context.failure,context.error);
        if(context.bestX.empty())throw Error(context.failure?context.failure:PENTA_OPT_INTERNAL,context.error.empty()?"Optimization produced no finite candidate.":context.error);
        r.objective_value=context.bestObjective;r.constraint_violation=context.bestViolation;r.feasible=context.bestViolation<=q.constraint_tolerance;r.converged=!context.failure&&!context.userStop&&flag>0&&flag<NLOPT_MAXEVAL_REACHED&&r.feasible;r.elapsed_seconds=context.elapsed();std::copy(context.bestX.begin(),context.bestX.end(),r.solution);
        std::string status=context.userStop?"output_function_stop":context.failure?"budget_reached":flag==NLOPT_MAXEVAL_REACHED?"max_evaluations":flag==NLOPT_MAXTIME_REACHED?"max_wall_seconds":flag==NLOPT_ROUNDOFF_LIMITED?"roundoff_limited":r.converged?"converged":!r.feasible?"infeasible":"not_converged";
        text(r.status,sizeof(r.status),status);std::string message=context.userStop?"Stopped cleanly by OutputFcn.":context.failure?context.error:std::string(nlopt_result_to_string(flag)?nlopt_result_to_string(flag):"NLopt termination");if(!r.feasible)message+="; independently evaluated constraints exceed constraint_tolerance";text(r.message,sizeof(r.message),message);if(!context.notify(PENTA_ITERATION_DONE,context.bestX,r.objective_value))throw Error(context.failure,context.error);return PENTA_OPT_OK;
    }catch(const Error& value){text(error,capacity,value.what());return value.status;}catch(const std::exception& value){text(error,capacity,value.what());return PENTA_OPT_INTERNAL;}catch(...){text(error,capacity,"Unknown native optimization failure.");return PENTA_OPT_INTERNAL;}
}
const penta_opt_api_v1 api{sizeof(api),PENTA_OPT_ABI_V1,"penta_opt","nlopt-native-opt-v1-output",1|PENTA_ITERATION_CAPABILITY_V1,solve};
}
extern "C" PENTA_OPT_EXPORT const penta_opt_api_v1* penta_opt_query_v1(uint32_t version){return version==PENTA_OPT_ABI_V1?&api:nullptr;}
