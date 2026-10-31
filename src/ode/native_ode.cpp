#include "native_internal.hpp"
#include "ode/native_build_config.hpp"
#include <cvodes/cvodes.h>
#include <cvodes/cvodes_ls.h>
#include <arkode/arkode.h>
#include <arkode/arkode_arkstep.h>
#include <arkode/arkode_erkstep.h>
#include <arkode/arkode_ls.h>
#include <nvector/nvector_serial.h>
#include <sunmatrix/sunmatrix_dense.h>
#include <sunlinsol/sunlinsol_dense.h>
#include <sundials/sundials_context.h>
#include <algorithm>
#include <array>
#include <exception>
#include <cmath>
#include <limits>
#include <numeric>
#include <sstream>

namespace penta::ode {
const char* backendIdentity() noexcept {return PENTA_ODE_SOLVER_BUILD_ID;}
OdeError::OdeError(std::string errorCode,const std::string& message,language::SourceSpan sourceSpan)
    :std::runtime_error(message),code(std::move(errorCode)),span(sourceSpan){}
std::size_t Solution::coordinateCount()const noexcept{return std::accumulate(stateWidths.begin(),stateWidths.end(),std::size_t{0});}
std::size_t Solution::retainedCoordinateCount()const noexcept{return values.size()+derivatives.size()+times.size()+sensitivities.size()+eventTimes.size()+eventValues.size()+eventIndices.size()+eventDirections.size()+denseCoordinateCount;}
namespace {
using Clock=std::chrono::steady_clock;
// ARKODE limits its Hermite interpolant to q-1 for these fixed tables. Preserve
// DP5's quartic output accuracy and retain exactly that same native polynomial.
int arkodeInterpolantDegree(Method method){return method==Method::Erk?4:3;}
void validateValues(const detail::Generated& model,double gamma,double time,const std::vector<double>& state,const std::vector<double>& parameters){
    if(!std::isfinite(gamma)||gamma<0||gamma>1||!std::isfinite(time))throw OdeError("O1001","ODE gamma/time must be finite, with gamma in [0,1].");
    if(state.size()!=model.coordinates||parameters.size()!=model.parameters)throw OdeError("O1001","ODE initial-state or parameter coordinate count does not match its declaration.");
    for(double x:state)if(!std::isfinite(x))throw OdeError("O1001","ODE state coordinates must be finite.");
    for(double x:parameters)if(!std::isfinite(x))throw OdeError("O1001","ODE parameter coordinates must be finite.");
}
std::vector<double> sampleTimes(const SolveOptions& o,std::size_t coordinates,std::size_t parameters){
    const bool validMethod=o.method==Method::Adams||o.method==Method::Bdf||o.method==Method::Erk||o.method==Method::Esdirk;
    if(!std::isfinite(o.t0)||!std::isfinite(o.t1)||!std::isfinite(o.sampleStep)||o.sampleStep<=0||!std::isfinite(o.rtol)||o.rtol<=0||o.rtol>=1||!std::isfinite(o.atol)||o.atol<=0||o.maxSteps==0||o.maxSteps>100000000||o.maxSamples==0||o.maxSamples>1000000||!std::isfinite(o.maximumWallSeconds)||o.maximumWallSeconds<=0||o.maximumWallSeconds>3600||(o.jacobian!="analytic"&&o.jacobian!="finite_difference")||!validMethod)throw OdeError("O1001","Invalid ODE interval, tolerances, method, Jacobian mode or resource limits.");
    if(!std::isfinite(o.initialStep)||!std::isfinite(o.maximumStep)||!std::isfinite(o.minimumStep)||o.initialStep<0||o.maximumStep<0||o.minimumStep<0||(o.maximumStep>0&&o.minimumStep>o.maximumStep)||o.maximumOrder<0||(o.method==Method::Bdf&&o.maximumOrder>5)||(o.method==Method::Adams&&o.maximumOrder>12))throw OdeError("O1001","Invalid native ODE step/order controls.");
    if((o.method==Method::Erk||o.method==Method::Esdirk)&&o.maximumOrder)throw OdeError("O1001","maximum_order applies only to BDF/Adams; ARKODE uses its declared embedded table.");
    if(o.maxDenseCoordinates<coordinates||o.maxDenseCoordinates>8000000)throw OdeError("O1001","Dense-output storage budget must be within the 8,000,000 coordinate bound.");
    if(o.maxRetainedCoordinates<1||o.maxRetainedCoordinates>32000000)throw OdeError("O1001","Combined retained-solution storage budget must be within1..32,000,000 coordinates.");
    if(o.eventCount>16||(o.eventCount&&!o.eventFunction)||(!o.eventDirections.empty()&&o.eventDirections.size()!=o.eventCount)||(!o.eventTerminal.empty()&&o.eventTerminal.size()!=o.eventCount))throw OdeError("O1001","Native events require 1..16 outputs and matching direction/terminal vectors.");
    for(int v:o.eventDirections)if(v < -1||v>1)throw OdeError("O1001","Event directions must be -1,0,+1 in physical increasing time.");
    for(int v:o.eventTerminal)if(v!=0&&v!=1)throw OdeError("O1001","Event terminal flags must be 0 or 1.");
    if(o.sensitivityParameters.size()>128||(!o.sensitivityInitial.empty()&&o.sensitivityInitial.size()!=coordinates*o.sensitivityParameters.size()))throw OdeError("O1001","Sensitivity seeds must have n state coordinates per selected direction (at most128).");
    if(!o.sensitivityParameters.empty()&&(o.method==Method::Erk||o.method==Method::Esdirk)){if(coordinates*(1+o.sensitivityParameters.size())>128||!o.sensitivityErrorControl||o.sensitivityMethod!="simultaneous")throw OdeError("O1001","ARKODE sensitivities require simultaneous error-controlled augmentation with at most128 coordinates.");if(o.method==Method::Esdirk&&o.jacobian=="analytic"&&(!o.stateHessian||coordinates*(coordinates+parameters)*(coordinates+parameters)>1048576))throw OdeError("O1001","Analytic ESDIRK sensitivities require a bounded compiler-generated full RHS Hessian.");}
    for(int p:o.sensitivityParameters)if(p < -1||p>=static_cast<int>(parameters))throw OdeError("O1001","Sensitivity parameter coordinate is outside the model declaration.");
    for(double x:o.sensitivityInitial)if(!std::isfinite(x))throw OdeError("O1001","Sensitivity initial seeds must be finite.");
    if(std::any_of(o.sensitivityParameters.begin(),o.sensitivityParameters.end(),[](int p){return p>=0;})&&!o.parameterJacobian)throw OdeError("O1001","Parameter sensitivities require a compiled parameter-partials evaluator.");
    if(o.sensitivityMethod!="simultaneous"&&o.sensitivityMethod!="staggered")throw OdeError("O1001","Sensitivity method must be simultaneous or staggered.");
    const double direction=o.t1>=o.t0?1.0:-1.0;
    std::vector<double> times{o.t0};
    if(!o.outputTimes.empty()){
        for(double t:o.outputTimes){if(!std::isfinite(t)||direction*(t-o.t0)<0||direction*(o.t1-t)<0)throw OdeError("O1001","Requested output times must be finite and inside the integration interval.");if(t==times.back())continue;if(direction*(t-times.back())<=0)throw OdeError("O1001","Requested output times must be strictly ordered in the integration direction.");times.push_back(t);}
    }else{
        const long double duration=std::abs(static_cast<long double>(o.t1)-o.t0);
        const long double count=std::ceil(duration/o.sampleStep)+1;
        if(!std::isfinite(count)||count>o.maxSamples+1)throw OdeError("O1006","ODE sampling exceeds max_samples.");
        if(duration>0&&o.t0+direction*o.sampleStep==o.t0)throw OdeError("O1001","ODE sample spacing is not representable at the initial time.");
        for(std::size_t i=1;;++i){const long double t=static_cast<long double>(o.t0)+direction*static_cast<long double>(i)*o.sampleStep;if(direction*(t-o.t1)>=0)break;const double rounded=static_cast<double>(t);if(direction*(rounded-o.t1)>=0)break;if(direction*(rounded-times.back())<=0)throw OdeError("O1001","ODE sample spacing loses distinct binary64 times.");times.push_back(rounded);if(times.size()>o.maxSamples)throw OdeError("O1006","ODE sampling exceeds max_samples.");}
    }
    if(times.back()!=o.t1)times.push_back(o.t1);
    const auto multiplier=std::max<std::size_t>(1,o.sensitivityParameters.size()+1);
    if(times.size()>o.maxSamples||times.size()>8000000/coordinates/multiplier)throw OdeError("O1006","ODE samples and sensitivities exceed the 8,000,000 stored-coordinate limit.");
    if(times.size()>o.maxRetainedCoordinates/(coordinates*(2+o.sensitivityParameters.size())+1))throw OdeError("O1006","Requested ODE states, slopes, times and sensitivities exceed the combined retained-coordinate budget.");
    return times;
}
struct Resources {
    SUNContext context=nullptr;N_Vector state=nullptr,work=nullptr,cost=nullptr;SUNMatrix matrix=nullptr;SUNLinearSolver linear=nullptr;void* integrator=nullptr;
    std::vector<N_Vector> sensitivity;
    bool arkode=false;
    ~Resources(){if(integrator){if(arkode)ARKodeFree(&integrator);else CVodeFree(&integrator);}if(linear)SUNLinSolFree(linear);if(matrix)SUNMatDestroy(matrix);for(auto v:sensitivity)if(v)N_VDestroy(v);if(cost)N_VDestroy(cost);if(work)N_VDestroy(work);if(state)N_VDestroy(state);if(context)SUNContext_Free(&context);}
};
OdeError modelFailure(const language::OdeDefinition& definition,const std::string& message){
    OdeError error("O1005",message,definition.span);if(definition.origin&&definition.origin->source)error.frames.push_back({definition.name,definition.origin->identity,detail::sha256(*definition.origin->source),definition.span,definition.origin->source});return error;
}
std::vector<language::DiagnosticFrame> failureFrames(const std::exception_ptr& failure){if(failure)try{std::rethrow_exception(failure);}catch(const OdeError& error){return error.frames;}catch(...){}return {};}
struct UserData {
    const detail::Model* model=nullptr;const language::OdeDefinition* definition=nullptr;std::exception_ptr failure;const NativeOptions* options=nullptr;const SolveOptions* settings=nullptr;const std::vector<double>* parameters=nullptr;
    double gamma=1,wallLimit=60;Clock::time_point start;
    std::vector<double> rhs,jacobian,parameterJacobian,massInverse;
    const RunningObjective* running=nullptr;std::vector<double> runningStateGradient,runningParameterGradient;
    bool augmented=false;std::vector<double> stateHessian;
    std::string error;bool cancelled=false,timedOut=false,evaluationFailed=false;
    const char* solver="CVODES";
    std::size_t eventEvaluations=0,sensitivityEvaluations=0;
    bool stop() noexcept {cancelled=options->cancellation&&options->cancellation->load(std::memory_order_relaxed);timedOut=std::chrono::duration<double>(Clock::now()-start).count()>wallLimit;return cancelled||timedOut;}
    void applyMass(double* values,std::size_t columns)const{
        if(massInverse.empty())return;
        const auto n=model->coordinates;std::vector<double> result(n*columns,0);
        for(std::size_t row=0;row<n;++row)for(std::size_t k=0;k<n;++k)for(std::size_t column=0;column<columns;++column)result[row*columns+column]+=massInverse[row*n+k]*values[k*columns+column];
        std::copy(result.begin(),result.end(),values);
    }
    int evaluate(double time,const double* y,double* f,double* J){
        if(options->chargeOperations)options->chargeOperations(model->coordinates*(J?model->coordinates+1:1));
        std::array<char,1024> message{};const int status=model->evaluate(time,gamma,y,parameters->data(),f,J,message.data(),message.size());
        if(status){evaluationFailed=true;error=message.data();if(definition)failure=std::make_exception_ptr(modelFailure(*definition,error));return status;}applyMass(f,1);if(J)applyMass(J,model->coordinates);return 0;
    }
};
void initializeMass(UserData& d){
    const auto& mass=d.settings->massMatrix;const auto n=d.model->coordinates;if(mass.empty())return;
    if(mass.size()!=n*n||n>128)throw OdeError("O1001","A constant mass matrix must be finite real n-by-n with n<=128.");
    std::vector<double> work=mass;d.massInverse.assign(n*n,0);double scale=0;for(double x:work){if(!std::isfinite(x))throw OdeError("O1001","Mass matrix entries must be finite.");scale=std::max(scale,std::abs(x));}for(std::size_t i=0;i<n;++i)d.massInverse[i*n+i]=1;
    for(std::size_t k=0;k<n;++k){std::size_t pivot=k;for(std::size_t i=k+1;i<n;++i)if(std::abs(work[i*n+k])>std::abs(work[pivot*n+k]))pivot=i;if(std::abs(work[pivot*n+k])<=scale*std::numeric_limits<double>::epsilon()*n*16)throw OdeError("O1001","Mass matrix is singular or numerically rank deficient; DAEs require a separate solver.");for(std::size_t j=0;j<n;++j){std::swap(work[k*n+j],work[pivot*n+j]);std::swap(d.massInverse[k*n+j],d.massInverse[pivot*n+j]);}const double diagonal=work[k*n+k];for(std::size_t j=0;j<n;++j){work[k*n+j]/=diagonal;d.massInverse[k*n+j]/=diagonal;}for(std::size_t i=0;i<n;++i)if(i!=k){const double a=work[i*n+k];for(std::size_t j=0;j<n;++j){work[i*n+j]-=a*work[k*n+j];d.massInverse[i*n+j]-=a*d.massInverse[k*n+j];}}}
}
int rhs(sunrealtype time,N_Vector y,N_Vector out,void* raw)noexcept{
    auto& d=*static_cast<UserData*>(raw);if(d.stop())return -1;try{const auto* input=N_VGetArrayPointer(y);auto* output=N_VGetArrayPointer(out);const auto status=d.evaluate(time,input,output,d.augmented?d.jacobian.data():nullptr);if(status||!d.augmented)return status;const auto n=d.model->coordinates,np=d.model->parameters;if(np&&d.settings->parameterJacobian){d.settings->parameterJacobian(time,input,d.parameterJacobian.data());d.applyMass(d.parameterJacobian.data(),np);}for(std::size_t direction=0;direction<d.settings->sensitivityParameters.size();++direction){const auto parameter=d.settings->sensitivityParameters[direction];for(std::size_t row=0;row<n;++row){long double value=parameter<0?0:d.parameterJacobian[row*np+static_cast<std::size_t>(parameter)];for(std::size_t column=0;column<n;++column)value+=static_cast<long double>(d.jacobian[row*n+column])*input[n*(direction+1)+column];output[n*(direction+1)+row]=static_cast<double>(value);if(!std::isfinite(output[n*(direction+1)+row]))throw OdeError("O1005","Nonfinite ARKODE sensitivity derivative.");}}++d.sensitivityEvaluations;return 0;}catch(const std::exception& e){d.evaluationFailed=true;d.failure=std::current_exception();try{d.error=e.what();}catch(...){}return -1;}catch(...){return -1;}
}
int jacobian(sunrealtype time,N_Vector y,N_Vector,SUNMatrix matrix,void* raw,N_Vector,N_Vector,N_Vector)noexcept{
    auto& d=*static_cast<UserData*>(raw);if(d.stop())return -1;try{const auto n=d.model->coordinates,np=d.model->parameters;const auto* input=N_VGetArrayPointer(y);const int status=d.evaluate(time,input,d.rhs.data(),d.jacobian.data());if(status)return status;SUNMatZero(matrix);for(std::size_t r=0;r<n;++r)for(std::size_t c=0;c<n;++c)SM_ELEMENT_D(matrix,r,c)=d.jacobian[r*n+c];if(d.augmented){const auto width=n+np;d.settings->stateHessian(time,input,d.stateHessian.data());d.applyMass(d.stateHessian.data(),width*width);for(std::size_t direction=0;direction<d.settings->sensitivityParameters.size();++direction){const auto parameter=d.settings->sensitivityParameters[direction];for(std::size_t row=0;row<n;++row)for(std::size_t column=0;column<n;++column){SM_ELEMENT_D(matrix,n*(direction+1)+row,n*(direction+1)+column)=d.jacobian[row*n+column];long double value=parameter<0?0:d.stateHessian[(row*width+column)*width+n+static_cast<std::size_t>(parameter)];for(std::size_t k=0;k<n;++k)value+=static_cast<long double>(d.stateHessian[(row*width+column)*width+k])*input[n*(direction+1)+k];const auto entry=static_cast<double>(value);if(!std::isfinite(entry))throw OdeError("O1005","Nonfinite analytic augmented ESDIRK Jacobian.");SM_ELEMENT_D(matrix,n*(direction+1)+row,column)=entry;}}}return 0;}catch(const std::exception& e){d.evaluationFailed=true;d.failure=std::current_exception();try{d.error=e.what();}catch(...){}return -1;}catch(...){return -1;}
}
int events(sunrealtype time,N_Vector y,sunrealtype* gout,void* raw)noexcept{
    auto& d=*static_cast<UserData*>(raw);if(d.stop())return -1;try{d.settings->eventFunction(time,N_VGetArrayPointer(y),gout);++d.eventEvaluations;for(std::size_t i=0;i<d.settings->eventCount;++i)if(!std::isfinite(gout[i]))throw OdeError("O1005","Native event callback produced a nonfinite value.");return 0;}catch(const std::exception& e){d.evaluationFailed=true;d.failure=std::current_exception();try{d.error=e.what();}catch(...){}return -1;}catch(...){return -1;}
}
int sensitivityRhs(int ns,sunrealtype time,N_Vector y,N_Vector,N_Vector* s,N_Vector* out,void* raw,N_Vector,N_Vector)noexcept{
    auto& d=*static_cast<UserData*>(raw);if(d.stop())return -1;try{const auto n=d.model->coordinates,np=d.model->parameters;const int status=d.evaluate(time,N_VGetArrayPointer(y),d.rhs.data(),d.jacobian.data());if(status)return status;
        if(d.settings->parameterJacobian&&np){d.settings->parameterJacobian(time,N_VGetArrayPointer(y),d.parameterJacobian.data());d.applyMass(d.parameterJacobian.data(),np);}++d.sensitivityEvaluations;
        for(int direction=0;direction<ns;++direction){const int parameter=d.settings->sensitivityParameters[static_cast<std::size_t>(direction)];const auto* input=N_VGetArrayPointer(s[direction]);auto* output=N_VGetArrayPointer(out[direction]);for(std::size_t row=0;row<n;++row){double value=parameter<0?0:d.parameterJacobian[row*np+static_cast<std::size_t>(parameter)];for(std::size_t k=0;k<n;++k)value+=d.jacobian[row*n+k]*input[k];if(!std::isfinite(value))throw OdeError("O1005","Native sensitivity derivative is nonfinite.");output[row]=value;}}
        return 0;
    }catch(const std::exception& e){d.evaluationFailed=true;d.failure=std::current_exception();try{d.error=e.what();}catch(...){}return -1;}catch(...){return -1;}
}
void sundialsError(int,const char*,const char*,const char* message,SUNErrCode,void* raw,SUNContext)noexcept{auto& d=*static_cast<UserData*>(raw);try{if(d.error.empty())d.error=message?std::string(message).substr(0,2048):"SUNDIALS integration error.";}catch(...) {}}
void require(int status,const char* operation,const UserData& d){if(status<0){const char* code=d.cancelled?"O1004":d.timedOut?"O1003":d.evaluationFailed?"O1005":"O1007";OdeError error(code,std::string(d.solver)+" "+operation+" failed ("+std::to_string(status)+"). "+d.error);error.frames=failureFrames(d.failure);throw error;}}
void checkStatus(int status,UserData& d,language::SourceSpan span){if(d.cancelled)throw OdeError("O1004","Native ODE integration cancelled.",span);if(d.timedOut)throw OdeError("O1003","Native ODE integration timed out.",span);if(status<0){OdeError error(status==(d.solver==std::string("ARKODE")?ARK_TOO_MUCH_WORK:CV_TOO_MUCH_WORK)?"O1006":d.evaluationFailed?"O1005":"O1007",std::string(d.solver)+" integration failed ("+std::to_string(status)+"). "+d.error,span);error.frames=failureFrames(d.failure);throw error;}}
void setup(Resources& r,UserData& d,const SolveOptions& o,const std::vector<double>& initial){
    const auto n=d.model->coordinates;r.arkode=o.method==Method::Erk||o.method==Method::Esdirk;const bool implicit=o.method!=Method::Erk;d.solver=r.arkode?"ARKODE":"CVODES";d.augmented=r.arkode&&!o.sensitivityParameters.empty();const auto m=n*(d.augmented?1+o.sensitivityParameters.size():1);if(d.augmented&&o.method==Method::Esdirk&&o.jacobian=="analytic")d.stateHessian.resize(n*(n+d.model->parameters)*(n+d.model->parameters));
    if(SUNContext_Create(SUN_COMM_NULL,&r.context)!=0)throw OdeError("O1007","Cannot create SUNDIALS context.");
    SUNContext_ClearErrHandlers(r.context);SUNContext_PushErrHandler(r.context,sundialsError,&d);
    r.state=N_VNew_Serial(static_cast<sunindextype>(m),r.context);if(!r.state)throw OdeError("O1007","Cannot allocate SUNDIALS state.");r.work=N_VClone(r.state);if(!r.work)throw OdeError("O1007","Cannot allocate dense-output workspace.");N_VConst(0,r.state);std::copy(initial.begin(),initial.end(),N_VGetArrayPointer(r.state));if(d.augmented&&!o.sensitivityInitial.empty())std::copy(o.sensitivityInitial.begin(),o.sensitivityInitial.end(),N_VGetArrayPointer(r.state)+n);
    if(o.method==Method::Erk)r.integrator=ERKStepCreate(rhs,o.t0,r.state,r.context);else if(o.method==Method::Esdirk)r.integrator=ARKStepCreate(nullptr,rhs,o.t0,r.state,r.context);else r.integrator=CVodeCreate(o.method==Method::Adams?CV_ADAMS:CV_BDF,r.context);if(!r.integrator)throw OdeError("O1007","Cannot allocate native ODE integrator.");
    if(r.arkode){if(o.method==Method::Erk)require(ERKStepSetTableNum(r.integrator,ARKODE_DORMAND_PRINCE_7_4_5),"Dormand-Prince table",d);else require(ARKStepSetTableNum(r.integrator,ARKODE_ESDIRK436L2SA_6_3_4,ARKODE_ERK_NONE),"ESDIRK table",d);require(ARKodeSetInterpolantType(r.integrator,ARK_INTERP_HERMITE),"Hermite interpolation type",d);require(ARKodeSetInterpolantDegree(r.integrator,arkodeInterpolantDegree(o.method)),"dense interpolation degree",d);require(ARKodeSStolerances(r.integrator,o.rtol,o.atol),"tolerances",d);require(ARKodeSetUserData(r.integrator,&d),"user data",d);require(ARKodeSetStopTime(r.integrator,o.t1),"stop time",d);
        if(o.initialStep)require(ARKodeSetInitStep(r.integrator,std::copysign(o.initialStep,o.t1-o.t0)),"initial step",d);
        if(o.maximumStep)require(ARKodeSetMaxStep(r.integrator,o.maximumStep),"maximum step",d);
        if(o.minimumStep)require(ARKodeSetMinStep(r.integrator,o.minimumStep),"minimum step",d);
    }else{require(CVodeInit(r.integrator,rhs,o.t0,r.state),"initialization",d);require(CVodeSStolerances(r.integrator,o.rtol,o.atol),"tolerances",d);require(CVodeSetUserData(r.integrator,&d),"user data",d);require(CVodeSetStopTime(r.integrator,o.t1),"stop time",d);if(o.initialStep)require(CVodeSetInitStep(r.integrator,std::copysign(o.initialStep,o.t1-o.t0)),"initial step",d);
        if(o.maximumStep)require(CVodeSetMaxStep(r.integrator,o.maximumStep),"maximum step",d);
        if(o.minimumStep)require(CVodeSetMinStep(r.integrator,o.minimumStep),"minimum step",d);
        if(o.maximumOrder)require(CVodeSetMaxOrd(r.integrator,o.maximumOrder),"maximum order",d);}
    if(implicit){r.matrix=SUNDenseMatrix(static_cast<sunindextype>(m),static_cast<sunindextype>(m),r.context);if(!r.matrix)throw OdeError("O1007","Cannot allocate dense ODE Jacobian matrix.");r.linear=SUNLinSol_Dense(r.state,r.matrix,r.context);if(!r.linear)throw OdeError("O1007","Cannot allocate dense ODE linear solver.");if(r.arkode){require(ARKodeSetLinearSolver(r.integrator,r.linear,r.matrix),"linear solver",d);require(ARKodeSetJacFn(r.integrator,o.jacobian=="analytic"?jacobian:nullptr),"Jacobian selection",d);}else{require(CVodeSetLinearSolver(r.integrator,r.linear,r.matrix),"linear solver",d);require(CVodeSetJacFn(r.integrator,o.jacobian=="analytic"?jacobian:nullptr),"Jacobian selection",d);}}
    if(o.eventCount){require(r.arkode?ARKodeRootInit(r.integrator,static_cast<int>(o.eventCount),events):CVodeRootInit(r.integrator,static_cast<int>(o.eventCount),events),"event initialization",d);std::vector<int> directions=o.eventDirections;if(directions.empty())directions.assign(o.eventCount,0);
        // SUNDIALS defines direction by the order of integration. The language
        // defines it by increasing physical time, including a backward solve.
        if(o.t1<o.t0)for(auto& direction:directions)direction=-direction;
        require(r.arkode?ARKodeSetRootDirection(r.integrator,directions.data()):CVodeSetRootDirection(r.integrator,directions.data()),"event direction",d);}
    const auto ns=o.sensitivityParameters.size();if(ns&&!r.arkode){r.sensitivity.resize(ns);for(std::size_t i=0;i<ns;++i){r.sensitivity[i]=N_VClone(r.state);if(!r.sensitivity[i])throw OdeError("O1007","Cannot allocate sensitivity vectors.");auto* out=N_VGetArrayPointer(r.sensitivity[i]);for(std::size_t k=0;k<n;++k)out[k]=o.sensitivityInitial.empty()?0:o.sensitivityInitial[i*n+k];}require(CVodeSensInit(r.integrator,static_cast<int>(ns),o.sensitivityMethod=="staggered"?CV_STAGGERED:CV_SIMULTANEOUS,sensitivityRhs,r.sensitivity.data()),"sensitivity initialization",d);std::vector<double> absolute(ns,o.atol);require(CVodeSensSStolerances(r.integrator,o.rtol,absolute.data()),"sensitivity tolerances",d);require(CVodeSetSensErrCon(r.integrator,o.sensitivityErrorControl?SUNTRUE:SUNFALSE),"sensitivity error control",d);}
}
void appendSample(Solution& out,Resources& r,UserData& d,double time,bool initial=false){
    const auto n=d.model->coordinates;const bool same=!out.times.empty()&&out.times.back()==time;if(same)return;
    if(out.retainedCoordinateCount()+n*(2+out.sensitivityCount)+1>d.settings->maxRetainedCoordinates)throw OdeError("O1006","Retained ODE values, slopes, times, dense segments, events and sensitivities exceed the combined coordinate budget.");
    if(out.times.size()>=d.settings->maxSamples)throw OdeError("O1006","Terminal/output sample exceeds max_samples.");
    if(initial)std::copy(N_VGetArrayPointer(r.state),N_VGetArrayPointer(r.state)+n*(d.augmented?1+out.sensitivityCount:1),N_VGetArrayPointer(r.work));else require(r.arkode?ARKodeGetDky(r.integrator,time,0,r.work):CVodeGetDky(r.integrator,time,0,r.work),"sample interpolation",d);
    const auto* values=N_VGetArrayPointer(r.work);out.times.push_back(time);for(std::size_t k=0;k<n;++k){if(!std::isfinite(values[k]))throw OdeError("O1005","Native ODE produced nonfinite state coordinates.");out.values.push_back(values[k]);}
    if(d.evaluate(time,values,d.rhs.data(),nullptr))throw modelFailure(*d.definition,"Cannot evaluate native endpoint slope. "+d.error);
    for(double x:d.rhs){if(!std::isfinite(x))throw OdeError("O1005","Native endpoint slope is nonfinite.");out.derivatives.push_back(x);}++out.stats.denseRhsEvaluations;
    if(d.options->sampleObserver)try{d.options->sampleObserver(time,{values,n});}catch(...){}
    if(d.augmented){for(std::size_t k=0;k<n*out.sensitivityCount;++k){const auto value=values[n+k];if(!std::isfinite(value))throw OdeError("O1005","Nonfinite native ARKODE sensitivity.");out.sensitivities.push_back(value);}}
    if(!r.sensitivity.empty()){if(!initial)require(CVodeGetSensDky(r.integrator,time,0,r.sensitivity.data()),"sensitivity interpolation",d);for(auto v:r.sensitivity){const auto* s=N_VGetArrayPointer(v);for(std::size_t k=0;k<n;++k){if(!std::isfinite(s[k]))throw OdeError("O1005","Native sensitivity is nonfinite.");out.sensitivities.push_back(s[k]);}}}
}
void retainDense(Solution& out,Resources& r,UserData& d,double reached){
    if(!d.settings->denseOutput)return;
    double current=0,h=0;int order=r.arkode?arkodeInterpolantDegree(d.settings->method):0;
    require(r.arkode?ARKodeGetCurrentTime(r.integrator,&current):CVodeGetCurrentTime(r.integrator,&current),"dense current time",d);require(r.arkode?ARKodeGetLastStep(r.integrator,&h):CVodeGetLastStep(r.integrator,&h),"dense last step",d);if(!r.arkode)require(CVodeGetLastOrder(r.integrator,&order),"dense order",d);
    if(!out.denseSegments.empty()&&out.denseSegments.back().center==current)return;
    DenseSegment segment;segment.first=current-h;segment.last=current;segment.center=current;segment.degree=static_cast<unsigned>(order);
    const auto n=d.model->coordinates,needed=(static_cast<std::size_t>(order)+1)*n;
    if(out.retainedCoordinateCount()+needed>d.settings->maxRetainedCoordinates)throw OdeError("O1006","Native dense output exceeds the combined solution coordinate budget.");
    if(needed>d.settings->maxDenseCoordinates-out.denseCoordinateCount)throw OdeError("O1006","Retained native dense output exceeds max_dense_coordinates; disable dense_output or raise its bounded budget.");
    segment.coefficients.reserve(needed);double factorial=1;for(int k=0;k<=order;++k){if(k>1)factorial*=k;require(r.arkode?ARKodeGetDky(r.integrator,current,k,r.work):CVodeGetDky(r.integrator,current,k,r.work),"dense polynomial coefficients",d);const auto* v=N_VGetArrayPointer(r.work);for(std::size_t i=0;i<n;++i)segment.coefficients.push_back(v[i]/factorial);}
    (void)reached;out.denseCoordinateCount+=needed;out.denseSegments.push_back(std::move(segment));
}
void finalStatistics(Solution& out,Resources& r,UserData& d){
    long steps=0,evaluations=0,jacobians=0,failures=0,nonlinear=0;require(r.arkode?ARKodeGetNumSteps(r.integrator,&steps):CVodeGetNumSteps(r.integrator,&steps),"step count",d);
    if(r.arkode){require(ARKodeGetNumRhsEvals(r.integrator,d.settings->method==Method::Erk?0:1,&evaluations),"RHS count",d);if(d.settings->method!=Method::Erk){require(ARKodeGetNumJacEvals(r.integrator,&jacobians),"Jacobian count",d);require(ARKodeGetNumNonlinSolvIters(r.integrator,&nonlinear),"nonlinear count",d);}require(ARKodeGetNumErrTestFails(r.integrator,&failures),"error-test count",d);require(ARKodeGetLastStep(r.integrator,&out.stats.lastStep),"last step",d);out.stats.lastOrder=d.settings->method==Method::Erk?5:4;}
    else{require(CVodeGetNumRhsEvals(r.integrator,&evaluations),"RHS count",d);require(CVodeGetNumJacEvals(r.integrator,&jacobians),"Jacobian count",d);require(CVodeGetNumNonlinSolvIters(r.integrator,&nonlinear),"nonlinear count",d);require(CVodeGetNumErrTestFails(r.integrator,&failures),"error-test count",d);require(CVodeGetLastStep(r.integrator,&out.stats.lastStep),"last step",d);require(CVodeGetLastOrder(r.integrator,&out.stats.lastOrder),"last order",d);}
    out.stats.steps=static_cast<std::size_t>(std::max(0L,steps));out.stats.rhsEvaluations=static_cast<std::size_t>(std::max(0L,evaluations));out.stats.jacobianEvaluations=static_cast<std::size_t>(std::max(0L,jacobians));out.stats.errorTestFailures=static_cast<std::size_t>(std::max(0L,failures));out.stats.nonlinearIterations=static_cast<std::size_t>(std::max(0L,nonlinear));out.stats.eventEvaluations=d.eventEvaluations;out.stats.sensitivityRhsEvaluations=d.sensitivityEvaluations;out.stats.elapsedSeconds=std::chrono::duration<double>(Clock::now()-d.start).count();
}
}
NativeEvaluation evaluateNative(const language::OdeDefinition& definition,double gamma,double time,const std::vector<double>& state,const std::vector<double>& parameters,const NativeOptions& options){
    const auto generated=detail::generate(definition);validateValues(generated,gamma,time,state,parameters);auto model=detail::compile(generated,options);detail::checkCancelled(options);NativeEvaluation result;result.rhs.resize(model->coordinates);result.jacobian.resize(model->coordinates*model->coordinates);result.cacheHit=model->cacheHit;result.key=model->key;std::array<char,1024> error{};if(model->evaluate(time,gamma,state.data(),parameters.data(),result.rhs.data(),result.jacobian.data(),error.data(),error.size()))throw modelFailure(definition,error.data());detail::checkCancelled(options);return result;
}
namespace {
std::shared_ptr<const Solution> solvePrepared(const language::OdeDefinition& definition,
    const detail::Generated& generated,const detail::Model* model,double gamma,
    const std::vector<double>& initial,const std::vector<double>& parameters,
    const SolveOptions& options,const NativeOptions& native,const std::vector<double>& requested){
    const auto start=Clock::now();
    auto out=std::make_shared<Solution>();out->stats.cacheHit=model->cacheHit;out->stats.key=model->key;out->stats.method=options.method==Method::Erk?"erk":options.method==Method::Esdirk?"esdirk":options.method==Method::Bdf?"bdf":"adams";out->stats.jacobian=options.method==Method::Erk?"not_used":options.jacobian;
    for(const auto& state:definition.states){out->stateNames.push_back(state.name);out->stateWidths.push_back(language::coordinateWidth(state.type,state.schema));}out->sensitivityCount=options.sensitivityParameters.size();out->sensitivityParameterIndices=options.sensitivityParameters;
    UserData data;data.definition=&definition;data.model=model;data.options=&native;data.settings=&options;data.parameters=&parameters;data.gamma=gamma;data.wallLimit=options.maximumWallSeconds;data.start=start;data.rhs.resize(generated.coordinates);data.jacobian.resize(generated.coordinates*generated.coordinates);data.parameterJacobian.resize(generated.coordinates*generated.parameters);initializeMass(data);
    if(options.t0==options.t1){out->times={options.t0};out->values=initial;if(data.evaluate(options.t0,initial.data(),data.rhs.data(),nullptr))throw modelFailure(definition,data.error);out->derivatives=data.rhs;out->sensitivities=options.sensitivityInitial;if(out->sensitivities.empty())out->sensitivities.assign(out->sensitivityCount*generated.coordinates,0);out->stats.denseRhsEvaluations=1;return out;}
    Resources resources;setup(resources,data,options,initial);appendSample(*out,resources,data,options.t0,true);const double direction=options.t1>options.t0?1:-1;std::size_t next=1;long steps=0;double reached=options.t0;
    while(direction*(options.t1-reached)>0){detail::checkCancelled(native);if(data.stop())checkStatus(-1,data,definition.span);if(static_cast<std::size_t>(steps)>=options.maxSteps)throw OdeError("O1006","Native ODE integration exceeded its total step budget.");require(resources.arkode?ARKodeSetMaxNumSteps(resources.integrator,static_cast<long>(options.maxSteps)):CVodeSetMaxNumSteps(resources.integrator,static_cast<long>(options.maxSteps)),"step limit",data);
        const double previous=reached;sunrealtype current=0;const int status=resources.arkode?ARKodeEvolve(resources.integrator,options.t1,resources.state,&current,ARK_ONE_STEP):CVode(resources.integrator,options.t1,resources.state,&current,CV_ONE_STEP);checkStatus(status,data,definition.span);reached=current;require(resources.arkode?ARKodeGetNumSteps(resources.integrator,&steps):CVodeGetNumSteps(resources.integrator,&steps),"step count",data);if(static_cast<std::size_t>(steps)>options.maxSteps)throw OdeError("O1006","Native ODE integration exceeded its total step budget.");retainDense(*out,resources,data,reached);
        const bool root=status==(resources.arkode?ARK_ROOT_RETURN:CV_ROOT_RETURN);bool terminal=false;
        if(root){std::vector<int> roots(options.eventCount);require(resources.arkode?ARKodeGetRootInfo(resources.integrator,roots.data()):CVodeGetRootInfo(resources.integrator,roots.data()),"event information",data);const auto* y=N_VGetArrayPointer(resources.state);for(std::size_t i=0;i<roots.size();++i)if(roots[i]){if(out->retainedCoordinateCount()+generated.coordinates+3>options.maxRetainedCoordinates)throw OdeError("O1006","ODE event storage exceeds the combined coordinate budget.");if(out->eventTimes.size()>=100000)throw OdeError("O1006","ODE event storage exceeds100000 crossings.");out->eventTimes.push_back(reached);out->eventIndices.push_back(static_cast<int>(i+1));out->eventDirections.push_back(options.t1<options.t0?-roots[i]:roots[i]);out->eventValues.insert(out->eventValues.end(),y,y+generated.coordinates);terminal=terminal||(!options.eventTerminal.empty()&&options.eventTerminal[i]!=0);}}
        while(next<requested.size()&&direction*(requested[next]-reached)<=0){appendSample(*out,resources,data,requested[next]);++next;}
        if(terminal){appendSample(*out,resources,data,reached);out->stats.terminalEvent=true;if(!out->denseSegments.empty())out->denseSegments.back().last=reached;break;}
        if(reached==previous&&!root)throw OdeError("O1007","Native ODE solver made no time progress.");
    }
    finalStatistics(*out,resources,data);detail::checkCancelled(native);return out;
}
}
std::shared_ptr<const Solution> solve(const language::OdeDefinition& definition,double gamma,const std::vector<double>& initial,const std::vector<double>& parameters,const SolveOptions& options,const NativeOptions& native){
    detail::checkCancelled(native);const auto generated=detail::generate(definition);validateValues(generated,gamma,options.t0,initial,parameters);const auto requested=sampleTimes(options,generated.coordinates,generated.parameters);const auto model=detail::compile(generated,native);
    return solvePrepared(definition,generated,model.get(),gamma,initial,parameters,options,native,requested);
}
struct PreparedModel::State {
    language::OdeDefinition definition;
    detail::Generated generated;
    std::unique_ptr<detail::Model> model;
    State(const language::OdeDefinition& source,const NativeOptions& options)
        :definition(source),generated(detail::generate(source)),model(detail::compile(generated,options)){}
};
PreparedModel::PreparedModel(const language::OdeDefinition& definition,const NativeOptions& options)
    :state_(std::make_shared<State>(definition,options)){detail::checkCancelled(options);}
std::size_t PreparedModel::coordinateCount()const noexcept{return state_->generated.coordinates;}
std::size_t PreparedModel::parameterCount()const noexcept{return state_->generated.parameters;}
const std::string& PreparedModel::key()const noexcept{return state_->model->key;}
bool PreparedModel::cacheHit()const noexcept{return state_->model->cacheHit;}
void PreparedModel::evaluateValues(double gamma,double time,std::span<const double> state,std::span<const double> parameters,std::span<double> rhs,const NativeOptions& options)const{
    evaluateJacobian(gamma,time,state,parameters,rhs,{},options);
}
void PreparedModel::evaluateJacobian(double gamma,double time,std::span<const double> state,std::span<const double> parameters,std::span<double> rhs,std::span<double> jacobian,const NativeOptions& options)const{
    detail::checkCancelled(options);const auto n=coordinateCount();
    if(!std::isfinite(gamma)||gamma<0||gamma>1||!std::isfinite(time)||state.size()!=n||parameters.size()!=parameterCount()||rhs.size()!=n||(!jacobian.empty()&&jacobian.size()!=n*n))throw OdeError("O1001","Prepared native span input/output layout is invalid.",state_->definition.span);
    for(const auto values:{state,parameters})for(double value:values)if(!std::isfinite(value))throw OdeError("O1001","Prepared native span inputs must be finite.",state_->definition.span);
    if(options.chargeOperations)options.chargeOperations(jacobian.empty()?n:n*(n+1));
    std::array<char,1024> error{};
    if(state_->model->evaluate(time,gamma,state.data(),parameters.data(),rhs.data(),jacobian.empty()?nullptr:jacobian.data(),error.data(),error.size()))throw modelFailure(state_->definition,error.data());
    detail::checkCancelled(options);
}
NativeEvaluation PreparedModel::evaluate(double gamma,double time,const std::vector<double>& state,const std::vector<double>& parameters,const NativeOptions& options)const{
    detail::checkCancelled(options);validateValues(state_->generated,gamma,time,state,parameters);
    const auto n=coordinateCount();if(options.chargeOperations)options.chargeOperations(n*(n+1));
    NativeEvaluation result;result.rhs.resize(n);result.jacobian.resize(n*n);result.cacheHit=cacheHit();result.key=key();std::array<char,1024> error{};
    if(state_->model->evaluate(time,gamma,state.data(),parameters.data(),result.rhs.data(),result.jacobian.data(),error.data(),error.size()))throw modelFailure(state_->definition,error.data());
    detail::checkCancelled(options);return result;
}
std::shared_ptr<const Solution> PreparedModel::solve(double gamma,const std::vector<double>& initial,const std::vector<double>& parameters,const SolveOptions& options,const NativeOptions& native)const{
    detail::checkCancelled(native);validateValues(state_->generated,gamma,options.t0,initial,parameters);
    const auto requested=sampleTimes(options,coordinateCount(),parameterCount());
    return solvePrepared(state_->definition,state_->generated,state_->model.get(),gamma,initial,parameters,options,native,requested);
}
std::vector<double> denseEvaluate(const Solution& solution,double time){
    if(!std::isfinite(time)||solution.times.empty())throw OdeError("O1011","Dense evaluation needs a finite time and a nonempty solution.");
    const auto n=solution.coordinateCount();const double direction=solution.times.back()>=solution.times.front()?1:-1;
    if(direction*(time-solution.times.front())<0||direction*(solution.times.back()-time)<0)throw OdeError("O1011","Dense query is outside the retained solution interval.");
    if(time==solution.times.front())return std::vector<double>(solution.values.begin(),solution.values.begin()+static_cast<std::ptrdiff_t>(n));
    const auto found=std::lower_bound(solution.denseSegments.begin(),solution.denseSegments.end(),time,[direction](const DenseSegment& segment,double t){return direction*(segment.last-t)<0;});
    if(found==solution.denseSegments.end()||direction*(time-found->first)<-16*std::numeric_limits<double>::epsilon()*std::max(1.0,std::abs(time))||found->coefficients.size()!=(found->degree+1)*n)throw OdeError("O1011","This solution does not retain a native integrator polynomial at the requested time.");
    std::vector<double> result(n,0);const double x=time-found->center;for(std::size_t k=found->degree+1;k>0;--k)for(std::size_t j=0;j<n;++j)result[j]=result[j]*x+found->coefficients[(k-1)*n+j];for(double value:result)if(!std::isfinite(value))throw OdeError("O1011","Dense interpolant produced a nonfinite value.");return result;
}
namespace {
int runningQuadrature(sunrealtype time,N_Vector y,N_Vector out,void* raw)noexcept{
    auto& d=*static_cast<UserData*>(raw);if(d.stop())return -1;try{const auto value=(*d.running)(time,N_VGetArrayPointer(y),d.parameters->data(),nullptr,nullptr);if(!std::isfinite(value))throw OdeError("O1012","Nonfinite native running objective.");N_VGetArrayPointer(out)[0]=value;return 0;}catch(const std::exception&e){d.evaluationFailed=true;d.failure=std::current_exception();try{d.error=e.what();}catch(...){}return -1;}catch(...){return -1;}
}
int backwardRhs(sunrealtype time,N_Vector y,N_Vector lambda,N_Vector out,void* raw)noexcept{
    auto& d=*static_cast<UserData*>(raw);if(d.stop())return -1;try{const auto n=d.model->coordinates;const int status=d.evaluate(time,N_VGetArrayPointer(y),d.rhs.data(),d.jacobian.data());if(status)return status;if(d.running){std::fill(d.runningStateGradient.begin(),d.runningStateGradient.end(),std::numeric_limits<double>::quiet_NaN());if(!std::isfinite((*d.running)(time,N_VGetArrayPointer(y),d.parameters->data(),d.runningStateGradient.data(),nullptr)))throw OdeError("O1012","Nonfinite native running objective.");}const auto* l=N_VGetArrayPointer(lambda);auto* f=N_VGetArrayPointer(out);for(std::size_t j=0;j<n;++j){f[j]=d.running?-d.runningStateGradient[j]:0;for(std::size_t k=0;k<n;++k)f[j]-=d.jacobian[k*n+j]*l[k];if(!std::isfinite(f[j]))throw OdeError("O1012","Nonfinite adjoint derivative.");}return 0;}catch(const std::exception& e){d.evaluationFailed=true;d.failure=std::current_exception();try{d.error=e.what();}catch(...){}return -1;}catch(...){return -1;}
}
int backwardQuadrature(sunrealtype time,N_Vector y,N_Vector lambda,N_Vector out,void* raw)noexcept{
    auto& d=*static_cast<UserData*>(raw);if(d.stop())return -1;try{const auto n=d.model->coordinates,np=d.model->parameters;d.settings->parameterJacobian(time,N_VGetArrayPointer(y),d.parameterJacobian.data());d.applyMass(d.parameterJacobian.data(),np);if(d.running){std::fill(d.runningParameterGradient.begin(),d.runningParameterGradient.end(),std::numeric_limits<double>::quiet_NaN());if(!std::isfinite((*d.running)(time,N_VGetArrayPointer(y),d.parameters->data(),nullptr,d.runningParameterGradient.data())))throw OdeError("O1012","Nonfinite native running objective.");}const auto* l=N_VGetArrayPointer(lambda);auto* q=N_VGetArrayPointer(out);for(std::size_t p=0;p<np;++p){q[p]=d.running?-d.runningParameterGradient[p]:0;for(std::size_t k=0;k<n;++k)q[p]-=d.parameterJacobian[k*np+p]*l[k];if(!std::isfinite(q[p]))throw OdeError("O1012","Nonfinite adjoint parameter quadrature.");}return 0;}catch(const std::exception& e){d.evaluationFailed=true;d.failure=std::current_exception();try{d.error=e.what();}catch(...){}return -1;}catch(...){return -1;}
}
#include "ark_adjoint.inc"
}
AdjointResult solveAdjoint(const language::OdeDefinition& definition,double gamma,const std::vector<double>& initial,const std::vector<double>& parameters,const TerminalObjective& objective,const SolveOptions& options,const NativeOptions& native,const RunningObjective& running){
    if(options.eventCount||!options.sensitivityParameters.empty())throw OdeError("O1012","Adjoint terminal objectives currently require a smooth fixed terminal time, without events or simultaneous forward sensitivity requests.");
    if(!objective||(!parameters.empty()&&!options.parameterJacobian))throw OdeError("O1012","Adjoint objectives and parameter partials must have compiled native evaluators.");
    if(options.method==Method::Erk||options.method==Method::Esdirk)return solveArkAdjoint(definition,gamma,initial,parameters,objective,options,native,running);
    detail::checkCancelled(native);const auto generated=detail::generate(definition);validateValues(generated,gamma,options.t0,initial,parameters);const auto requested=sampleTimes(options,generated.coordinates,generated.parameters);const auto model=detail::compile(generated,native);const auto n=generated.coordinates,np=generated.parameters;
    auto solution=std::make_shared<Solution>();solution->stats.cacheHit=model->cacheHit;solution->stats.key=model->key;solution->stats.method=options.method==Method::Bdf?"bdf":"adams";solution->stats.jacobian=options.jacobian;for(const auto& state:definition.states){solution->stateNames.push_back(state.name);solution->stateWidths.push_back(language::coordinateWidth(state.type,state.schema));}
    AdjointResult result;UserData data;data.definition=&definition;data.model=model.get();data.options=&native;data.settings=&options;data.parameters=&parameters;data.gamma=gamma;data.wallLimit=options.maximumWallSeconds;data.start=Clock::now();data.rhs.resize(n);data.jacobian.resize(n*n);data.parameterJacobian.resize(n*np);initializeMass(data);
    result.initialGradient.resize(n);result.parameterGradient.resize(np);if(running){data.running=&running;data.runningStateGradient.resize(n);data.runningParameterGradient.resize(np);}
    if(options.t0==options.t1){result.objective=objective(options.t1,initial.data(),parameters.data(),result.initialGradient.data(),result.parameterGradient.data());solution->times={options.t0};solution->values=initial;if(data.evaluate(options.t0,initial.data(),data.rhs.data(),nullptr))throw modelFailure(definition,data.error);solution->derivatives=data.rhs;result.solution=solution;return result;}
    Resources resources;setup(resources,data,options,initial);if(running){resources.cost=N_VNew_Serial(1,resources.context);if(!resources.cost)throw OdeError("O1007","Cannot allocate native running-cost quadrature.");N_VConst(0,resources.cost);require(CVodeQuadInit(resources.integrator,runningQuadrature,resources.cost),"running-cost quadrature initialization",data);require(CVodeQuadSStolerances(resources.integrator,options.rtol,options.atol),"running-cost quadrature tolerances",data);require(CVodeSetQuadErrCon(resources.integrator,SUNTRUE),"running-cost quadrature error control",data);}require(CVodeAdjInit(resources.integrator,100,CV_HERMITE),"adjoint checkpoint initialization",data);appendSample(*solution,resources,data,options.t0,true);const double direction=options.t1>options.t0?1:-1;double reached=options.t0;std::size_t next=1;int checkpoints=0;long steps=0;
    while(direction*(options.t1-reached)>0){detail::checkCancelled(native);if(data.stop())checkStatus(-1,data,definition.span);if(static_cast<std::size_t>(steps)>=options.maxSteps)throw OdeError("O1006","Adjoint forward solve exceeded its step budget.");sunrealtype current=0;const int status=CVodeF(resources.integrator,options.t1,resources.state,&current,CV_ONE_STEP,&checkpoints);checkStatus(status,data,definition.span);reached=current;require(CVodeGetNumSteps(resources.integrator,&steps),"adjoint forward steps",data);retainDense(*solution,resources,data,reached);while(next<requested.size()&&direction*(requested[next]-reached)<=0){appendSample(*solution,resources,data,requested[next]);++next;}}
    finalStatistics(*solution,resources,data);const auto* finalState=solution->values.data()+solution->values.size()-n;result.objective=objective(options.t1,finalState,parameters.data(),result.initialGradient.data(),result.parameterGradient.data());if(!std::isfinite(result.objective))throw OdeError("O1012","Native terminal objective is nonfinite.");for(double v:result.initialGradient)if(!std::isfinite(v))throw OdeError("O1012","Native terminal objective state gradient is nonfinite.");for(double v:result.parameterGradient)if(!std::isfinite(v))throw OdeError("O1012","Native terminal objective parameter gradient is nonfinite.");
    if(running){require(CVodeGetQuadDky(resources.integrator,options.t1,0,resources.cost),"running-cost integral",data);result.runningCost=N_VGetArrayPointer(resources.cost)[0];result.objective+=result.runningCost;if(!std::isfinite(result.objective))throw OdeError("O1012","Nonfinite total terminal/running objective.");}
    // These user workspaces outlive CVODES' backward problem. CVODES owns only
    // its internal copies; free them after explicit integrator destruction.
    N_Vector lambda=N_VClone(resources.state),quadrature=nullptr;SUNMatrix matrix=nullptr;SUNLinearSolver linear=nullptr;
    try{if(!lambda)throw OdeError("O1007","Cannot allocate adjoint state.");std::copy(result.initialGradient.begin(),result.initialGradient.end(),N_VGetArrayPointer(lambda));int which=-1;require(CVodeCreateB(resources.integrator,options.method==Method::Bdf?CV_BDF:CV_ADAMS,&which),"backward allocation",data);require(CVodeInitB(resources.integrator,which,backwardRhs,options.t1,lambda),"backward initialization",data);require(CVodeSStolerancesB(resources.integrator,which,options.rtol,options.atol),"backward tolerances",data);require(CVodeSetUserDataB(resources.integrator,which,&data),"backward user data",data);require(CVodeSetMaxNumStepsB(resources.integrator,which,static_cast<long>(options.maxSteps)),"backward step budget",data);
        matrix=SUNDenseMatrix(static_cast<sunindextype>(n),static_cast<sunindextype>(n),resources.context);if(!matrix)throw OdeError("O1007","Cannot allocate adjoint matrix.");linear=SUNLinSol_Dense(lambda,matrix,resources.context);if(!linear)throw OdeError("O1007","Cannot allocate adjoint linear solver.");require(CVodeSetLinearSolverB(resources.integrator,which,linear,matrix),"backward linear solver",data);
        if(np){quadrature=N_VNew_Serial(static_cast<sunindextype>(np),resources.context);if(!quadrature)throw OdeError("O1007","Cannot allocate adjoint parameter quadrature.");N_VConst(0,quadrature);require(CVodeQuadInitB(resources.integrator,which,backwardQuadrature,quadrature),"adjoint quadrature initialization",data);require(CVodeQuadSStolerancesB(resources.integrator,which,options.rtol,options.atol),"adjoint quadrature tolerances",data);require(CVodeSetQuadErrConB(resources.integrator,which,SUNTRUE),"adjoint quadrature error control",data);}
        sunrealtype time=options.t1;long previousSteps=0;std::size_t totalSteps=0;
        while(direction*(time-options.t0)>0){const double previousTime=time;const int status=CVodeB(resources.integrator,options.t0,CV_ONE_STEP);checkStatus(status,data,definition.span);require(CVodeGetB(resources.integrator,which,&time,lambda),"adjoint initial gradient",data);long stepsNow=0;require(CVodeGetNumSteps(CVodeGetAdjCVodeBmem(resources.integrator,which),&stepsNow),"backward steps",data);const auto increment=stepsNow>=previousSteps?stepsNow-previousSteps:stepsNow;totalSteps+=static_cast<std::size_t>(std::max(0L,increment));previousSteps=stepsNow;if(totalSteps>options.maxSteps)throw OdeError("O1006","Adjoint backward integration exceeded its total step budget.");if(time==previousTime)throw OdeError("O1007","Adjoint backward integration made no time progress.");}
        auto* backwardMemory=CVodeGetAdjCVodeBmem(resources.integrator,which);require(CVodeGetDky(backwardMemory,options.t0,0,lambda),"adjoint endpoint interpolation",data);std::copy_n(N_VGetArrayPointer(lambda),n,result.initialGradient.begin());if(np){require(CVodeGetQuadDky(backwardMemory,options.t0,0,quadrature),"adjoint parameter gradient",data);for(std::size_t p=0;p<np;++p)result.parameterGradient[p]+=N_VGetArrayPointer(quadrature)[p];}
        result.backwardSteps=totalSteps;result.checkpoints=static_cast<std::size_t>(std::max(0,checkpoints));CVodeFree(&resources.integrator);
    }catch(...){if(resources.integrator)CVodeFree(&resources.integrator);if(linear)SUNLinSolFree(linear);if(matrix)SUNMatDestroy(matrix);if(quadrature)N_VDestroy(quadrature);if(lambda)N_VDestroy(lambda);throw;}
    if(linear)SUNLinSolFree(linear);
    if(matrix)SUNMatDestroy(matrix);
    if(quadrature)N_VDestroy(quadrature);
    if(lambda)N_VDestroy(lambda);
    for(double x:result.initialGradient)if(!std::isfinite(x))throw OdeError("O1012","Adjoint initial gradient is nonfinite.");
    for(double x:result.parameterGradient)if(!std::isfinite(x))throw OdeError("O1012","Adjoint parameter gradient is nonfinite.");
    solution->stats.elapsedSeconds=std::chrono::duration<double>(Clock::now()-data.start).count();result.solution=std::move(solution);detail::checkCancelled(native);return result;
}

} // namespace penta::ode
