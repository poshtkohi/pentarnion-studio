#include "inference_module.hpp"
#include "fitting/fit_module.hpp"
#include "runtime/lazy_module.hpp"
#include "runtime/data_io.hpp"
#include "ode/native_internal.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <exception>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <deque>
#include <thread>
namespace penta::inference {
namespace {
using Json=nlohmann::json;
void message(char* out,uint64_t cap,const char* text){if(out&&cap){const auto n=std::min<uint64_t>(cap-1,std::strlen(text));std::memcpy(out,text,n);out[n]=0;}}
Json configuration(const penta_inference_request_v1& r,const Options& o,const std::string& moduleHash,const std::string& fitHash){
    const auto n=r.dimension;auto values=[](const double* p,uint64_t count){return p?Json(std::vector<double>(p,p+count)):Json();};
    return {{"module_hash",moduleHash},{"fit_hash",fitHash},{"model_identity",o.modelIdentity},{"operation",r.operation},{"dimension",n},{"initial",values(r.initial,n)},{"starts",(r.operation==0||r.operation==3)?values(r.starts,r.runs*n):Json()},{"x",values(r.x,r.observations)},{"y",values(r.y,r.observations)},{"prediction_x",values(r.prediction_x,r.prediction_count)},{"runs",r.runs},{"seed",r.seed},{"confidence",r.confidence},{"bootstrap_method",r.bootstrap_method},{"block_length",r.block_length},{"profile_coordinate",r.profile_coordinate},{"profile_grid",r.operation==1?values(r.profile_grid,r.runs):Json()},{"fit_method",r.fit.method},{"max_iterations",r.fit.max_iterations},{"max_evaluations",r.fit.max_evaluations},{"fit_wall_seconds",r.fit.maximum_wall_seconds},{"ftol",r.fit.function_tolerance},{"gtol",r.fit.gradient_tolerance},{"xtol",r.fit.parameter_tolerance},{"noise_variance",r.fit.noise_variance},{"noise_model",r.fit.reserved},{"noise_degrees",r.fit.loss_scale},{"lower",values(r.fit.lower,n)},{"upper",values(r.fit.upper,n)},{"parameter_scale",values(r.fit.parameter_scale,n)}};
}
Json payload(const Result& r){std::vector<std::string> messages(r.status.size());for(std::size_t i=0;i<messages.size();++i)messages[i]=r.messages.data()+512*i;return {{"parameters",r.parameters},{"costs",r.costs},{"predictions",r.predictions},{"base_parameters",r.baseParameters},{"base_predictions",r.basePredictions},{"base_ready",r.report.base_ready},{"base_cost",r.report.base_cost},{"base_rank",r.report.base_rank},{"base_message",std::string(r.report.base_message)},{"status",r.status},{"iterations",r.iterations},{"evaluations",r.evaluations},{"ranks",r.ranks},{"messages",messages}};}
template<class T>void restoreVector(const Json& data,const char* key,std::vector<T>& values){const auto restored=data.at(key).get<std::vector<T>>();if(restored.size()!=values.size())throw Error(1,"Inference checkpoint result shape changed.");values=restored;}
void restore(Result& r,const Options& o,const penta_inference_request_v1& request){
    const auto document=Json::parse(runtime::dataio::readBytes(o.checkpoint));if(document.at("format")!="PentarnionInference"||document.at("version")!=1||document.at("fingerprint")!=r.fingerprint)throw Error(1,"Inference checkpoint source/model/backend/settings are incompatible; prior data was retained.");const auto& data=document.at("results");if(document.at("payload_hash")!=ode::detail::sha256(data.dump()))throw Error(1,"Inference checkpoint data integrity check failed.");
    restoreVector(data,"parameters",r.parameters);restoreVector(data,"costs",r.costs);restoreVector(data,"predictions",r.predictions);restoreVector(data,"base_parameters",r.baseParameters);restoreVector(data,"base_predictions",r.basePredictions);restoreVector(data,"status",r.status);restoreVector(data,"iterations",r.iterations);restoreVector(data,"evaluations",r.evaluations);restoreVector(data,"ranks",r.ranks);r.report.base_rank=data.at("base_rank").get<uint64_t>();r.report.base_ready=data.at("base_ready").get<uint32_t>();r.report.base_cost=data.at("base_cost").get<double>();message(r.report.base_message,sizeof(r.report.base_message),data.at("base_message").get<std::string>().c_str());
    if(r.report.base_ready>1||!std::isfinite(r.report.base_cost))throw Error(1,"Invalid inference checkpoint baseline.");
    const auto messages=data.at("messages").get<std::vector<std::string>>();if(messages.size()!=r.status.size())throw Error(1,"Inference checkpoint message count changed.");for(std::size_t row=0;row<r.status.size();++row){if(r.status[row]>PENTA_INF_RANK_DEFICIENT)throw Error(1,"Inference checkpoint task status is invalid.");message(r.messages.data()+row*512,512,messages[row].c_str());if(o.retryFailed&&(r.status[row]==PENTA_INF_NOT_CONVERGED||r.status[row]==PENTA_INF_FAILED))r.status[row]=PENTA_INF_PENDING;if(r.status[row]!=PENTA_INF_PENDING)++r.restored;}
    for(const auto* values:{&r.parameters,&r.costs,&r.predictions,&r.baseParameters,&r.basePredictions})for(const auto v:*values)if(!std::isfinite(v))throw Error(1,"Inference checkpoint contains nonfinite data.");
    if(r.report.base_rank>request.dimension||r.report.base_cost<0)throw Error(1,"Inference checkpoint baseline metadata is invalid.");
    for(std::size_t row=0;row<r.status.size();++row){
        if(r.ranks[row]>request.dimension||r.iterations[row]>request.fit.max_iterations||r.evaluations[row]>request.fit.max_evaluations||r.costs[row]<0)throw Error(1,"Inference checkpoint task diagnostics are invalid.");
        if(r.status[row]==PENTA_INF_PENDING||r.status[row]==PENTA_INF_FAILED)continue;
        for(unsigned j=0;j<request.dimension;++j){const double value=r.parameters[row*request.dimension+j];if((request.fit.lower&&value<request.fit.lower[j]-1e-12*(1+std::abs(request.fit.lower[j])))||(request.fit.upper&&value>request.fit.upper[j]+1e-12*(1+std::abs(request.fit.upper[j]))))throw Error(1,"Inference checkpoint completed parameters violate their recorded bounds.");}
    }
}
struct Context {
    const Options& options;Result& result;const Json& configuration;std::filesystem::path fitDirectory;
    runtime::ParallelSettings settings;std::exception_ptr failure;std::mutex mutex;
    const std::thread::id owner=std::this_thread::get_id();
    std::atomic_bool userStopped{false},aborted{false};
    struct Observation {
        penta_iteration_snapshot_v1 snapshot{};std::vector<double> point;
        std::condition_variable ready;bool done=false,stop=false;
    };
    std::deque<std::shared_ptr<Observation>> pending;
    Context(const Options& o,Result& r,const Json& config,std::filesystem::path directory):options(o),result(r),configuration(config),fitDirectory(std::move(directory)){}
    bool cancelled()const{return (options.cancellation&&options.cancellation->load(std::memory_order_relaxed))||(options.stopRequested&&options.stopRequested());}
    void failed(std::exception_ptr error)noexcept{
        std::lock_guard lock(mutex);if(!failure)failure=error;aborted.store(true,std::memory_order_relaxed);
        for(auto& item:pending){item->done=true;item->stop=true;item->ready.notify_one();}pending.clear();
    }
    bool invoke(const penta_iteration_snapshot_v1& snapshot)noexcept{
        if(aborted.load(std::memory_order_relaxed))return true;
        try{const bool stop=options.observer(snapshot);if(stop&&snapshot.phase!=PENTA_ITERATION_DONE)userStopped.store(true,std::memory_order_relaxed);return userStopped.load(std::memory_order_relaxed);}
        catch(...){failed(std::current_exception());return true;}
    }
    void pump()noexcept{
        if(std::this_thread::get_id()!=owner)return;
        for(;;){
            std::shared_ptr<Observation> item;
            {std::lock_guard lock(mutex);if(pending.empty())break;item=std::move(pending.front());pending.pop_front();if(item->done)continue;}
            const bool stop=invoke(item->snapshot);
            {std::lock_guard lock(mutex);item->stop=stop;item->done=true;item->ready.notify_one();}
        }
    }
    bool observe(const penta_iteration_snapshot_v1& snapshot,const penta_inference_fit_metadata_v1& metadata,const std::vector<double>& full){
        if(!options.observer)return false;
        if(snapshot.dimension!=metadata.free_count||!snapshot.x)throw Error(1,"Inference iteration coordinate layout mismatch.");
        auto item=std::make_shared<Observation>();item->snapshot=snapshot;item->point=full;
        for(std::size_t j=0;j<metadata.free_count;++j)item->point[metadata.free_indices[j]]=snapshot.x[j];
        item->snapshot.dimension=metadata.full_dimension;item->snapshot.start_index=metadata.task_id;item->snapshot.x=item->point.data();
        if(std::this_thread::get_id()==owner){pump();return invoke(item->snapshot);}
        std::unique_lock lock(mutex);
        if(aborted.load(std::memory_order_relaxed))return true;
        if(pending.size()>=runtime::ParallelSettings::maximumThreads)throw Error(4,"Inference observer queue exceeded its bounded worker count.");
        pending.push_back(item);
        while(!item->done){item->ready.wait_for(lock,std::chrono::milliseconds(5));if(cancelled()||aborted.load(std::memory_order_relaxed)){item->done=true;item->stop=true;}}
        return item->stop;
    }
};
int cancelled(void* raw){return static_cast<Context*>(raw)->cancelled();}
int stopped(void* raw){auto& c=*static_cast<Context*>(raw);return c.userStopped.load(std::memory_order_relaxed)||c.aborted.load(std::memory_order_relaxed);}
int parallel(void* raw,uint64_t count,uint64_t cost,penta_numeric_range_task_v1 task,void* data,char* error,uint64_t capacity)noexcept{auto& c=*static_cast<Context*>(raw);try{runtime::ParallelStatistics observed;std::string detail;const auto status=runtime::ThreadBudget::instance().run(count,cost,task,data,c.settings,c.options.cancellation,observed,detail);c.result.parallel.threadsUsed=std::max(c.result.parallel.threadsUsed,observed.threadsUsed);c.result.parallel.peakActiveThreads=std::max(c.result.parallel.peakActiveThreads,observed.peakActiveThreads);c.result.parallel.parallelCalls+=observed.parallelCalls;c.result.parallel.serialCalls+=observed.serialCalls;if(status)message(error,capacity,detail.c_str());return status;}catch(const std::exception& e){c.failed(std::current_exception());message(error,capacity,e.what());return 4;}catch(...){c.failed(std::current_exception());return 4;}}
int fit(void* raw,const penta_fit_request_v1* request,const penta_inference_fit_metadata_v1* metadata,penta_fit_result_v1* output,char* error,uint64_t capacity)noexcept{
    auto& c=*static_cast<Context*>(raw);try{
        if(!request||!metadata||metadata->struct_size<sizeof(*metadata)||metadata->free_count!=request->dimension||metadata->full_dimension<1||metadata->full_dimension>16||!metadata->free_indices||!metadata->full_template)throw Error(1,"Inference fit metadata is incompatible.");
        std::vector<double> full(metadata->full_template,metadata->full_template+metadata->full_dimension);std::vector<bool> seen(metadata->full_dimension);
        for(std::size_t j=0;j<metadata->free_count;++j){const auto index=metadata->free_indices[j];if(index>=metadata->full_dimension||seen[index])throw Error(1,"Inference free-coordinate map is invalid.");seen[index]=true;}
        const auto observer=c.options.observer?std::function<bool(const penta_iteration_snapshot_v1&)>([&](const penta_iteration_snapshot_v1& snapshot){return c.observe(snapshot,*metadata,full);}):std::function<bool(const penta_iteration_snapshot_v1&)>{};
        const auto charge=[&](std::size_t work){if(std::this_thread::get_id()==c.owner)c.pump();if(c.options.charge)c.options.charge(work);};
        const auto fit=fitting::Module(c.fitDirectory).solve(*request,c.options.cancellation,charge,observer);const auto& r=fit.report;const auto n=fit.solution.size(),m=fit.residual.size();
        if(!output||output->struct_size<sizeof(*output)||output->solution_capacity<n||output->residual_capacity<m||output->jacobian_capacity<n*m||output->covariance_capacity<n*n)throw Error(1,"Native inference fit output layout mismatch.");
        std::copy(fit.solution.begin(),fit.solution.end(),output->solution);std::copy(fit.residual.begin(),fit.residual.end(),output->residuals);std::copy(fit.jacobian.begin(),fit.jacobian.end(),output->jacobian);std::copy(fit.covariance.begin(),fit.covariance.end(),output->covariance_matrix);output->rank=r.rank;output->cost=r.cost;output->converged=r.converged;output->iterations=r.iterations;output->evaluations=r.evaluations;message(output->status,sizeof(output->status),r.status);message(output->message,sizeof(output->message),r.message);return 0;
    }catch(const fitting::Error& e){message(error,capacity,e.what());return stopped(&c)?PENTA_INFERENCE_STOPPED:e.status;}catch(const std::exception& e){c.failed(std::current_exception());message(error,capacity,e.what());return 4;}catch(...){c.failed(std::current_exception());return 4;}
}
int checkpoint(void* raw,const penta_inference_result_v1*,char*,uint64_t)noexcept{auto& c=*static_cast<Context*>(raw);try{if(!c.options.checkpoint.empty()){const auto data=payload(c.result);const Json document{{"format","PentarnionInference"},{"version",1},{"fingerprint",c.result.fingerprint},{"configuration",c.configuration},{"payload_hash",ode::detail::sha256(data.dump())},{"results",data},{"restart_semantics","completed tasks reused; interrupted fits restart from recorded initials"}};const auto bytes=document.dump();if(bytes.size()>64*1024*1024)throw Error(1,"Inference checkpoint exceeds64MiB.");if(!c.options.checkpoint.parent_path().empty())std::filesystem::create_directories(c.options.checkpoint.parent_path());runtime::dataio::writeBytesAtomic(c.options.checkpoint,bytes);}return 0;}catch(const std::exception& e){c.result.checkpointOkay=false;c.result.checkpointMessage=e.what();return 0;}catch(...){c.result.checkpointOkay=false;c.result.checkpointMessage="Inference checkpoint write failed.";return 0;}}
}
Module::Module(std::filesystem::path directory):path_(runtime::detail::nativeModulePath(directory,"penta_inference")),fitDirectory_(path_.parent_path()){}
const penta_inference_api_v1& Module::api()const{const auto& a=runtime::detail::NativeModuleRegistry<penta_inference_api_v1>::get(path_,"penta_inference_query_v1",PENTA_INFERENCE_ABI_V1);if(!a.run)throw Error(5,"Inference module lacks the native experiment capability.");return a;}
Result Module::run(const penta_inference_request_v1& request,const Options& options)const{
    const uint64_t n=request.dimension,k=request.prediction_count,runs=request.runs,m=request.observations;if(!n||n>16||!runs||runs>4096||m<2||m>4096||!k||k>4096||runs*(n+k+4)>8000000||options.threads>64)throw Error(1,"Inference input/result resource bounds are invalid.");
    if(options.resume&&(options.checkpoint.empty()||!std::filesystem::exists(options.checkpoint)))throw Error(1,"Inference resume requires an existing checkpoint.");
    if(!options.resume&&!options.checkpoint.empty()&&std::filesystem::exists(options.checkpoint)&&!options.overwriteCheckpoint)throw Error(1,"Inference checkpoint exists; choose resume or explicit overwrite_checkpoint.");
    if(!options.checkpoint.empty()&&runs*(n+k+4)>2000000)throw Error(1,"Inference checkpoint coordinate bound exceeded; reduce tasks/prediction locations.");
    const auto config=configuration(request,options,ode::detail::fileHash(path_),ode::detail::fileHash(runtime::detail::nativeModulePath(fitDirectory_,"penta_fit")));Result result;result.fingerprint=ode::detail::sha256(config.dump());result.parameters.resize(runs*n);result.costs.resize(runs);result.predictions.resize(runs*k);result.baseParameters.resize(n);result.basePredictions.resize(m);result.status.resize(runs);result.iterations.resize(runs);result.evaluations.resize(runs);result.ranks.resize(runs);result.messages.resize(runs*512);result.parameterMean.resize(n);result.parameterStd.resize(n);result.correlation.resize(n*n);result.correlationDefined.resize(n*n);result.fittedLower.resize(k);result.fittedUpper.resize(k);if(options.resume)restore(result,options,request);
    auto& r=result.report;r.struct_size=sizeof(r);r.capacity=runs;r.prediction_capacity=runs*k;r.parameters=result.parameters.data();r.costs=result.costs.data();r.predictions=result.predictions.data();r.base_parameters=result.baseParameters.data();r.base_predictions=result.basePredictions.data();r.status=result.status.data();r.iterations=result.iterations.data();r.evaluations=result.evaluations.data();r.ranks=result.ranks.data();r.messages=result.messages.data();r.parameter_mean=result.parameterMean.data();r.parameter_std=result.parameterStd.data();r.correlation=result.correlation.data();r.correlation_defined=result.correlationDefined.data();r.fitted_lower=result.fittedLower.data();r.fitted_upper=result.fittedUpper.data();
    result.threadsGranted=runtime::ThreadBudget::backendThreadLimit(options.threads);result.parallel.requestedThreads=options.threads;result.parallel.effectiveThreads=result.threadsGranted;Context context(options,result,config,fitDirectory_);context.settings.threads=result.threadsGranted==1?1:options.threads;context.settings.serialThreshold=options.threads==0?16384:1;penta_inference_host_v1 host{sizeof(host),PENTA_INFERENCE_ABI_V1,&context,cancelled,parallel,fit,checkpoint,stopped};char error[2048]{};int status=0;runtime::ThreadBudget::instance().withOwnerPump([&]{context.pump();},[&]{runtime::ThreadBudget::instance().withReservation([&]{if(request.fit.reserved||request.operation==PENTA_INF_COVERAGE||request.bootstrap_method==3){const auto& extended=runtime::detail::NativeModuleRegistry<penta_inference_api_v2>::get(path_,"penta_inference_query_v2",PENTA_INFERENCE_ABI_V2);if(!extended.run)throw Error(5,"Inference DLL lacks likelihood/coverage capability.");status=extended.run(&host,&request,&r,error,sizeof(error));}else status=api().run(&host,&request,&r,error,sizeof(error));});});context.pump();r.user_stopped=context.userStopped.load(std::memory_order_relaxed);if(context.failure)std::rethrow_exception(context.failure);if(status)throw Error(cancelled(&context)?3:status,error[0]?error:"Native inference experiment failed.");return result;
}
}
