#include "dae_api.h"
#include <idas/idas.h>
#include <nvector/nvector_serial.h>
#include <sunmatrix/sunmatrix_dense.h>
#include <sunlinsol/sunlinsol_dense.h>
#include <Eigen/LU>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>
namespace {
void message(char* out,uint64_t cap,const char* text){if(out&&cap){const auto n=std::min<uint64_t>(cap-1,std::strlen(text));std::memcpy(out,text,n);out[n]=0;}}
struct Resources {
    SUNContext context=nullptr;N_Vector y=nullptr,yp=nullptr,id=nullptr;SUNMatrix matrix=nullptr;SUNLinearSolver linear=nullptr;void* solver=nullptr;
    ~Resources(){if(solver)IDAFree(&solver);if(linear)SUNLinSolFree(linear);if(matrix)SUNMatDestroy(matrix);if(id)N_VDestroy(id);if(yp)N_VDestroy(yp);if(y)N_VDestroy(y);if(context)SUNContext_Free(&context);}
};
struct Data {
    const penta_dae_host_v1& host;const penta_dae_request_v1& request;penta_dae_result_v1& report;
    std::chrono::steady_clock::time_point started=std::chrono::steady_clock::now();
    std::vector<double> f,jy,jyp;std::string failure;
    void check(){if(host.cancelled&&host.cancelled(host.context))throw std::runtime_error("DAE solve cancelled.");if(std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count()>request.max_wall_seconds)throw std::runtime_error("DAE wall-time budget exceeded.");}
    void evaluate(double t,const double* y,const double* yp,bool derivative){
        check();if(report.residual_evaluations>=request.max_evaluations)throw std::runtime_error("DAE native residual evaluation budget exceeded.");char error[2048]{};const auto n=request.dimension;
        if(host.charge&&host.charge(host.context,n*(derivative?2*n+1:1),error,sizeof(error)))throw std::runtime_error(error[0]?error:"DAE native work budget exceeded.");
        ++report.residual_evaluations;if(derivative)++report.jacobian_evaluations;
        if(request.residual(request.callback_context,t,y,yp,f.data(),derivative?jy.data():nullptr,derivative?jyp.data():nullptr,error,sizeof(error)))throw std::runtime_error(error[0]?error:"Compiled DAE residual failed.");
        for(const auto value:f)if(!std::isfinite(value))throw std::runtime_error("Non-finite DAE residual.");
        if(derivative)for(const auto* matrix:{&jy,&jyp})for(const auto value:*matrix)if(!std::isfinite(value))throw std::runtime_error("Non-finite generated DAE Jacobian.");
    }
    double norm()const{double maximum=0;for(const auto value:f)maximum=std::max(maximum,std::abs(value));return maximum;}
    void indexCheck(double t,const double* y,const double* yp){
        evaluate(t,y,yp,true);const auto n=request.dimension;Eigen::MatrixXd matrix(n,n);
        for(unsigned row=0;row<n;++row)for(unsigned column=0;column<n;++column){if(request.algebraic[column]&&jyp[row*n+column]!=0)throw std::runtime_error("Declared algebraic coordinates must have zero derivative columns in the initial residual Jacobian.");matrix(row,column)=request.algebraic[column]?jy[row*n+column]:jyp[row*n+column];}
        // Equilibrate rows and columns before the local index-one rank check.
        // This checks solvability for algebraic y and differential y', without
        // ever inverting a singular mass matrix or claiming global index proof.
        for(unsigned row=0;row<n;++row){const double scale=matrix.row(row).cwiseAbs().maxCoeff();if(scale==0)throw std::runtime_error("Singular/higher-index residual: index-one initialization matrix has a zero row.");matrix.row(row)/=scale;}
        for(unsigned column=0;column<n;++column){const double scale=matrix.col(column).cwiseAbs().maxCoeff();if(scale==0)throw std::runtime_error("Singular/higher-index residual: index-one initialization matrix has a zero column.");matrix.col(column)/=scale;}
        Eigen::FullPivLU<Eigen::MatrixXd> factor(matrix);factor.setThreshold(request.index_tolerance);if(factor.rank()!=n)throw std::runtime_error("Singular/higher-index residual: local index-one initialization matrix is rank deficient.");
    }
};
int residual(sunrealtype t,N_Vector y,N_Vector yp,N_Vector f,void* raw)noexcept{auto& d=*static_cast<Data*>(raw);try{d.evaluate(t,N_VGetArrayPointer(y),N_VGetArrayPointer(yp),false);std::copy(d.f.begin(),d.f.end(),N_VGetArrayPointer(f));return 0;}catch(const std::exception& e){try{d.failure=e.what();}catch(...){}return -1;}catch(...){return -1;}}
int jacobian(sunrealtype t,sunrealtype cj,N_Vector y,N_Vector yp,N_Vector,SUNMatrix matrix,void* raw,N_Vector,N_Vector,N_Vector)noexcept{auto& d=*static_cast<Data*>(raw);try{d.evaluate(t,N_VGetArrayPointer(y),N_VGetArrayPointer(yp),true);const auto n=d.request.dimension;for(unsigned row=0;row<n;++row)for(unsigned column=0;column<n;++column)SM_ELEMENT_D(matrix,row,column)=d.jy[row*n+column]+cj*d.jyp[row*n+column];return 0;}catch(const std::exception& e){try{d.failure=e.what();}catch(...){}return -1;}catch(...){return -1;}}
int event(sunrealtype t,N_Vector y,N_Vector yp,sunrealtype* value,void* raw)noexcept{auto& d=*static_cast<Data*>(raw);try{d.check();char error[2048]{};++d.report.event_evaluations;if(d.request.event(d.request.event_context,t,N_VGetArrayPointer(y),N_VGetArrayPointer(yp),value,error,sizeof(error)))throw std::runtime_error(error[0]?error:"Compiled DAE event failed.");if(!std::isfinite(*value))throw std::runtime_error("DAE event returned a non-finite value.");return 0;}catch(const std::exception& e){try{d.failure=e.what();}catch(...){}return -1;}catch(...){return -1;}}
void solverError(int,const char*,const char*,const char* message,SUNErrCode,void* raw,SUNContext)noexcept{auto& d=*static_cast<Data*>(raw);try{if(d.failure.empty())d.failure=message?message:"IDAS failure";}catch(...) {}}
void require(int status,const char* operation,Data& d){d.check();if(status<0)throw std::runtime_error(std::string("IDAS ")+operation+" failed ("+std::to_string(status)+"). "+d.failure);}
int solve(const penta_dae_host_v1* host,const penta_dae_request_v1* request,penta_dae_result_v1* result,char* error,uint64_t capacity)noexcept{
    try{
        if(!host||host->struct_size<sizeof(*host)||host->abi_version!=PENTA_DAE_ABI_V1||!request||request->struct_size<sizeof(*request)||!result||result->struct_size<sizeof(*result))throw std::runtime_error("Incompatible DAE ABI.");
        const auto& r=*request;auto& out=*result;const auto n=r.dimension;
        if(!n||n>32||r.initialization>1||r.jacobian>1||!r.initial||!r.initial_derivative||!r.algebraic||!r.residual||!out.times||!out.values||!out.derivatives||!out.consistent_initial||!out.consistent_derivative||!r.max_steps||r.max_steps>10000000||!r.max_evaluations||r.max_evaluations>10000000)throw std::runtime_error("Invalid DAE layout, initialization or resource limits.");
        for(const auto value:{r.t0,r.t1,r.sample_step,r.rtol,r.atol,r.initial_residual_tolerance,r.index_tolerance,r.max_wall_seconds,r.initial_step,r.maximum_step,r.minimum_event_interval})if(!std::isfinite(value))throw std::runtime_error("DAE numerical settings must be finite.");
        if(r.t1<=r.t0||r.sample_step<=0||r.rtol<=0||r.rtol>=1||r.atol<=0||r.initial_residual_tolerance<=0||r.index_tolerance<=0||r.index_tolerance>=.1||r.max_wall_seconds<=0||r.initial_step<0||r.maximum_step<0||r.minimum_event_interval<=0)throw std::runtime_error("Invalid DAE time/tolerance settings.");
        if((r.reset&&!r.event)||r.event_direction<-1||r.event_direction>1||r.event_terminal>1||(r.event&&(!r.max_events||r.max_events>100000||out.event_capacity<r.max_events||!out.event_times||!out.event_before||!out.event_after)))throw std::runtime_error("Invalid DAE native event/reset settings.");
        for(unsigned j=0;j<n;++j)if(!std::isfinite(r.initial[j])||!std::isfinite(r.initial_derivative[j])||r.algebraic[j]>1)throw std::runtime_error("DAE initial values must be finite and algebraic mask entries must be zero or one.");
        out.samples=out.events=out.steps=out.residual_evaluations=out.jacobian_evaluations=out.event_evaluations=0;out.terminal_event=0;out.initial_correction_norm=out.maximum_sample_residual=0;
        Data data{*host,r,out, std::chrono::steady_clock::now(),std::vector<double>(n),std::vector<double>(n*n),std::vector<double>(n*n),{}};data.indexCheck(r.t0,r.initial,r.initial_derivative);
        Resources resources;if(SUNContext_Create(SUN_COMM_NULL,&resources.context))throw std::runtime_error("Cannot allocate IDAS context.");SUNContext_ClearErrHandlers(resources.context);SUNContext_PushErrHandler(resources.context,solverError,&data);
        resources.y=N_VNew_Serial(n,resources.context);if(!resources.y)throw std::runtime_error("Cannot allocate IDAS state.");resources.yp=N_VClone(resources.y);resources.id=N_VClone(resources.y);if(!resources.yp||!resources.id)throw std::runtime_error("Cannot allocate IDAS derivative/mask.");std::copy_n(r.initial,n,N_VGetArrayPointer(resources.y));std::copy_n(r.initial_derivative,n,N_VGetArrayPointer(resources.yp));for(unsigned j=0;j<n;++j)N_VGetArrayPointer(resources.id)[j]=r.algebraic[j]?0:1;
        resources.solver=IDACreate(resources.context);if(!resources.solver)throw std::runtime_error("Cannot create IDAS integrator.");require(IDAInit(resources.solver,residual,r.t0,resources.y,resources.yp),"initialization",data);require(IDASetUserData(resources.solver,&data),"user data",data);require(IDASetId(resources.solver,resources.id),"differential/algebraic mask",data);require(IDASStolerances(resources.solver,r.rtol,r.atol),"tolerances",data);require(IDASetMaxNumSteps(resources.solver,static_cast<long>(r.max_steps)),"step budget",data);require(IDASetStopTime(resources.solver,r.t1),"stop time",data);if(r.initial_step)require(IDASetInitStep(resources.solver,r.initial_step),"initial step",data);if(r.maximum_step)require(IDASetMaxStep(resources.solver,r.maximum_step),"maximum step",data);
        resources.matrix=SUNDenseMatrix(n,n,resources.context);if(!resources.matrix)throw std::runtime_error("Cannot allocate IDAS Jacobian.");resources.linear=SUNLinSol_Dense(resources.y,resources.matrix,resources.context);if(!resources.linear)throw std::runtime_error("Cannot allocate IDAS dense solver.");require(IDASetLinearSolver(resources.solver,resources.linear,resources.matrix),"linear solver",data);require(IDASetJacFn(resources.solver,r.jacobian?nullptr:jacobian),"Jacobian selection",data);
        auto initialize=[&](double t){
            if(r.initialization){const double target=std::min(r.t1,t+std::min(r.sample_step,.01*(r.t1-r.t0)));if(target<=t)throw std::runtime_error("Consistent initialization requires a representable forward interval.");require(IDACalcIC(resources.solver,IDA_YA_YDP_INIT,target),"consistent initialization",data);require(IDAGetConsistentIC(resources.solver,resources.y,resources.yp),"consistent initial values",data);}
            data.indexCheck(t,N_VGetArrayPointer(resources.y),N_VGetArrayPointer(resources.yp));if(data.norm()>r.initial_residual_tolerance)throw std::runtime_error("Initial/reset DAE residual exceeds initial_residual_tolerance; supply consistent values or select initialization=consistent.");
        };initialize(r.t0);out.initial_residual_norm=data.norm();std::copy_n(N_VGetArrayPointer(resources.y),n,out.consistent_initial);std::copy_n(N_VGetArrayPointer(resources.yp),n,out.consistent_derivative);for(unsigned j=0;j<n;++j)out.initial_correction_norm=std::max({out.initial_correction_norm,std::abs(out.consistent_initial[j]-r.initial[j]),std::abs(out.consistent_derivative[j]-r.initial_derivative[j])});
        if(r.event){require(IDARootInit(resources.solver,1,event),"event initialization",data);int direction=r.event_direction;require(IDASetRootDirection(resources.solver,&direction),"event direction",data);require(IDASetNoInactiveRootWarn(resources.solver),"initial event policy",data);}
        auto save=[&](double t){if(out.samples>=out.capacity)throw std::runtime_error("DAE retained-sample budget exceeded.");data.evaluate(t,N_VGetArrayPointer(resources.y),N_VGetArrayPointer(resources.yp),false);out.maximum_sample_residual=std::max(out.maximum_sample_residual,data.norm());const auto row=out.samples++;out.times[row]=t;std::copy_n(N_VGetArrayPointer(resources.y),n,out.values+row*n);std::copy_n(N_VGetArrayPointer(resources.yp),n,out.derivatives+row*n);};
        uint64_t committedSteps=0;auto stepCount=[&]{long int steps=0;require(IDAGetNumSteps(resources.solver,&steps),"step statistics",data);out.steps=committedSteps+static_cast<uint64_t>(steps);if(out.steps>r.max_steps)throw std::runtime_error("DAE step budget exceeded.");};
        double t=r.t0,lastEvent=-std::numeric_limits<double>::infinity();save(t);uint64_t sampleIndex=1;
        while(t<r.t1&&!out.terminal_event){data.check();const double target=std::min(r.t1,r.t0+sampleIndex*r.sample_step);if(target<=t){++sampleIndex;continue;}
            while(t<target){if(out.steps>=r.max_steps)throw std::runtime_error("DAE step budget exceeded.");require(IDASetMaxNumSteps(resources.solver,static_cast<long>(r.max_steps-out.steps)),"remaining step budget",data);sunrealtype reached=t;const int status=IDASolve(resources.solver,target,&reached,resources.y,resources.yp,IDA_NORMAL);require(status,"integration",data);t=reached;stepCount();
                if(status==IDA_ROOT_RETURN){if(out.events>=r.max_events||t-lastEvent<r.minimum_event_interval)throw std::runtime_error("DAE event budget/minimum interval exceeded (possible chattering/Zeno behavior).");lastEvent=t;const auto eventIndex=out.events++;out.event_times[eventIndex]=t;std::copy_n(N_VGetArrayPointer(resources.y),n,out.event_before+eventIndex*n);
                    if(r.reset){std::vector<double> y(n),yp(n);char detail[2048]{};if(r.reset(r.reset_context,t,N_VGetArrayPointer(resources.y),N_VGetArrayPointer(resources.yp),y.data(),yp.data(),detail,sizeof(detail)))throw std::runtime_error(detail[0]?detail:"Compiled DAE reset failed.");for(unsigned j=0;j<n;++j)if(!std::isfinite(y[j])||!std::isfinite(yp[j]))throw std::runtime_error("DAE reset returned a non-finite state/derivative.");std::copy(y.begin(),y.end(),N_VGetArrayPointer(resources.y));std::copy(yp.begin(),yp.end(),N_VGetArrayPointer(resources.yp));committedSteps=out.steps;require(IDAReInit(resources.solver,t,resources.y,resources.yp),"native reset restart",data);require(IDASetStopTime(resources.solver,r.t1),"reset stop time",data);initialize(t);}
                    std::copy_n(N_VGetArrayPointer(resources.y),n,out.event_after+eventIndex*n);if(r.event_terminal){out.terminal_event=1;break;}
                }else if(status!=IDA_SUCCESS&&status!=IDA_TSTOP_RETURN)throw std::runtime_error("Unexpected IDAS integration return.");
            }
            save(t);++sampleIndex;
        }
        out.elapsed_seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-data.started).count();return 0;
    }catch(const std::exception& failure){message(error,capacity,failure.what());return 1;}catch(...){message(error,capacity,"Unknown native DAE failure.");return 4;}
}
const penta_dae_api_v1 api{sizeof(api),PENTA_DAE_ABI_V1,solve};
}
extern "C" PENTA_NUMERIC_EXPORT const penta_dae_api_v1*penta_dae_query_v1(uint32_t version){return version==PENTA_DAE_ABI_V1?&api:nullptr;}
