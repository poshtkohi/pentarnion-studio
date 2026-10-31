#include "discovery_module.hpp"
#include "runtime/lazy_module.hpp"
#include "runtime/data_io.hpp"
#include "ode/native_internal.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <mutex>
#include <stdexcept>
namespace penta::discovery {
namespace {
using Json=nlohmann::json;using Clock=std::chrono::steady_clock;
void message(char* target,uint64_t capacity,const char* text){if(target&&capacity){const auto n=std::min<uint64_t>(capacity-1,std::strlen(text));std::memcpy(target,text,n);target[n]=0;}}
Json scientificOptions(const ode::PreparedModel& model,const Options& o,const std::string& moduleHash){
    const auto& s=o.solver;
    return {{"version",1},{"model_key",model.key()},{"module_hash",moduleHash},{"solver_backend",ode::backendIdentity()},{"runs",o.runs},{"coordinates",model.coordinateCount()},
        {"physical_coordinates",o.physicalCoordinates},{"parameter_count",model.parameterCount()},{"initial",o.initial},{"parameters",o.parameters},{"gamma",o.gamma},
        {"t0",s.t0},{"t1",s.t1},{"sample_step",s.sampleStep},{"rtol",s.rtol},{"atol",s.atol},{"method",static_cast<int>(s.method)},
        {"jacobian",s.jacobian},{"max_steps",s.maxSteps},{"max_samples",s.maxSamples},{"max_wall_seconds",s.maximumWallSeconds},
        {"escape_radius",o.escapeRadius},{"transient",o.transient},{"return_tolerance",o.returnTolerance},
        {"equilibrium_tolerance",o.equilibriumTolerance},{"min_return_time",o.minReturnTime},{"retain_trajectories",o.retainTrajectories}};
}
struct TrajectoryOwner {std::shared_ptr<const ode::Solution> solution;std::vector<double> rhs;};
struct Context {
    const ode::PreparedModel* model;const Options* options;Result* result;
    std::vector<char>* messages;std::vector<std::shared_ptr<const ode::Solution>>* retained;
    Clock::time_point started;Json configuration;
    std::mutex failureMutex;std::exception_ptr failure;
    bool cancelled()const noexcept{try{return (options->native.cancellation&&options->native.cancellation->load(std::memory_order_relaxed))||(options->stopRequested&&options->stopRequested());}catch(...){return true;}}
    void saveFailure(){std::lock_guard lock(failureMutex);if(!failure)failure=std::current_exception();}
};
int cancelled(void* raw){return static_cast<Context*>(raw)->cancelled();}
int parallel(void* raw,uint64_t count,uint64_t cost,penta_numeric_range_task_v1 task,void* data,char* error,uint64_t capacity)noexcept{
    auto& c=*static_cast<Context*>(raw);try{
        runtime::ParallelSettings settings;settings.threads=c.options->threads;settings.serialThreshold=1;
        runtime::ParallelStatistics stats;std::string text;const auto status=runtime::ThreadBudget::instance().run(count,cost,task,data,settings,c.options->native.cancellation,stats,text);
        auto& total=c.result->parallel;total.requestedThreads=settings.threads;total.effectiveThreads=std::max(total.effectiveThreads,stats.effectiveThreads);total.threadsUsed=std::max(total.threadsUsed,stats.threadsUsed);total.peakActiveThreads=std::max(total.peakActiveThreads,stats.peakActiveThreads);total.lastElements=stats.lastElements;total.parallelCalls+=stats.parallelCalls;total.serialCalls+=stats.serialCalls;
        message(error,capacity,text.c_str());return status;
    }catch(const std::exception& e){c.saveFailure();message(error,capacity,e.what());return PENTA_NUMERIC_INTERNAL;}catch(...){c.saveFailure();return PENTA_NUMERIC_INTERNAL;}
}
int solve(void* raw,uint64_t row,const double* initial,const double* parameters,double gamma,penta_discovery_trajectory_v1* out,char* error,uint64_t capacity)noexcept{
    auto& c=*static_cast<Context*>(raw);try{
        auto settings=c.options->solver;settings.denseOutput=false;settings.sensitivityParameters.clear();settings.sensitivityInitial.clear();
        const auto n=c.model->coordinateCount(),p=c.options->physicalCoordinates;
        std::vector<double> state(initial,initial+n),params;
        if(c.model->parameterCount())params.assign(parameters,parameters+c.model->parameterCount());
        auto native=c.options->native;const auto externalCharge=native.chargeOperations;
        native.chargeOperations=[&,externalCharge](std::size_t work){
            if(std::chrono::duration<double>(Clock::now()-c.started).count()>c.options->maximumWallSeconds)throw ode::OdeError("O1003","Discovery global wall-time budget reached.");
            if(externalCharge)externalCharge(work);
        };
        auto owner=std::make_unique<TrajectoryOwner>();
        double radius=0;for(std::size_t k=0;k<p;++k)radius=std::hypot(radius,state[k]);
        if(c.options->escapeRadius>0&&radius>=c.options->escapeRadius){
            auto s=std::make_shared<ode::Solution>();s->times={settings.t0};s->values=state;s->stats.terminalEvent=true;owner->solution=s;owner->rhs.assign(n,0);
        }else{
            if(c.options->escapeRadius>0){settings.eventCount=1;settings.eventDirections={0};settings.eventTerminal={1};
                settings.eventFunction=[p,limit=c.options->escapeRadius](double,const double* y,double* event){double norm=0;for(std::size_t k=0;k<p;++k)norm=std::hypot(norm,y[k]);event[0]=norm-limit;};}
            owner->solution=c.model->solve(gamma,state,params,settings,native);
            const auto& s=*owner->solution;std::vector<double> last(s.values.end()-static_cast<std::ptrdiff_t>(n),s.values.end());
            owner->rhs=c.model->evaluate(gamma,s.times.back(),last,params,native).rhs;
        }
        const auto& s=*owner->solution;out->coordinates=n;out->samples=s.times.size();out->steps=s.stats.steps;out->rhs_evaluations=s.stats.rhsEvaluations;
        out->times=s.times.data();out->values=s.values.data();out->final_rhs=owner->rhs.data();out->threshold_reached=s.stats.terminalEvent;
        if(c.options->retainTrajectories)(*c.retained)[row]=owner->solution;
        out->handle=owner.release();return PENTA_NUMERIC_OK;
    }catch(const ode::OdeError& e){c.result->failureReasons[row]=static_cast<std::uint32_t>(classifyFailure(e.code,e.what(),c.cancelled()));message(error,capacity,e.what());return c.cancelled()?PENTA_NUMERIC_CANCELLED:e.code=="O1003"?7:PENTA_NUMERIC_DOMAIN;}
    catch(const std::exception& e){c.result->failureReasons[row]=static_cast<std::uint32_t>(classifyFailure("",e.what(),c.cancelled()));message(error,capacity,e.what());return c.cancelled()?PENTA_NUMERIC_CANCELLED:PENTA_NUMERIC_DOMAIN;}
    catch(...){c.result->failureReasons[row]=static_cast<std::uint32_t>(FailureReason::OtherNumerical);message(error,capacity,"Native ODE candidate failed.");return PENTA_NUMERIC_INTERNAL;}
}
void release(void*,penta_discovery_trajectory_v1* out){delete static_cast<TrajectoryOwner*>(out->handle);out->handle=nullptr;}
Json payload(const Result& r,const std::vector<char>& messages,std::size_t runs){
    std::vector<std::string> text(runs);for(std::size_t row=0;row<runs;++row)text[row]=messages.data()+row*512;
    return {{"status",r.status},{"states",r.states},{"stop_times",r.stopTimes},{"maximum_norm",r.maximumNorm},{"return_error",r.returnError},
        {"period",r.period},{"rhs_norm",r.rhsNorm},{"steps",r.steps},{"evaluations",r.evaluations},{"messages",text},{"failure_reasons",r.failureReasons}};
}
int checkpoint(void* raw,const penta_discovery_result_v1* report,char*,uint64_t)noexcept{
    auto& c=*static_cast<Context*>(raw);try{
        c.result->completed=static_cast<std::size_t>(report->completed);
        if(!c.options->checkpoint.empty()){
            const auto data=payload(*c.result,*c.messages,c.options->runs);
            const Json document{{"format","PentarnionExperiment"},{"version",1},{"fingerprint",c.result->fingerprint},{"configuration",c.configuration},
                {"invocation_hash",c.options->invocationHash},{"payload_hash",ode::detail::sha256(data.dump())},{"results",data}};
            const auto bytes=document.dump();if(bytes.size()>64*1024*1024)throw std::runtime_error("Experiment checkpoint exceeds64MiB.");
            if(!c.options->checkpoint.parent_path().empty())std::filesystem::create_directories(c.options->checkpoint.parent_path());
            runtime::dataio::writeBytesAtomic(c.options->checkpoint,bytes);
        }
        if(c.options->progress)c.options->progress(c.result->completed,c.options->runs);
        return 0;
    }catch(const std::exception& e){c.result->checkpointOkay=false;c.result->checkpointMessage=e.what();return 0;}
    catch(...){c.result->checkpointOkay=false;c.result->checkpointMessage="Checkpoint/progress callback failed.";return 0;}
}
template<class T>void restoreVector(const Json& data,const char* key,std::vector<T>& target){auto loaded=data.at(key).get<std::vector<T>>();if(loaded.size()!=target.size())throw std::runtime_error("Checkpoint result layout changed.");target=std::move(loaded);}
void restore(Result& r,std::vector<char>& messages,const Options& o){
    if(std::filesystem::file_size(o.checkpoint)>64*1024*1024)throw std::runtime_error("Experiment checkpoint exceeds64MiB.");
    const auto bytes=runtime::dataio::readBytes(o.checkpoint);const auto document=Json::parse(bytes);
    if(document.at("format")!="PentarnionExperiment"||document.at("version")!=1||document.at("fingerprint")!=r.fingerprint)throw std::runtime_error("Checkpoint source/model/settings are incompatible; prior data was retained.");
    const auto& data=document.at("results");if(document.at("payload_hash")!=ode::detail::sha256(data.dump()))throw std::runtime_error("Checkpoint data integrity check failed.");
    restoreVector(data,"status",r.status);restoreVector(data,"states",r.states);restoreVector(data,"stop_times",r.stopTimes);restoreVector(data,"maximum_norm",r.maximumNorm);
    restoreVector(data,"return_error",r.returnError);restoreVector(data,"period",r.period);restoreVector(data,"rhs_norm",r.rhsNorm);restoreVector(data,"steps",r.steps);restoreVector(data,"evaluations",r.evaluations);
    const auto text=data.at("messages").get<std::vector<std::string>>();if(text.size()!=o.runs)throw std::runtime_error("Checkpoint message count changed.");
    if(data.contains("failure_reasons"))restoreVector(data,"failure_reasons",r.failureReasons);
    else for(std::size_t row=0;row<o.runs;++row)if(r.status[row]==PENTA_DISCOVERY_FAILURE||r.status[row]==PENTA_DISCOVERY_TIMEOUT)r.failureReasons[row]=static_cast<std::uint32_t>(classifyFailure(r.status[row]==PENTA_DISCOVERY_TIMEOUT?"O1003":"",text[row]));
    for(auto code:r.failureReasons)if(code>10)throw std::runtime_error("Checkpoint contains an unknown failure reason.");
    for(std::size_t row=0;row<o.runs;++row){if(r.status[row]>PENTA_DISCOVERY_CANCELLED)throw std::runtime_error("Checkpoint has an unknown status.");message(messages.data()+row*512,512,text[row].c_str());
        if(r.status[row]!=PENTA_DISCOVERY_PENDING&&r.status[row]!=PENTA_DISCOVERY_CANCELLED)++r.restored;}
    for(const auto* values:{&r.states,&r.stopTimes,&r.maximumNorm,&r.returnError,&r.period,&r.rhsNorm})for(double v:*values)if(!std::isfinite(v))throw std::runtime_error("Checkpoint contains nonfinite numerical data.");
}
}
const char* statusName(std::uint32_t code)noexcept{switch(code){case 0:return "pending";case 1:return "bounded_over_horizon";case 2:return "equilibrium_candidate";case 3:return "recurrent_candidate";case 4:return "periodic_candidate";case 5:return "threshold_reached";case 6:return "numerical_failure";case 7:return "timeout";case 8:return "inconclusive";case 9:return "cancelled";default:return "unknown";}}
Module::Module(std::filesystem::path directory):path_(runtime::detail::nativeModulePath(std::move(directory),"penta_discovery")){}
bool Module::loaded()const{return runtime::detail::NativeModuleRegistry<penta_discovery_api_v1>::loaded(path_);}
const penta_discovery_api_v1& Module::api()const{const auto& result=runtime::detail::NativeModuleRegistry<penta_discovery_api_v1>::get(path_,"penta_discovery_query_v1",PENTA_DISCOVERY_ABI_V1);if(!result.run||(result.capabilities&1)!=1)throw std::runtime_error("Discovery module lacks native search capability.");return result;}
Result Module::run(const ode::PreparedModel& model,const Options& o)const{
    const auto n=model.coordinateCount(),np=model.parameterCount(),runs=o.runs;
    if(runs<1||runs>100000||n<1||n>128||o.physicalCoordinates<1||o.physicalCoordinates>n||o.initial.size()!=runs*n||o.parameters.size()!=runs*np||o.gamma.size()!=runs||o.threads>64||o.maximumBytes<1024||o.maximumBytes>1024ull*1024*1024)
        throw std::runtime_error("Discovery input shape/thread/memory bounds are invalid.");
    const long double samples=std::ceil(std::abs(static_cast<long double>(o.solver.t1)-o.solver.t0)/o.solver.sampleStep)+1;
    if(!std::isfinite(samples)||samples<2||samples>o.solver.maxSamples)throw std::runtime_error("Discovery output sample budget is invalid.");
    const auto workers=std::min({runs,o.batchSize,runtime::ThreadBudget::effectiveThreads(o.threads)});
    const long double working=workers*samples*(2*n+1)*sizeof(double);
    const long double retained=o.retainTrajectories?runs*samples*(2*n+1)*sizeof(double):0;
    const long double estimated=runs*(2*n*sizeof(double)+np*sizeof(double)+9*sizeof(double)+2*sizeof(uint64_t)+sizeof(uint32_t)+512)+working+retained;
    if(estimated>o.maximumBytes)throw std::runtime_error("Discovery result buffers exceed maximum_bytes.");
    for(const auto* values:{&o.initial,&o.parameters})for(double value:*values)if(!std::isfinite(value))throw std::runtime_error("Discovery inputs must be finite.");
    for(double gamma:o.gamma)if(!std::isfinite(gamma)||gamma<0||gamma>1)throw std::runtime_error("Discovery gamma values must be in [0,1].");
    if(o.resume&&(o.checkpoint.empty()||!std::filesystem::exists(o.checkpoint)))throw std::runtime_error("Resume requires an existing checkpoint.");
    if(!o.resume&&!o.checkpoint.empty()&&std::filesystem::exists(o.checkpoint)&&!o.overwriteCheckpoint)throw std::runtime_error("Checkpoint already exists; choose resume or explicit overwrite.");
    if(o.resume&&o.retainTrajectories)throw std::runtime_error("Retained trajectories are not restored from summary checkpoints; replay selected candidates instead.");
    const auto config=scientificOptions(model,o,ode::detail::fileHash(path_));Result result;result.fingerprint=ode::detail::sha256(config.dump());result.kernelKey=model.key();result.cacheHit=model.cacheHit();
    result.parallel.threadsUsed=0;result.parallel.peakActiveThreads=0;result.parallel.effectiveThreads=0;
    result.status.assign(runs,0);result.states.assign(runs*n,0);result.stopTimes.assign(runs,0);result.maximumNorm.assign(runs,0);result.returnError.assign(runs,0);result.period.assign(runs,0);result.rhsNorm.assign(runs,0);result.steps.assign(runs,0);result.evaluations.assign(runs,0);
    result.failureReasons.assign(runs,0);if(o.retainTrajectories)result.trajectories.resize(runs);
    std::vector<char> messages(runs*512,0);if(o.resume)restore(result,messages,o);
    auto configuration=config;configuration["checkpoint_semantics"]="completed_tasks; interrupted tasks restart from recorded initials";
    Context context{&model,&o,&result,&messages,&result.trajectories,Clock::now(),std::move(configuration),{}, {}};
    penta_discovery_host_v1 host{sizeof(host),PENTA_DISCOVERY_ABI_V1,&context,cancelled,parallel,solve,release,checkpoint};
    penta_discovery_request_v1 q{};q.struct_size=sizeof(q);q.runs=runs;q.coordinates=n;q.physical_coordinates=o.physicalCoordinates;q.parameters=np;q.initial=o.initial.data();q.parameter_values=o.parameters.data();q.gamma=o.gamma.data();q.t0=o.solver.t0;q.t1=o.solver.t1;q.transient=o.transient;q.return_tolerance=o.returnTolerance;q.equilibrium_tolerance=o.equilibriumTolerance;q.min_return_time=o.minReturnTime;q.maximum_wall_seconds=o.maximumWallSeconds;q.batch_size=o.batchSize;
    penta_discovery_result_v1 out{};out.struct_size=sizeof(out);out.capacity=runs;out.status=result.status.data();out.states=result.states.data();out.stop_times=result.stopTimes.data();out.maximum_norm=result.maximumNorm.data();out.return_error=result.returnError.data();out.period=result.period.data();out.rhs_norm=result.rhsNorm.data();out.steps=result.steps.data();out.evaluations=result.evaluations.data();out.messages=messages.data();char error[2048]{};
    const auto status=api().run(&host,&q,&out,error,sizeof(error));result.completed=out.completed;result.elapsedSeconds=out.elapsed_seconds;result.cancelled=status==PENTA_NUMERIC_CANCELLED||context.cancelled();
    result.messages.resize(runs);for(std::size_t row=0;row<runs;++row)result.messages[row]=messages.data()+row*512;
    if(context.failure)std::rethrow_exception(context.failure);
    if(status&&status!=PENTA_NUMERIC_CANCELLED)throw std::runtime_error(error[0]?error:"Native discovery worker failed.");
    return result;
}
}
