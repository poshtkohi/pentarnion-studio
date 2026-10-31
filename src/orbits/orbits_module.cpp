#include "orbits_module.hpp"
#include "modules/orbits_api.h"
#include "native_functions/native_callback.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <exception>
#include <map>
#include <mutex>
#include <numeric>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#include <unistd.h>
#endif

namespace penta::orbits {
namespace {
using Clock=std::chrono::steady_clock;
void text(char* out,uint64_t capacity,const char* message){if(out&&capacity){const auto count=std::min<uint64_t>(capacity-1,std::strlen(message));std::memcpy(out,message,count);out[count]=0;}}
struct Loaded {void* handle=nullptr;const penta_orbits_api_v1* api=nullptr;};
std::mutex& mutex(){static std::mutex value;return value;}
std::map<std::filesystem::path,Loaded>& registry(){static std::map<std::filesystem::path,Loaded> value;return value;}
std::filesystem::path executableDirectory(){
#ifdef _WIN32
    std::vector<wchar_t> buffer(32768);const auto count=GetModuleFileNameW(nullptr,buffer.data(),static_cast<DWORD>(buffer.size()));if(!count||count>=buffer.size())throw Error(4,"Cannot locate orbit module directory.");return std::filesystem::path(std::wstring(buffer.data(),count)).parent_path();
#else
    std::vector<char> buffer(32768);const auto count=readlink("/proc/self/exe",buffer.data(),buffer.size());if(count<=0||static_cast<std::size_t>(count)>=buffer.size())throw Error(4,"Cannot locate orbit module directory.");return std::filesystem::path(std::string(buffer.data(),static_cast<std::size_t>(count))).parent_path();
#endif
}
const penta_orbits_api_v1& api(std::filesystem::path directory){
    if(directory.empty())directory=executableDirectory()/"modules";
    if(!directory.is_absolute())throw Error(1,"Orbit module directory must be absolute and host-controlled.");
#ifdef _WIN32
    auto path=(directory/"penta_orbits.dll").lexically_normal();
#else
    auto path=(directory/"libpenta_orbits.so").lexically_normal();
#endif
    std::lock_guard lock(mutex());if(const auto found=registry().find(path);found!=registry().end())return *found->second.api;Loaded loaded;
#ifdef _WIN32
    loaded.handle=LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);if(!loaded.handle)throw Error(4,"Cannot load optional penta_orbits module/dependencies (Windows error "+std::to_string(GetLastError())+").");const auto query=reinterpret_cast<penta_orbits_query_function_v1>(reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(loaded.handle),"penta_orbits_query_v1")));auto release=[&]{FreeLibrary(static_cast<HMODULE>(loaded.handle));};
#else
    loaded.handle=dlopen(path.c_str(),RTLD_NOW|RTLD_LOCAL);if(!loaded.handle)throw Error(4,"Cannot load optional penta_orbits module/dependencies.");const auto query=reinterpret_cast<penta_orbits_query_function_v1>(dlsym(loaded.handle,"penta_orbits_query_v1"));auto release=[&]{dlclose(loaded.handle);};
#endif
    if(!query){release();throw Error(4,"Orbit module query export is missing.");}loaded.api=query(PENTA_ORBITS_ABI_V1);
    if(!loaded.api||loaded.api->struct_size<sizeof(penta_orbits_api_v1)||loaded.api->abi_version!=PENTA_ORBITS_ABI_V1||(loaded.api->capabilities&7)!=7||!loaded.api->periodic||!loaded.api->continuation||!loaded.api->spectrum){release();throw Error(4,"Orbit module ABI/capabilities are incompatible.");}
    return *registry().emplace(path,loaded).first->second.api;
}
struct Host {
    const ode::NativeOptions& native;std::exception_ptr failure;
    penta_orbits_host_v1 get(){penta_orbits_host_v1 h{};h.struct_size=sizeof(h);h.abi_version=PENTA_ORBITS_ABI_V1;h.context=this;h.cancelled=[](void* raw){const auto& h=*static_cast<Host*>(raw);return h.native.cancellation&&h.native.cancellation->load(std::memory_order_relaxed)?1:0;};h.charge=[](void* raw,uint64_t count,char* error,uint64_t cap)noexcept{auto& h=*static_cast<Host*>(raw);try{if(h.native.chargeOperations)h.native.chargeOperations(static_cast<std::size_t>(count));return 0;}catch(const std::exception& e){h.failure=std::current_exception();text(error,cap,e.what());return 1;}catch(...){h.failure=std::current_exception();return 1;}};return h;}
};
void finiteInputs(const ode::PreparedModel& model,const std::vector<double>& state,const std::vector<double>& parameters){if(state.size()!=model.coordinateCount()||parameters.size()!=model.parameterCount()||state.empty()||state.size()>32)throw Error(1,"Orbit analysis requires complete 1..32 real state coordinates and every declared parameter.");for(const auto& input:{state,parameters})for(const double x:input)if(!std::isfinite(x))throw Error(1,"Orbit inputs must be finite.");}
native_functions::Callback rhsCallback(const language::OdeDefinition& model,const ode::NativeOptions& native){
    language::FunctionDefinition function;function.name="orbit_rhs_"+model.name;function.native=true;function.span=model.span;function.parameters=model.states;function.parameters.insert(function.parameters.end(),model.parameters.begin(),model.parameters.end());function.parameters.push_back({model.timeName,language::Type::Real,model.span});
    language::Statement returned;returned.kind=language::StatementKind::Return;returned.span=model.span;
    if(model.states.size()==1){function.result=model.states[0].type;returned.expression=model.derivatives[0];}else{function.result=language::Type::Tuple;for(const auto& state:model.states)function.results.push_back(state.type);auto tuple=std::make_shared<language::Expr>();tuple->kind=language::ExprKind::TupleLiteral;tuple->type=language::Type::Tuple;tuple->span=model.span;tuple->children=model.derivatives;returned.expression=std::move(tuple);}function.body={returned};
    native_functions::Options options;options.build=native;options.chargeOperations=native.chargeOperations;return native_functions::Callback(function,options);
}
struct Flow {
    const ode::PreparedModel& model;double gamma;std::vector<double> parameters;ode::NativeOptions native;ode::SolveOptions solver;
    Clock::time_point start;double seconds;std::size_t steps=0,rhs=0;std::exception_ptr failure;
    std::shared_ptr<const ode::Solution> integrate(const std::vector<double>& state,double period,bool tangent){
        auto settings=solver;settings.t1=settings.t0+period;settings.sampleStep=period;settings.outputTimes={settings.t0,settings.t1};settings.denseOutput=false;
        settings.maximumWallSeconds=seconds-std::chrono::duration<double>(Clock::now()-start).count();if(settings.maximumWallSeconds<=0)throw Error(2,"Orbit analysis wall-time budget reached.");
        const auto n=state.size();settings.sensitivityParameters.clear();settings.sensitivityInitial.clear();
        if(tangent){settings.sensitivityParameters.assign(n,-1);settings.sensitivityInitial.assign(n*n,0);for(std::size_t k=0;k<n;++k)settings.sensitivityInitial[k*n+k]=1;}
        auto result=model.solve(gamma,state,parameters,settings,native);steps+=result->stats.steps;rhs+=result->stats.rhsEvaluations;return result;
    }
    static int evaluate(void* raw,const double* state,double period,int tangent,double* endpoint,double* monodromy,double* finalRhs,char* error,uint64_t cap)noexcept{
        auto& b=*static_cast<Flow*>(raw);try{const auto n=b.model.coordinateCount();const auto solution=b.integrate(std::vector<double>(state,state+n),period,tangent!=0);if(solution->times.size()!=2||solution->values.size()!=2*n)throw Error(2,"Unexpected shooting output layout.");std::copy_n(solution->values.data()+n,n,endpoint);std::copy_n(solution->derivatives.data()+n,n,finalRhs);if(tangent){if(solution->sensitivities.size()!=2*n*n)throw Error(2,"Unexpected monodromy sensitivity layout.");for(std::size_t row=0;row<n;++row)for(std::size_t col=0;col<n;++col)monodromy[row*n+col]=solution->sensitivities[n*n+col*n+row];}return 0;}catch(const std::exception& e){b.failure=std::current_exception();text(error,cap,e.what());return 1;}catch(...){b.failure=std::current_exception();return 1;}
    }
};
void spectrum(const penta_orbits_api_v1& module,const std::vector<double>& matrix,std::size_t n,std::vector<double>& real,std::vector<double>& imag){real.resize(n);imag.resize(n);penta_orbits_spectrum_v1 request{};request.struct_size=sizeof(request);request.dimension=n;request.matrix=matrix.data();request.real_parts=real.data();request.imaginary_parts=imag.data();char error[1024]{};if(module.spectrum(&request,error,sizeof(error)))throw Error(2,error);}
void orbitalStability(PeriodicResult& result,double tolerance){
    const auto n=result.multipliersReal.size();result.multipliersModulus.resize(n);double neutral=1e300;
    for(std::size_t k=0;k<n;++k){const double distance=std::hypot(result.multipliersReal[k]-1,result.multipliersImag[k]);if(distance<neutral){neutral=distance;result.neutralIndex=k;}result.multipliersModulus[k]=std::hypot(result.multipliersReal[k],result.multipliersImag[k]);}
    result.neutralError=neutral;bool boundary=false;result.unstable=false;
    for(std::size_t k=0;k<n;++k)if(k!=result.neutralIndex){result.unstable=result.unstable||result.multipliersModulus[k]>1+tolerance;boundary=boundary||std::abs(result.multipliersModulus[k]-1)<=tolerance;}
    result.stable=result.converged&&neutral<=tolerance&&!boundary&&!result.unstable;result.unstable=result.converged&&neutral<=tolerance&&result.unstable;result.inconclusive=!result.stable&&!result.unstable;
}
void checkAutonomous(const language::OdeDefinition& model){if(!autonomous(model))throw Error(1,"Periodic shooting/continuation currently requires an autonomous ODE; time-dependent forcing needs a separate fixed-period formulation.");}
void checkPeriodicOptions(const PeriodicOptions& o){
    for(const double x:{o.period,o.minPeriod,o.maxPeriod,o.tolerance,o.stabilityTolerance,o.maximumSeconds,o.sampleStep,o.periodScale})if(!std::isfinite(x))throw Error(1,"Periodic analysis settings must be finite.");
    if(o.maximumIterations<1||o.maximumIterations>100||o.period<=0||o.minPeriod<=0||o.maxPeriod<=o.minPeriod||o.period<o.minPeriod||o.period>o.maxPeriod||o.tolerance<=0||o.stabilityTolerance<=0||o.maximumSeconds<=0||o.sampleStep<0||o.periodScale<=0)throw Error(1,"Invalid periodic bounds, iteration/sample or tolerance budget.");
}
void checkContinuationOptions(const ContinuationOptions& o){
    for(const double x:{o.step,o.minimumStep,o.maximumStep,o.minimumParameter,o.maximumParameter,o.tolerance,o.stabilityTolerance,o.maximumSeconds})if(!std::isfinite(x))throw Error(1,"Continuation settings must be finite.");
    if(o.points<2||o.points>1000||o.maximumIterations<1||o.maximumIterations>100||o.step==0||o.minimumStep<=0||o.maximumStep<o.minimumStep||o.minimumParameter>=o.maximumParameter||o.tolerance<=0||o.stabilityTolerance<=0||o.maximumSeconds<=0)throw Error(1,"Invalid continuation dimensions, bounds or budget.");
}
}

bool autonomous(const language::OdeDefinition& model){
    std::function<bool(const language::ExprPtr&)> dependent=[&](const auto& expression){if(!expression)return false;if(expression->kind==language::ExprKind::Reference&&expression->text==model.timeName)return true;return std::any_of(expression->children.begin(),expression->children.end(),dependent);};
    return std::none_of(model.derivatives.begin(),model.derivatives.end(),dependent);
}

PeriodicResult periodicOrbit(const language::OdeDefinition& definition,double gamma,const std::vector<double>& initial,const std::vector<double>& parameters,const PeriodicOptions& settings,const ode::NativeOptions& native,std::filesystem::path directory){
    checkAutonomous(definition);checkPeriodicOptions(settings);ode::PreparedModel model(definition,native);finiteInputs(model,initial,parameters);const auto n=initial.size();if(n<2)throw Error(1,"A nontrivial autonomous periodic orbit requires at least two real phase coordinates.");
    if(settings.solver.method!=ode::Method::Bdf&&settings.solver.method!=ode::Method::Adams)throw Error(1,"Analytic monodromy currently uses CVODES BDF or Adams sensitivities.");
    const auto module=api(directory);PeriodicResult result;result.state.resize(n);result.monodromy.resize(n*n);result.history.resize((settings.maximumIterations+1)*6);result.cacheHit=model.cacheHit();result.key=model.key();
    const auto reference=settings.phaseReference.empty()?initial:settings.phaseReference;auto direction=settings.phaseDirection;const auto scales=settings.stateScale.empty()?std::vector<double>(n,1):settings.stateScale;if(scales.size()!=n)throw Error(1,"state_scale must contain every real phase coordinate.");for(const double scale:scales)if(!std::isfinite(scale)||scale<=0)throw Error(1,"state_scale entries must be finite and positive.");
    if(reference.size()!=n)throw Error(1,"Phase reference must contain every real phase coordinate.");
    if(direction.empty())direction=model.evaluate(gamma,settings.solver.t0,reference,parameters,native).rhs;
    if(direction.size()!=n)throw Error(1,"Phase direction must contain every real phase coordinate.");
    Host host{native,{}};auto nativeHost=host.get();Flow flow{model,gamma,parameters,native,settings.solver,Clock::now(),settings.maximumSeconds,0,0,{}};
    penta_orbits_periodic_v1 request{};request.struct_size=sizeof(request);request.dimension=n;request.max_iterations=settings.maximumIterations;request.history_capacity=settings.maximumIterations+1;request.initial=initial.data();request.phase_reference=reference.data();request.phase_direction=direction.data();request.state_scale=scales.data();request.period_scale=settings.periodScale;request.period=settings.period;request.min_period=settings.minPeriod;request.max_period=settings.maxPeriod;request.tolerance=settings.tolerance;request.max_seconds=settings.maximumSeconds;request.context=&flow;request.flow=Flow::evaluate;request.state=result.state.data();request.monodromy=result.monodromy.data();request.history=result.history.data();
    char error[1024]{};const int status=module.periodic(&nativeHost,&request,error,sizeof(error));if(status){if(host.failure)std::rethrow_exception(host.failure);if(flow.failure)std::rethrow_exception(flow.failure);throw Error(status,error);}
    result.period=request.final_period;result.closureResidual=request.closure_residual;result.scaledClosureResidual=request.scaled_closure_residual;result.phaseResidual=request.phase_residual;result.iterations=request.iterations;result.evaluations=request.evaluations;result.history.resize(request.history_rows*6);result.converged=request.converged!=0;result.status=request.status;result.message=request.message;
    auto output=settings.solver;output.t1=output.t0+result.period;output.outputTimes.clear();output.sampleStep=settings.sampleStep>0?settings.sampleStep:result.period/200;output.denseOutput=false;output.maximumWallSeconds=settings.maximumSeconds-std::chrono::duration<double>(Clock::now()-flow.start).count();if(output.maximumWallSeconds<=0)throw Error(2,"Orbit output exceeded analysis wall-time budget.");
    result.orbit=model.solve(gamma,result.state,parameters,output,native);result.steps=flow.steps+result.orbit->stats.steps;result.rhsEvaluations=flow.rhs+result.orbit->stats.rhsEvaluations;
    for(std::size_t row=0;row<result.orbit->times.size();++row){double distance=0,scaledDistance=0;for(std::size_t col=0;col<n;++col){const auto delta=result.orbit->values[row*n+col]-result.state[col];distance=std::hypot(distance,delta);scaledDistance=std::hypot(scaledDistance,delta/scales[col]);}result.amplitude=std::max(result.amplitude,distance);result.scaledAmplitude=std::max(result.scaledAmplitude,scaledDistance);}
    if(result.scaledAmplitude<=std::max(settings.tolerance*10,1e-8)){result.converged=false;result.status="equilibrium_candidate";result.message="The closed flow has negligible amplitude; a periodic orbit was not established.";}
    spectrum(module,result.monodromy,n,result.multipliersReal,result.multipliersImag);orbitalStability(result,settings.stabilityTolerance);
    if(result.message.empty())result.message="Shooting closure and Floquet diagnostics are numerical; this period is not certified minimal.";
    return result;
}

namespace {
struct BranchBridge {
    const ode::PreparedModel& prepared;native_functions::Callback& callback;double gamma;std::vector<double> parameters;std::size_t parameterIndex;ode::NativeOptions native;ode::SolveOptions solver;Clock::time_point start;double maximumSeconds;bool periodic=false;
    std::vector<double> phaseReference,phaseDirection;std::size_t steps=0;std::exception_ptr failure;
    void evaluate(const double* point,double parameter,double* residual,double* jacobian){
        const auto n=prepared.coordinateCount();parameters[parameterIndex]=parameter;
        std::vector<double> input(point,point+n);input.insert(input.end(),parameters.begin(),parameters.end());input.push_back(solver.t0);
        if(!periodic){native_functions::DerivativeRequest request;request.order=1;request.requestedThreads=1;request.active.resize(n);std::iota(request.active.begin(),request.active.end(),0);request.active.push_back(n+parameterIndex);const auto value=callback.evaluate(gamma,input,request);std::copy(value.values.begin(),value.values.end(),residual);std::copy(value.jacobian.begin(),value.jacobian.end(),jacobian);return;}
        const double period=point[n];if(!std::isfinite(period)||period<=0)throw Error(2,"Periodic continuation proposed a nonpositive period.");
        auto settings=solver;settings.t1=settings.t0+period;settings.sampleStep=period;settings.outputTimes={settings.t0,settings.t1};settings.denseOutput=false;settings.sensitivityParameters.assign(n,-1);settings.sensitivityParameters.push_back(static_cast<int>(parameterIndex));settings.sensitivityInitial.assign((n+1)*n,0);for(std::size_t k=0;k<n;++k)settings.sensitivityInitial[k*n+k]=1;
        settings.maximumWallSeconds=maximumSeconds-std::chrono::duration<double>(Clock::now()-start).count();if(settings.maximumWallSeconds<=0)throw Error(2,"Periodic continuation wall budget reached.");
        settings.parameterJacobian=[&,n](double time,const double* state,double* out){std::vector<double> inputs(state,state+n);inputs.insert(inputs.end(),parameters.begin(),parameters.end());inputs.push_back(time);native_functions::DerivativeRequest derivative;derivative.order=1;derivative.requestedThreads=1;derivative.active.resize(parameters.size());std::iota(derivative.active.begin(),derivative.active.end(),n);const auto value=callback.evaluate(gamma,inputs,derivative);std::copy(value.jacobian.begin(),value.jacobian.end(),out);};
        const auto solution=prepared.solve(gamma,std::vector<double>(point,point+n),parameters,settings,native);steps+=solution->stats.steps;const auto offset=(solution->times.size()-1)*(n+1)*n;
        for(std::size_t row=0;row<n;++row){residual[row]=solution->values[n+row]-point[row];for(std::size_t col=0;col<n;++col)jacobian[row*(n+2)+col]=solution->sensitivities[offset+col*n+row]-(row==col?1:0);jacobian[row*(n+2)+n]=solution->derivatives[n+row];jacobian[row*(n+2)+n+1]=solution->sensitivities[offset+n*n+row];}
        residual[n]=0;for(std::size_t col=0;col<n;++col){residual[n]+=phaseDirection[col]*(point[col]-phaseReference[col]);jacobian[n*(n+2)+col]=phaseDirection[col];}jacobian[n*(n+2)+n]=jacobian[n*(n+2)+n+1]=0;
    }
    static int call(void* raw,const double* point,double parameter,double* residual,double* jacobian,char* error,uint64_t capacity)noexcept{auto& b=*static_cast<BranchBridge*>(raw);try{b.evaluate(point,parameter,residual,jacobian);return 0;}catch(const std::exception& e){b.failure=std::current_exception();text(error,capacity,e.what());return 1;}catch(...){b.failure=std::current_exception();return 1;}}
};

ContinuationResult branch(const language::OdeDefinition& definition,double gamma,const std::vector<double>& initial,const std::vector<double>& parameters,const ContinuationOptions& settings,const ode::NativeOptions& native,std::filesystem::path directory,bool periodic){
    checkAutonomous(definition);checkContinuationOptions(settings);if(periodic)checkPeriodicOptions(settings.periodic);ode::PreparedModel prepared(definition,native);finiteInputs(prepared,initial,parameters);if(settings.parameterIndex>=parameters.size())throw Error(1,"Continuation requires one selected real parameter coordinate.");const auto n=initial.size(),dimension=n+(periodic?1u:0u);auto callback=rhsCallback(definition,native);const auto module=api(directory);const auto started=Clock::now();std::vector<double> seed=initial,phaseDirection,reference;
    ContinuationResult result;result.dimension=dimension;result.physicalDimension=n;result.kind=periodic?"periodic":"equilibrium";result.cacheHit=prepared.cacheHit()&&callback.cacheHit();result.key=prepared.key()+":"+callback.key();
    if(periodic){auto options=settings.periodic;options.maximumSeconds=settings.maximumSeconds;const auto orbit=periodicOrbit(definition,gamma,initial,parameters,options,native,directory);if(!orbit.converged)throw Error(2,"Periodic branch seed did not converge to a nontrivial closed orbit.");seed=orbit.state;seed.push_back(orbit.period);reference=orbit.state;phaseDirection=prepared.evaluate(gamma,options.solver.t0,reference,parameters,native).rhs;double length=0;for(const auto x:phaseDirection)length=std::hypot(length,x);if(length<1e-12)throw Error(2,"Periodic branch phase direction is degenerate.");for(auto& x:phaseDirection)x/=length;result.steps=orbit.steps;}
    result.states.resize(settings.points*dimension);result.parameters.resize(settings.points);result.tangents.resize(settings.points*(dimension+1));result.residuals.resize(settings.points);result.stepSizes.resize(settings.points);
    auto solver=settings.periodic.solver;BranchBridge bridge{prepared,callback,gamma,parameters,settings.parameterIndex,native,solver,started,settings.maximumSeconds,periodic,reference,phaseDirection,0,{}};Host host{native,{}};auto nativeHost=host.get();penta_orbits_continue_v1 request{};request.struct_size=sizeof(request);request.dimension=dimension;request.points=settings.points;request.max_iterations=settings.maximumIterations;request.initial=seed.data();request.parameter=parameters[settings.parameterIndex];request.step=settings.step;request.min_step=settings.minimumStep;request.max_step=settings.maximumStep;request.min_parameter=settings.minimumParameter;request.max_parameter=settings.maximumParameter;request.tolerance=settings.tolerance;request.max_seconds=settings.maximumSeconds;request.context=&bridge;request.residual=BranchBridge::call;request.states=result.states.data();request.parameters=result.parameters.data();request.tangents=result.tangents.data();request.residuals=result.residuals.data();request.step_sizes=result.stepSizes.data();request.row_capacity=settings.points;
    char error[1024]{};const int status=module.continuation(&nativeHost,&request,error,sizeof(error));if(status){if(host.failure)std::rethrow_exception(host.failure);if(bridge.failure)std::rethrow_exception(bridge.failure);throw Error(status,error);}
    result.rows=request.rows;result.completed=request.completed!=0;result.status=request.status;result.message=request.message;result.evaluations=request.evaluations;result.iterations=request.iterations;result.steps+=bridge.steps;
    result.states.resize(result.rows*dimension);result.parameters.resize(result.rows);result.tangents.resize(result.rows*(dimension+1));result.residuals.resize(result.rows);result.stepSizes.resize(result.rows);
    result.valuesReal.resize(result.rows*n);result.valuesImag.resize(result.rows*n);result.measure.resize(result.rows);result.stable.resize(result.rows);result.unstable.resize(result.rows);result.inconclusive.resize(result.rows);result.foldCandidate.assign(result.rows,0);result.hopfCandidate.assign(result.rows,0);result.periodDoublingCandidate.assign(result.rows,0);result.stabilityChange.assign(result.rows,0);if(periodic){result.periods.resize(result.rows);result.closureResiduals.resize(result.rows);result.neutralErrors.resize(result.rows);}
    for(std::size_t row=0;row<result.rows;++row){
        std::vector<double> point(result.states.begin()+static_cast<std::ptrdiff_t>(row*dimension),result.states.begin()+static_cast<std::ptrdiff_t>(row*dimension+n));auto params=parameters;params[settings.parameterIndex]=result.parameters[row];std::vector<double> real,imag;
        if(!periodic){const auto evaluation=prepared.evaluate(gamma,solver.t0,point,params,native);spectrum(module,evaluation.jacobian,n,real,imag);double largest=-1e300;for(const auto x:real)largest=std::max(largest,x);result.measure[row]=largest;result.stable[row]=largest< -settings.stabilityTolerance;result.unstable[row]=largest>settings.stabilityTolerance;result.inconclusive[row]=!result.stable[row]&&!result.unstable[row];}
        else{const double remaining=settings.maximumSeconds-std::chrono::duration<double>(Clock::now()-started).count();if(remaining<=0)throw Error(2,"Periodic branch diagnostics exhausted wall budget.");Flow flow{prepared,gamma,params,native,solver,Clock::now(),remaining,0,0,{}};const double period=result.states[row*dimension+n];const auto solution=flow.integrate(point,period,true);std::vector<double> matrix(n*n);for(std::size_t i=0;i<n;++i)for(std::size_t j=0;j<n;++j)matrix[i*n+j]=solution->sensitivities[n*n+j*n+i];PeriodicResult stability;stability.converged=true;spectrum(module,matrix,n,stability.multipliersReal,stability.multipliersImag);orbitalStability(stability,settings.stabilityTolerance);real=stability.multipliersReal;imag=stability.multipliersImag;result.stable[row]=stability.stable;result.unstable[row]=stability.unstable;result.inconclusive[row]=stability.inconclusive;result.neutralErrors[row]=stability.neutralError;double radius=0;for(std::size_t k=0;k<n;++k)if(k!=stability.neutralIndex)radius=std::max(radius,stability.multipliersModulus[k]);result.measure[row]=radius;result.periods[row]=period;double closure=0;for(std::size_t k=0;k<n;++k)closure=std::max(closure,std::abs(solution->values[n+k]-point[k]));result.closureResiduals[row]=closure;result.steps+=flow.steps;}
        std::copy(real.begin(),real.end(),result.valuesReal.begin()+static_cast<std::ptrdiff_t>(row*n));std::copy(imag.begin(),imag.end(),result.valuesImag.begin()+static_cast<std::ptrdiff_t>(row*n));
        if(row){
            const double prev=result.tangents[(row-1)*(dimension+1)+dimension],now=result.tangents[row*(dimension+1)+dimension];result.foldCandidate[row]=prev*now<0;result.stabilityChange[row]=result.stable[row]!=result.stable[row-1]||result.unstable[row]!=result.unstable[row-1];
            std::vector<bool> matched(n,false);
            for(std::size_t k=0;k<n;++k){std::size_t nearest=0;double distance=1e300;for(std::size_t old=0;old<n;++old)if(!matched[old]){const auto offset=(row-1)*n+old;const double gap=std::hypot(real[k]-result.valuesReal[offset],imag[k]-result.valuesImag[offset]);if(gap<distance){distance=gap;nearest=old;}}matched[nearest]=true;const auto offset=(row-1)*n+nearest;
                if(!periodic&&std::abs(imag[k])>settings.stabilityTolerance&&std::abs(result.valuesImag[offset])>settings.stabilityTolerance&&real[k]*result.valuesReal[offset]<0)result.hopfCandidate[row]=1;
                if(periodic&&std::abs(imag[k])<=settings.stabilityTolerance&&std::abs(result.valuesImag[offset])<=settings.stabilityTolerance&&(real[k]+1)*(result.valuesReal[offset]+1)<0)result.periodDoublingCandidate[row]=1;
            }
        }
    }
    if(result.message.empty())result.message=periodic?"Pseudo-arclength periodic branch uses a fixed phase section. Bifurcation flags are candidates, not refined/certified bifurcations.":"Pseudo-arclength equilibrium branch. Fold/Hopf flags are sampled candidates, not refined/certified bifurcations.";
    return result;
}
}
ContinuationResult continueEquilibrium(const language::OdeDefinition& model,double gamma,const std::vector<double>& initial,const std::vector<double>& parameters,const ContinuationOptions& options,const ode::NativeOptions& native,std::filesystem::path directory){return branch(model,gamma,initial,parameters,options,native,std::move(directory),false);}
ContinuationResult continuePeriodic(const language::OdeDefinition& model,double gamma,const std::vector<double>& initial,const std::vector<double>& parameters,const ContinuationOptions& options,const ode::NativeOptions& native,std::filesystem::path directory){return branch(model,gamma,initial,parameters,options,native,std::move(directory),true);}
}
