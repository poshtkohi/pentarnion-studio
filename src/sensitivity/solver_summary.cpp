#include "solver_summary.hpp"
#include "discovery/verification.hpp"
#include "ode/native_internal.hpp"
#include "runtime/lazy_module.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <mutex>
#include <set>
#include <numeric>
#include <sstream>

namespace penta::sensitivity::solver {
namespace {
using Clock=std::chrono::steady_clock;
void require(bool okay,const char* message){if(!okay)throw std::runtime_error(message);}
void copyError(char* out,uint64_t capacity,const char* message){if(out&&capacity){const auto text=std::string(message).substr(0,capacity-1);std::copy(text.begin(),text.end(),out);out[text.size()]=0;}}
void finite(double value){require(std::isfinite(value),"A native solver summary is nonfinite; no zero/missing-value imputation was performed.");}
double sampleSummary(const std::vector<double>& times,const std::vector<double>& values,std::size_t n,std::size_t coordinate,double transient,Summary summary,double crossingLevel=0){
    require(!times.empty()&&values.size()==times.size()*n&&coordinate<n,"Native summary trajectory shape is inconsistent.");
    if(summary==Summary::Period)return sampledPeriod(times,values,n,coordinate,transient,crossingLevel);
    if(summary==Summary::Final){const double value=values[values.size()-n+coordinate];finite(value);return value;}
    require(summary!=Summary::SpatialMean&&summary!=Summary::FieldRms,"Spatial summaries apply only to PDE fields.");
    long double sum=0;std::size_t count=0;double minimum=std::numeric_limits<double>::infinity(),maximum=-minimum;
    const double first=times.front();for(std::size_t sample=0;sample<times.size();++sample){if(std::abs(times[sample]-first)<transient)continue;const double value=values[sample*n+coordinate];finite(value);++count;sum+=summary==Summary::Rms?static_cast<long double>(value)*value:value;minimum=std::min(minimum,value);maximum=std::max(maximum,value);}
    require(count>0,"The selected transient leaves no stored samples for sensitivity.");
    const double result=summary==Summary::Minimum?minimum:summary==Summary::Maximum?maximum:summary==Summary::Rms?static_cast<double>(std::sqrt(sum/count)):static_cast<double>(sum/count);finite(result);return result;
}
}
#include "solver_summary_dae.inc"
double sampledPeriod(std::span<const double> times,std::span<const double> values,std::size_t coordinates,std::size_t coefficient,double transient,double level){
    require(times.size()>=3&&coordinates>=1&&coefficient<coordinates&&values.size()==times.size()*coordinates&&std::isfinite(level)&&transient>=0,"Invalid native sampled period summary.");std::vector<double> crossings;
    for(std::size_t row=1;row<times.size();++row){require(std::isfinite(times[row])&&times[row]>times[row-1],"Period summaries require increasing finite sample times.");if(times[row-1]-times.front()<transient)continue;const double a=values[(row-1)*coordinates+coefficient]-level,b=values[row*coordinates+coefficient]-level;finite(a);finite(b);if(a<0&&b>=0)crossings.push_back(times[row-1]+(-a)/(b-a)*(times[row]-times[row-1]));}
    require(crossings.size()>=3,"Period summary needs at least three post-transient rising level crossings; no zero period was substituted.");std::vector<double> intervals;for(std::size_t i=1;i<crossings.size();++i)intervals.push_back(crossings[i]-crossings[i-1]);std::sort(intervals.begin(),intervals.end());const auto middle=intervals.size()/2;return intervals.size()%2?intervals[middle]:.5*(intervals[middle-1]+intervals[middle]);
}
long double Model::estimatedBufferBytes()const{
    const long double n=stateCoordinates,fixed=initial.size()+parameters.size()+x.size()+y.size()+left.size()+right.size()+boundary.size()+512;
    if(kind==Kind::Ode||kind==Kind::Dae){if(!(odeOptions.sampleStep>0))return std::numeric_limits<long double>::infinity();const auto samples=std::ceil(std::abs(static_cast<long double>(odeOptions.t1)-odeOptions.t0)/odeOptions.sampleStep)+1;
        // Stored values/slopes/times plus conservative dense Jacobian/LU and
        // native integrator vector scratch. Allocator/SUNDIALS internals remain
        // outside an exact RSS promise, but dense n^2 work is never omitted.
        return (fixed+samples*(2*n+1)+16*n*n+256*n)*sizeof(double);
    }
    if(kind==Kind::Map){if(mapTransient>mapIterations)return std::numeric_limits<long double>::infinity();const long double retained=summary==Summary::Final?0:static_cast<long double>(mapIterations-mapTransient+1)*(n+1);return (fixed+retained+4*n)*sizeof(double);}
    const bool two=kind==Kind::Pde2;const long double nodes=two?static_cast<long double>(grid.nx)*grid.ny:pde.base.nodes;const auto step=two?grid.sample_step:pde.base.sample_step;if(!(step>0))return std::numeric_limits<long double>::infinity();
    const auto samples=std::ceil((static_cast<long double>(two?grid.t1:pde.base.t1)-(two?grid.t0:pde.base.t0))/step)+2;
    // Output plus known stencil/reaction/assembly/operator scratch, including
    // conservative sparse-entry storage. Sparse factor fill is backend/data
    // dependent; the public contract explicitly calls this a buffer forecast.
    return (fixed+samples*(nodes*n+n+2)+128*nodes*n)*sizeof(double);
}
void Model::validate(std::size_t dimensions)const{
    require(dimensions>0&&dimensions<=32&&parameterCoordinates.size()==dimensions,"Solver sensitivity requires1..32 selected flat parameter coordinates.");
    require(parameters.size()<=4096&&std::isfinite(gamma)&&gamma>=0&&gamma<=1,"Invalid native parameter/context layout.");
    std::set<std::size_t> selected;for(const auto index:parameterCoordinates)require(index<parameters.size()&&selected.insert(index).second,"Parameter coordinates must be unique valid trailing parameter indices.");
    for(const auto* array:{&initial,&initialDerivative,&parameters,&x,&y,&left,&right,&boundary})for(const double value:*array)finite(value);
    require(maximumBytes>=1024&&maximumBytes<=1024ull*1024*1024&&std::isfinite(maximumSeconds)&&maximumSeconds>0&&maximumSeconds<=3600,"Invalid solver sensitivity memory/time budget.");
    require(std::isfinite(transient)&&transient>=0,"Summary transient must be finite and nonnegative.");
    require(std::isfinite(crossingLevel)&&std::isfinite(qrStep)&&qrStep>0&&maximumIntervals>=2&&maximumIntervals<=16384,"Invalid dynamical metric summary controls.");if(summary==Summary::LargestLyapunov)require(kind==Kind::Ode,"Lyapunov summaries require an explicit native ODE variational flow.");if(summary==Summary::EventTime)require((kind==Kind::Ode||kind==Kind::Dae)&&event,"Event-time summaries require an explicitly compiled native event callback.");if(event)require((kind==Kind::Ode||kind==Kind::Dae)&&event->inputCount()==1+(kind==Kind::Dae?2:1)*stateCoordinates+parameters.size()&&event->outputCount()==1,"Native summary event layout is inconsistent.");
    if(kind==Kind::Ode){require(ode&&ode->coordinateCount()==stateCoordinates&&initial.size()==stateCoordinates&&parameters.size()==ode->parameterCount(),"Native ODE sensitivity model has inconsistent state/parameter coordinates.");require(coefficient<stateCoordinates&&node==0,"ODE coefficient is one-based in the complete flat state; node applies only to PDEs.");require(summary!=Summary::SpatialMean&&summary!=Summary::FieldRms,"Spatial summaries require a PDE model.");require(odeOptions.t1!=odeOptions.t0&&odeOptions.sampleStep>0&&odeOptions.maxSamples>=2,"Invalid ODE summary interval/sampling.");const long double samples=std::ceil(std::abs(static_cast<long double>(odeOptions.t1)-odeOptions.t0)/odeOptions.sampleStep)+1;require(std::isfinite(samples)&&samples<=odeOptions.maxSamples&&samples*(2*stateCoordinates+1)*8<=maximumBytes,"ODE summary buffers exceed max_bytes/max_samples.");require(summary==Summary::Final||transient<std::abs(odeOptions.t1-odeOptions.t0),"ODE summary transient must fit inside the observation interval.");}
    else if(kind==Kind::Dae){require(callback&&stateCoordinates>=1&&stateCoordinates<=32&&initial.size()==stateCoordinates&&initialDerivative.size()==stateCoordinates&&algebraic.size()==stateCoordinates&&callback->inputCount()==1+2*stateCoordinates+parameters.size()&&callback->outputCount()==stateCoordinates,"Native DAE summary layout is inconsistent.");require(summary!=Summary::SpatialMean&&summary!=Summary::FieldRms&&summary!=Summary::LargestLyapunov,"DAE summaries support final/mean/rms/min/max/period/event_time; variational DAEs are not inferred.");require(coefficient<stateCoordinates&&node==0,"DAE summary coefficient must select one complete state coordinate.");for(auto bit:algebraic)require(bit<=1,"Invalid DAE algebraic mask.");require(daeOptions.sample_step>0&&daeOptions.t1>daeOptions.t0,"Invalid native DAE summary interval.");}
    else if(kind==Kind::Map){require(callback&&stateCoordinates>0&&stateCoordinates<=128&&initial.size()==stateCoordinates&&callback->outputCount()==stateCoordinates&&callback->inputCount()==stateCoordinates+parameters.size(),"Native map sensitivity requires complete matching input/output state coordinates.");require(coefficient<stateCoordinates&&node==0&&mapIterations>=1&&mapIterations<=1000000&&mapTransient<=mapIterations,"Invalid native map summary coordinate/iteration/transient.");require(summary!=Summary::SpatialMean&&summary!=Summary::FieldRms,"Spatial summaries require a PDE model.");}
    else{const bool two=kind==Kind::Pde2;const auto components=two?grid.components:pde.base.components;const auto nodes=two?grid.nx*grid.ny:pde.base.nodes;require(two?(grid.nx>=3&&grid.ny>=3&&grid.nx<=256&&grid.ny<=256&&grid.nx<=16384/grid.ny):(pde.base.nodes>=3&&pde.base.nodes<=4096),"PDE sensitivity exceeds existing native grid bounds.");require(callback&&(components==1||components==5)&&components==stateCoordinates&&initial.size()==nodes*components&&coefficient<components&&node<nodes,"PDE sensitivity needs complete real/Pentarnion field coefficients and a valid node.");require(callback->inputCount()==components+(two?3:2)+reactionParameters&&callback->outputCount()==components&&parameters.size()==reactionParameters+(two?2:1),"PDE reaction parameters/diffusion coordinates are inconsistent.");require(x.size()==(two?grid.nx:pde.base.nodes)&&(!two||y.size()==grid.ny),"PDE spatial axes must match the complete field.");const auto t0=two?grid.t0:pde.base.t0,t1=two?grid.t1:pde.base.t1,step=two?grid.sample_step:pde.base.sample_step;const long double samples=std::ceil((static_cast<long double>(t1)-t0)/step)+2;require(step>0&&t1>t0&&std::isfinite(samples)&&samples*(nodes*components+components+2)*8<=maximumBytes,"PDE summary buffers exceed max_bytes.");require(summary==Summary::Final||summary==Summary::SpatialMean||summary==Summary::FieldRms||transient<t1-t0,"PDE temporal transient must fit inside the observation interval.");}
    const auto buffers=estimatedBufferBytes();require(std::isfinite(buffers)&&buffers<=maximumBytes,"Native solver outputs/Jacobian/assembly scratch exceed their per-worker share of max_bytes; reduce threads/grid/samples or increase the explicit budget.");
}

double Model::evaluate(std::span<const double> selected)const{
    require(selected.size()==parameterCoordinates.size(),"Solver sensitivity point dimensions do not match selected parameter coordinates.");
    auto point=parameters;for(std::size_t k=0;k<selected.size();++k){finite(selected[k]);point[parameterCoordinates[k]]=selected[k];}
    const auto started=Clock::now();auto stopped=[&]{if(native.cancellation&&native.cancellation->load(std::memory_order_relaxed))throw std::runtime_error("Solver sensitivity cancelled.");if(std::chrono::duration<double>(Clock::now()-started).count()>maximumSeconds)throw std::runtime_error("Solver sensitivity per-evaluation wall-time budget reached.");};
    if(kind==Kind::Dae)return daeSummary(*this,point);
    if(kind==Kind::Ode&&summary==Summary::LargestLyapunov)return discovery::verification::spectrum(*ode,initial,point,gamma,odeOptions,transient,qrStep,maximumIntervals,native,moduleDirectory).exponents.front();
    if(kind==Kind::Ode){auto settings=odeOptions;settings.denseOutput=false;settings.maxRetainedCoordinates=maximumBytes/8;settings.maximumWallSeconds=std::min(settings.maximumWallSeconds,maximumSeconds);if(event){settings.eventFunction=[&,event=event](double time,const double* state,double* output){std::vector<double> frame{time};frame.insert(frame.end(),state,state+stateCoordinates);frame.insert(frame.end(),point.begin(),point.end());event->evaluateValues(gamma,frame,std::span<double>(output,event->outputCount()));};}const auto solved=ode->solve(gamma,initial,point,settings,native);if(summary==Summary::EventTime){require(eventIndex<solved->eventTimes.size(),"The requested localized native event was not observed; no final time was substituted.");return solved->eventTimes[eventIndex];}require(!solved->stats.terminalEvent&&solved->times.back()==settings.t1,"A native sensitivity ODE stopped before its final observation; no partial summary was substituted.");return sampleSummary(solved->times,solved->values,stateCoordinates,coefficient,transient,summary,crossingLevel);}
    if(kind==Kind::Map){auto state=initial;std::vector<double> input(stateCoordinates+parameters.size()),output(stateCoordinates),times,values;std::copy(point.begin(),point.end(),input.begin()+static_cast<std::ptrdiff_t>(stateCoordinates));
        if(summary!=Summary::Final){times.reserve(mapIterations-mapTransient+1);values.reserve((mapIterations-mapTransient+1)*stateCoordinates);}
        for(std::size_t step=0;step<=mapIterations;++step){stopped();if(step>=mapTransient&&summary!=Summary::Final){times.push_back(static_cast<double>(step));values.insert(values.end(),state.begin(),state.end());}if(step==mapIterations)break;std::copy(state.begin(),state.end(),input.begin());callback->evaluateValues(gamma,input,output);state=output;for(const auto value:state)finite(value);if(charge)charge(stateCoordinates);}
        if(summary==Summary::Final)return state[coefficient];
        return sampleSummary(times,values,stateCoordinates,coefficient,0,summary,crossingLevel);
    }
    struct Reaction {const native_functions::Callback& callback;double gamma;std::size_t c;std::vector<double> input;std::exception_ptr failure;
        static int one(void* raw,double time,double x,const double* state,double* out,char* error,uint64_t capacity)noexcept{auto& b=*static_cast<Reaction*>(raw);try{std::copy_n(state,b.c,b.input.begin());b.input[b.c]=time;b.input[b.c+1]=x;b.callback.evaluateValues(b.gamma,b.input,std::span<double>(out,b.c));return 0;}catch(const std::exception& e){b.failure=std::current_exception();copyError(error,capacity,e.what());return 1;}catch(...){b.failure=std::current_exception();return 1;}}
        static int two(void* raw,double time,double x,double y,const double* state,double* out,char* error,uint64_t capacity)noexcept{auto& b=*static_cast<Reaction*>(raw);try{std::copy_n(state,b.c,b.input.begin());b.input[b.c]=time;b.input[b.c+1]=x;b.input[b.c+2]=y;b.callback.evaluateValues(b.gamma,b.input,std::span<double>(out,b.c));return 0;}catch(const std::exception& e){b.failure=std::current_exception();copyError(error,capacity,e.what());return 1;}catch(...){b.failure=std::current_exception();return 1;}}
    };
    const bool two=kind==Kind::Pde2;const auto prefix=stateCoordinates+(two?3:2);std::vector<double> bindings(prefix+reactionParameters,0);std::copy_n(point.begin(),reactionParameters,bindings.begin()+static_cast<std::ptrdiff_t>(prefix));Reaction reaction{*callback,gamma,stateCoordinates,std::move(bindings),{}};
    pde::Result solved;
    try{if(two){auto request=grid;request.x=x.data();request.y=y.data();request.initial=initial.data();request.boundary_value=boundary.data();request.diffusion_x=point[reactionParameters];request.diffusion_y=point[reactionParameters+1];request.max_wall_seconds=std::min(request.max_wall_seconds,maximumSeconds);request.reaction_context=&reaction;request.reaction=Reaction::two;solved=pde::Module(moduleDirectory).solveGrid(request,native.cancellation,charge,1);}
        else{auto request=pde;request.base.struct_size=sizeof(request);request.base.x=x.data();request.base.initial=initial.data();request.base.left=left.data();request.base.right=right.data();request.base.diffusion=point[reactionParameters];request.base.max_wall_seconds=std::min(request.base.max_wall_seconds,maximumSeconds);request.base.reaction_context=&reaction;request.base.reaction=Reaction::one;solved=pde::Module(moduleDirectory).solve(request.base,native.cancellation,charge,1);}}
    catch(...){if(reaction.failure)std::rethrow_exception(reaction.failure);throw;}
    require(!solved.times.empty()&&std::string(solved.report.status)=="completed","A native sensitivity PDE did not complete; no partial summary was substituted.");
    const auto c=stateCoordinates,nodes=initial.size()/c;
    if(summary==Summary::SpatialMean){require(solved.means.size()==solved.times.size()*c,"PDE spatial-mean layout is inconsistent.");const double value=solved.means[solved.means.size()-c+coefficient];finite(value);return value;}
    if(summary==Summary::FieldRms){require(solved.rms.size()==solved.times.size(),"PDE RMS layout is inconsistent.");finite(solved.rms.back());return solved.rms.back();}
    return sampleSummary(solved.times,solved.values,nodes*c,node*c+coefficient,transient,summary,crossingLevel);
}
std::string Model::cacheKey()const{return kind==Kind::Ode?ode->key():callback->key();}
std::string Model::fingerprint()const{
    std::ostringstream stream;stream.precision(17);stream<<"native_solver_summary_v1|"<<cacheKey()<<'|'<<int(kind)<<'|'<<int(summary)<<'|'<<gamma<<'|'<<coefficient<<'|'<<node<<'|'<<transient<<'|'<<stateCoordinates<<'|'<<crossingLevel<<'|'<<qrStep<<'|'<<maximumIntervals<<'|'<<eventIndex<<'|'<<(event?event->key():"");for(auto value:odeOptions.massMatrix)stream<<'|'<<value;for(auto value:odeOptions.eventDirections)stream<<'|'<<value;for(auto value:odeOptions.eventTerminal)stream<<'|'<<value;
    for(const auto* data:{&initial,&initialDerivative,&parameters,&x,&y,&left,&right,&boundary})for(const double value:*data)stream<<'|'<<value;
    for(const auto value:parameterCoordinates)stream<<'|'<<value;
    if(kind==Kind::Ode)stream<<'|'<<ode::backendIdentity()<<'|'<<odeOptions.t0<<'|'<<odeOptions.t1<<'|'<<odeOptions.sampleStep<<'|'<<odeOptions.rtol<<'|'<<odeOptions.atol<<'|'<<int(odeOptions.method)<<'|'<<odeOptions.jacobian;
    else if(kind==Kind::Dae){stream<<'|'<<daeOptions.t0<<'|'<<daeOptions.t1<<'|'<<daeOptions.sample_step<<'|'<<daeOptions.rtol<<'|'<<daeOptions.atol<<'|'<<daeOptions.initialization<<'|'<<daeOptions.jacobian<<'|'<<daeOptions.event_direction<<'|'<<daeOptions.event_terminal<<'|'<<ode::detail::fileHash(runtime::detail::nativeModulePath(moduleDirectory,"penta_dae"));for(auto value:initialDerivative)stream<<'|'<<value;for(auto value:algebraic)stream<<'|'<<unsigned(value);}
    else if(kind==Kind::Map)stream<<'|'<<mapIterations<<'|'<<mapTransient;
    else if(kind==Kind::Pde1){const auto& r=pde.base;stream<<'|'<<r.method<<'|'<<r.boundary<<'|'<<pde.integrator<<'|'<<r.length<<'|'<<r.t0<<'|'<<r.t1<<'|'<<r.time_step<<'|'<<r.sample_step<<'|'<<ode::detail::fileHash(runtime::detail::nativeModulePath(moduleDirectory,"penta_pde"));}
    else{stream<<'|'<<grid.boundary<<'|'<<grid.integrator<<'|'<<grid.length_x<<'|'<<grid.length_y<<'|'<<grid.t0<<'|'<<grid.t1<<'|'<<grid.time_step<<'|'<<grid.sample_step<<'|'<<ode::detail::fileHash(runtime::detail::nativeModulePath(moduleDirectory,"penta_pde"));}
    return ode::detail::sha256(stream.str());
}
Result analyze(const Model& model,penta_sensitivity_request_v1 request,const std::shared_ptr<std::atomic_bool>& cancellation,std::size_t threads){
    model.validate(request.dimensions);
    require(request.lower&&request.upper,"Sensitivity bounds must be present before native summary dispatch.");
    auto active=model;if(cancellation)active.native.cancellation=cancellation;
    struct Bridge {const Model& model;std::exception_ptr failure;std::mutex mutex;
        static int evaluate(void* raw,const double* point,uint64_t dimensions,double* output,char* error,uint64_t capacity)noexcept{auto& b=*static_cast<Bridge*>(raw);try{*output=b.model.evaluate(std::span<const double>(point,static_cast<std::size_t>(dimensions)));finite(*output);return 0;}catch(const std::exception& e){std::lock_guard lock(b.mutex);if(!b.failure)b.failure=std::current_exception();copyError(error,capacity,e.what());return 1;}catch(...){std::lock_guard lock(b.mutex);if(!b.failure)b.failure=std::current_exception();return 1;}}
    } bridge{active,{}, {}};
    request.callback_context=&bridge;request.evaluate=Bridge::evaluate;
    std::ostringstream identity;identity.precision(17);identity<<model.fingerprint()<<'|'<<request.method<<'|'<<request.dimensions<<'|'<<request.levels<<'|'<<request.samples<<'|'<<request.seed<<'|'<<request.bootstrap<<'|'<<request.confidence;
    for(std::size_t k=0;k<request.dimensions;++k)identity<<'|'<<request.lower[k]<<'|'<<request.upper[k];
    Result result;result.fingerprint=ode::detail::sha256(identity.str());try{result.sensitivity=sensitivity::Module(model.moduleDirectory).analyze(request,cancellation,threads);}catch(...){if(bridge.failure)std::rethrow_exception(bridge.failure);throw;}return result;
}
}
