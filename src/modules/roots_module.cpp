#define PENTA_ROOTS_MODULE_BUILD
#include "roots_api.h"
#include <boost/math/tools/toms748_solve.hpp>
#include <kinsol/kinsol.h>
#include <kinsol/kinsol_ls.h>
#include <nvector/nvector_serial.h>
#include <sunmatrix/sunmatrix_dense.h>
#include <sunlinsol/sunlinsol_dense.h>
#include <sundials/sundials_context.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>
namespace {
using Clock=std::chrono::steady_clock;
void message(char* out,uint64_t capacity,const std::string& text){if(out&&capacity){const auto n=std::min<uint64_t>(text.size(),capacity-1);std::memcpy(out,text.data(),static_cast<std::size_t>(n));out[n]=0;}}
struct Failure:std::runtime_error{int code;Failure(int status,const std::string& text):std::runtime_error(text),code(status){}};
constexpr int observerStop=100;
void require(bool okay,const char* text){if(!okay)throw Failure(PENTA_ROOTS_ARGUMENT,text);}
struct State {
    const penta_roots_host_v1& host;const penta_roots_request_v1& request;
    penta_roots_result_v1& result;Clock::time_point started=Clock::now();
    std::vector<double> temporary,jacobian,lastX,lastResidual;
    int failure=0;std::string error;bool userStop=false,initialized=false;
    State(const penta_roots_host_v1& h,const penta_roots_request_v1& r,penta_roots_result_v1& out):host(h),request(r),result(out){}
    void notify(uint32_t phase,const std::vector<double>& x,const std::vector<double>& f){if(host.struct_size<sizeof(penta_roots_host_v1)||!host.output)return;double residualNorm=0;for(const double value:f)residualNorm=std::max(residualNorm,std::abs(value));penta_iteration_snapshot_v1 snapshot{sizeof(snapshot),PENTA_ITERATION_ABI_V1,phase,PENTA_ITERATION_EVENT_EVALUATION,PENTA_ITERATION_HAS_RESIDUAL_NORM,0,0,result.evaluations,1,x.size(),x.data(),0,residualNorm,0,std::chrono::duration<double>(Clock::now()-started).count()};char message[2048]{};const int action=host.output(host.context,&snapshot,message,sizeof(message));initialized=true;if(action<0)throw Failure(PENTA_ROOTS_CALLBACK,message[0]?message:"OutputFcn failed.");if(action>0&&phase!=PENTA_ITERATION_DONE){userStop=true;throw Failure(observerStop,"Stopped cleanly by OutputFcn.");}}
    void stop(uint64_t work=1){
        if(host.cancelled&&host.cancelled(host.context))throw Failure(PENTA_ROOTS_CANCELLED,"Root solve cancelled.");
        if(std::chrono::duration<double>(Clock::now()-started).count()>request.maximum_wall_seconds)throw Failure(PENTA_ROOTS_BUDGET,"Root solve exceeded its wall-time budget.");
        if(host.charge){char text[1024]{};if(host.charge(host.context,work,text,sizeof(text)))throw Failure(PENTA_ROOTS_BUDGET,text[0]?text:"Root operation budget exhausted.");}
    }
    void evaluate(const double* x,double* f,double* j){
        stop(j?request.dimension*request.dimension*request.dimension:request.dimension);
        if(result.evaluations>=request.max_evaluations)throw Failure(PENTA_ROOTS_BUDGET,"Root solve exhausted max_evaluations.");
        ++result.evaluations;if(j)++result.jacobian_evaluations;
        char text[1024]{};const int status=request.evaluate(request.callback_context,x,request.dimension,f,j,text,sizeof(text));
        if(status)throw Failure(status==PENTA_ROOTS_CANCELLED?status:PENTA_ROOTS_CALLBACK,text[0]?text:"Native root callback failed.");
        for(uint64_t n=0;n<request.dimension;++n){
            if(!std::isfinite(f[n]))throw Failure(PENTA_ROOTS_DOMAIN,"Native root callback returned a nonfinite residual.");
            if(request.operation==PENTA_ROOTS_FIXEDPOINT){f[n]-=x[n];if(!std::isfinite(f[n]))throw Failure(PENTA_ROOTS_DOMAIN,"Fixed-point residual G(x)-x is nonfinite.");}
        }
        if(j){for(uint64_t n=0;n<request.dimension*request.dimension;++n)if(!std::isfinite(j[n]))throw Failure(PENTA_ROOTS_DOMAIN,"Native root Jacobian is nonfinite.");
            if(request.operation==PENTA_ROOTS_FIXEDPOINT)for(uint64_t n=0;n<request.dimension;++n)j[n*request.dimension+n]-=1;}
        lastX.assign(x,x+request.dimension);lastResidual.assign(f,f+request.dimension);stop();notify(initialized?PENTA_ITERATION_ITER:PENTA_ITERATION_INIT,lastX,lastResidual);
    }
    void verify(const std::vector<double>& x,std::vector<double>& f){stop();if(lastX==x)f=lastResidual;else evaluate(x.data(),f.data(),nullptr);}
    void record(int code,const char* text)noexcept{failure=code;try{error=text;}catch(...){}}
};
void norms(State& state,const std::vector<double>& f){
    state.result.residual_norm=0;state.result.scaled_residual_norm=0;
    for(std::size_t n=0;n<f.size();++n){state.result.residual_norm=std::max(state.result.residual_norm,std::abs(f[n]));const double scale=state.request.residual_scale?state.request.residual_scale[n]:1;
        const double scaled=std::abs(f[n])*scale;if(!std::isfinite(scaled))throw Failure(PENTA_ROOTS_DOMAIN,"Scaled root residual overflowed.");state.result.scaled_residual_norm=std::max(state.result.scaled_residual_norm,scaled);}
}
struct Resources {
    SUNContext context=nullptr;N_Vector x=nullptr,uscale=nullptr,fscale=nullptr;SUNMatrix matrix=nullptr;SUNLinearSolver linear=nullptr;void* solver=nullptr;
    ~Resources(){if(solver)KINFree(&solver);if(linear)SUNLinSolFree(linear);if(matrix)SUNMatDestroy(matrix);if(x)N_VDestroy(x);if(uscale)N_VDestroy(uscale);if(fscale)N_VDestroy(fscale);if(context)SUNContext_Free(&context);}
};
void sundialsError(int,const char*,const char*,const char* text,SUNErrCode,void* raw,SUNContext){auto& state=*static_cast<State*>(raw);if(state.error.empty())try{state.error=text?text:"KINSOL reported an error.";}catch(...){}}
int residual(N_Vector x,N_Vector f,void* raw)noexcept{
    auto& state=*static_cast<State*>(raw);try{state.evaluate(N_VGetArrayPointer(x),N_VGetArrayPointer(f),nullptr);return 0;}
    catch(const Failure& error){state.record(error.code,error.what());return -1;}catch(const std::exception& error){state.record(PENTA_ROOTS_INTERNAL,error.what());return -1;}catch(...){state.record(PENTA_ROOTS_INTERNAL,"Unknown native root residual failure.");return -1;}
}
int jacobian(N_Vector x,N_Vector,SUNMatrix matrix,void* raw,N_Vector,N_Vector)noexcept{
    auto& state=*static_cast<State*>(raw);try{state.evaluate(N_VGetArrayPointer(x),state.temporary.data(),state.jacobian.data());
        const auto size=state.request.dimension;for(uint64_t row=0;row<size;++row)for(uint64_t column=0;column<size;++column)SM_ELEMENT_D(matrix,row,column)=state.jacobian[row*size+column];return 0;}
    catch(const Failure& error){state.record(error.code,error.what());return -1;}catch(const std::exception& error){state.record(PENTA_ROOTS_INTERNAL,error.what());return -1;}catch(...){state.record(PENTA_ROOTS_INTERNAL,"Unknown native root Jacobian failure.");return -1;}
}
void kin(int status,const char* operation,const State& state){if(status<0)throw Failure(PENTA_ROOTS_INTERNAL,std::string("KINSOL ")+operation+" failed ("+std::to_string(status)+"). "+state.error);}
void scalar(State& state,std::vector<double>& x,std::vector<double>& f){
    const auto& request=state.request;auto& output=state.result;
    double lower=request.lower,upper=request.upper,fl=0,fu=0;
    state.evaluate(&lower,&fl,nullptr);state.evaluate(&upper,&fu,nullptr);
    auto tolerance=[&](double a,double b){return std::abs(b-a)<=request.absolute_tolerance+request.relative_tolerance*std::max(std::abs(a),std::abs(b));};
    if(fl==0||fu==0){x[0]=fl==0?lower:upper;f[0]=0;output.lower=output.upper=x[0];output.converged=1;output.backend_flag=0;message(output.status,sizeof(output.status),"exact_endpoint");norms(state,f);return;}
    require(std::signbit(fl)!=std::signbit(fu),"root requires opposite signs at finite bracket endpoints (or an exact endpoint root).");
    // Keep one evaluation for the returned candidate's residual verification.
    std::uintmax_t iterations=std::min<uint64_t>(request.max_iterations,request.max_evaluations-output.evaluations-1);
    auto function=[&](double value){double result=0;state.evaluate(&value,&result,nullptr);return result;};
    const auto bracket=boost::math::tools::toms748_solve(function,lower,upper,fl,fu,tolerance,iterations);
    output.iterations=static_cast<uint64_t>(iterations);output.backend_evaluations=output.iterations+2;output.lower=bracket.first;output.upper=bracket.second;output.step_norm=std::abs(output.upper-output.lower);
    x[0]=output.lower+(output.upper-output.lower)*.5;
    state.verify(x,f);norms(state,f);
    output.converged=output.scaled_residual_norm<=request.residual_tolerance;
    output.backend_flag=output.converged?0:tolerance(output.lower,output.upper)?1:2;
    message(output.status,sizeof(output.status),output.converged?"residual_satisfied":output.backend_flag==1?"bracket_converged_residual_not_satisfied":"iteration_limit");
}
void system(State& state,std::vector<double>& x,std::vector<double>& f){
    const auto& request=state.request;auto& output=state.result;const auto n=static_cast<sunindextype>(request.dimension);Resources resources;
    require(SUNContext_Create(SUN_COMM_NULL,&resources.context)==0,"Cannot create SUNDIALS roots context.");
    SUNContext_ClearErrHandlers(resources.context);SUNContext_PushErrHandler(resources.context,sundialsError,&state);
    resources.x=N_VNew_Serial(n,resources.context);resources.uscale=N_VNew_Serial(n,resources.context);resources.fscale=N_VNew_Serial(n,resources.context);
    if(!resources.x||!resources.uscale||!resources.fscale)throw Failure(PENTA_ROOTS_INTERNAL,"Cannot allocate KINSOL vectors.");
    for(uint64_t i=0;i<request.dimension;++i){NV_Ith_S(resources.x,i)=x[i];NV_Ith_S(resources.uscale,i)=request.variable_scale?request.variable_scale[i]:1;NV_Ith_S(resources.fscale,i)=request.residual_scale?request.residual_scale[i]:1;}
    resources.solver=KINCreate(resources.context);if(!resources.solver)throw Failure(PENTA_ROOTS_INTERNAL,"Cannot create KINSOL solver.");
    kin(KINInit(resources.solver,residual,resources.x),"initialization",state);kin(KINSetUserData(resources.solver,&state),"user data",state);
    kin(KINSetNumMaxIters(resources.solver,static_cast<long int>(request.max_iterations)),"iteration limit",state);
    kin(KINSetFuncNormTol(resources.solver,request.residual_tolerance),"residual tolerance",state);
    kin(KINSetScaledStepTol(resources.solver,request.absolute_tolerance),"step tolerance",state);
    double scaledNorm=0;for(uint64_t i=0;i<request.dimension;++i){const double scaled=x[i]*(request.variable_scale?request.variable_scale[i]:1);if(!std::isfinite(scaled))throw Failure(PENTA_ROOTS_DOMAIN,"Scaled root initial value overflowed.");scaledNorm=std::hypot(scaledNorm,scaled);}
    if(!std::isfinite(scaledNorm))throw Failure(PENTA_ROOTS_DOMAIN,"Scaled root initial norm overflowed.");
    const double automaticStep=std::max(1.0,scaledNorm)>std::numeric_limits<double>::max()/1000?std::numeric_limits<double>::max():1000*std::max(1.0,scaledNorm);
    kin(KINSetMaxNewtonStep(resources.solver,request.maximum_newton_step>0?request.maximum_newton_step:automaticStep),"maximum Newton step",state);
    resources.matrix=SUNDenseMatrix(n,n,resources.context);if(!resources.matrix)throw Failure(PENTA_ROOTS_INTERNAL,"Cannot allocate KINSOL matrix.");
    resources.linear=SUNLinSol_Dense(resources.x,resources.matrix,resources.context);if(!resources.linear)throw Failure(PENTA_ROOTS_INTERNAL,"Cannot allocate KINSOL linear solver.");
    kin(KINSetLinearSolver(resources.solver,resources.linear,resources.matrix),"linear solver",state);
    if(!request.finite_difference)kin(KINSetJacFn(resources.solver,jacobian),"generated Jacobian",state);
    state.temporary.resize(request.dimension);state.jacobian.resize(request.dimension*request.dimension);
    output.backend_flag=KINSol(resources.solver,resources.x,request.line_search?KIN_LINESEARCH:KIN_NONE,resources.uscale,resources.fscale);
    long int iterations=0,evaluations=0,jacobians=0;sunrealtype step=0;
    KINGetNumNonlinSolvIters(resources.solver,&iterations);KINGetNumFuncEvals(resources.solver,&evaluations);KINGetNumJacEvals(resources.solver,&jacobians);KINGetStepLength(resources.solver,&step);
    output.iterations=static_cast<uint64_t>(std::max(0L,iterations));output.backend_evaluations=static_cast<uint64_t>(std::max(0L,evaluations));output.jacobian_evaluations=static_cast<uint64_t>(std::max(0L,jacobians));output.step_norm=std::isfinite(step)?step:0;
    if(state.failure)throw Failure(state.failure,state.error.empty()?"Native root callback failed.":state.error);
    for(uint64_t i=0;i<request.dimension;++i)x[i]=NV_Ith_S(resources.x,i);
    state.verify(x,f);norms(state,f);
    output.converged=output.backend_flag>=0&&output.scaled_residual_norm<=request.residual_tolerance;
    const char* status=output.converged?"residual_satisfied":output.backend_flag==KIN_STEP_LT_STPTOL?"small_step_without_root":output.backend_flag==KIN_MAXITER_REACHED?"iteration_limit":output.backend_flag==KIN_LINESEARCH_NONCONV?"line_search_failed":output.backend_flag<0?"solver_failed":"residual_not_satisfied";
    message(output.status,sizeof(output.status),status);
    if(!output.converged&&!state.error.empty())message(output.message,sizeof(output.message),state.error);
}
int solve(const penta_roots_host_v1* host,const penta_roots_request_v1* request,penta_roots_result_v1* output,char* error,uint64_t capacity)noexcept{
    try{
        if(!host||host->struct_size<offsetof(penta_roots_host_v1,output)||host->abi_version!=PENTA_ROOTS_ABI_V1)throw Failure(PENTA_ROOTS_VERSION,"Incompatible roots host ABI.");
        require(request&&request->struct_size>=sizeof(*request)&&output&&output->struct_size>=sizeof(*output),"Invalid roots request/result structure.");
        require(request->dimension>=1&&request->dimension<=128,"Root dimension must be 1..128 real coordinates.");
        require(request->operation>=PENTA_ROOTS_SCALAR&&request->operation<=PENTA_ROOTS_FIXEDPOINT,"Unknown root operation.");
        require(request->max_iterations>=1&&request->max_iterations<=100000&&request->max_evaluations>=3&&request->max_evaluations<=1000000,"Root iteration/evaluation budgets are out of bounds.");
        require(std::isfinite(request->absolute_tolerance)&&request->absolute_tolerance>0&&std::isfinite(request->relative_tolerance)&&request->relative_tolerance>=0&&std::isfinite(request->residual_tolerance)&&request->residual_tolerance>0&&std::isfinite(request->maximum_wall_seconds)&&request->maximum_wall_seconds>0&&request->maximum_wall_seconds<=3600,"Root tolerances and wall budget must be finite and positive (relative tolerance may be zero).");
        require(request->line_search<=1&&request->finite_difference<=1&&request->evaluate,"Invalid root method or native callback.");
        require(std::isfinite(request->maximum_newton_step)&&request->maximum_newton_step>=0,"max_newton_step must be finite and nonnegative (0 selects automatic).");
        require(output->solution&&output->residual&&output->solution_capacity>=request->dimension&&output->residual_capacity>=request->dimension,"Root output buffers are too small.");
        const auto n=request->dimension;std::vector<double> solution(n),values(n);
        for(uint64_t i=0;i<n;++i){if(request->operation!=PENTA_ROOTS_SCALAR){require(request->initial&&std::isfinite(request->initial[i]),"Root initial values must be finite.");solution[i]=request->initial[i];}if(request->variable_scale)require(std::isfinite(request->variable_scale[i])&&request->variable_scale[i]>0,"variable_scale entries must be finite and positive.");if(request->residual_scale)require(std::isfinite(request->residual_scale[i])&&request->residual_scale[i]>0,"residual_scale entries must be finite and positive.");}
        if(request->operation==PENTA_ROOTS_SCALAR)require(n==1&&std::isfinite(request->lower)&&std::isfinite(request->upper)&&request->lower<request->upper&&std::isfinite(request->upper-request->lower),"root requires finite increasing bracket endpoints and representable finite bracket width.");
        penta_roots_result_v1 candidate{};candidate.struct_size=sizeof(candidate);State state(*host,*request,candidate);state.stop();
        if(request->operation==PENTA_ROOTS_SCALAR){candidate.lower=request->lower;candidate.upper=request->upper;}
        try{if(request->operation==PENTA_ROOTS_SCALAR)scalar(state,solution,values);else system(state,solution,values);}catch(const Failure& failure){if(failure.code!=observerStop||state.lastX.empty())throw;solution=state.lastX;values=state.lastResidual;state.failure=0;norms(state,values);candidate.converged=0;message(candidate.status,sizeof(candidate.status),"output_function_stop");message(candidate.message,sizeof(candidate.message),"Stopped cleanly by OutputFcn; retained the last finite evaluated candidate.");}
        if(!state.userStop)state.stop();
        state.notify(PENTA_ITERATION_DONE,solution,values);std::copy(solution.begin(),solution.end(),output->solution);std::copy(values.begin(),values.end(),output->residual);
        candidate.solution=output->solution;candidate.solution_capacity=output->solution_capacity;candidate.residual=output->residual;candidate.residual_capacity=output->residual_capacity;*output=candidate;return PENTA_ROOTS_OK;
    }catch(const Failure& failure){message(error,capacity,failure.what());return failure.code;}catch(const std::exception& failure){message(error,capacity,failure.what());return PENTA_ROOTS_INTERNAL;}catch(...){message(error,capacity,"Unknown roots module failure.");return PENTA_ROOTS_INTERNAL;}
}
const penta_roots_api_v1 api{sizeof(penta_roots_api_v1),PENTA_ROOTS_ABI_V1,"penta_roots","Boost.Math TOMS748 / SUNDIALS KINSOL7-output",3|PENTA_ITERATION_CAPABILITY_V1,solve};
}
extern "C" PENTA_ROOTS_EXPORT const penta_roots_api_v1* penta_roots_query_v1(uint32_t requested){return requested==PENTA_ROOTS_ABI_V1?&api:nullptr;}
