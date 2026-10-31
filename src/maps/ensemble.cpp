#include "ensemble.hpp"
#include "runtime/data_io.hpp"
#include "ode/native_internal.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <random>

namespace penta::maps::ensemble {
namespace {
using Json=nlohmann::json;using Clock=std::chrono::steady_clock;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
std::uint64_t mix(std::uint64_t n){n+=UINT64_C(0x9e3779b97f4a7c15);n=(n^(n>>30))*UINT64_C(0xbf58476d1ce4e5b9);n=(n^(n>>27))*UINT64_C(0x94d049bb133111eb);return n^(n>>31);}
Json payload(const Result& r){return {{"states",r.states},{"status",r.status},{"iterations",r.iterations},{"evaluations",r.evaluations},{"messages",r.messages}};}
void save(Result& r,const Options& o){
    if(o.checkpoint.empty())return;
    try{const auto data=payload(r);const Json document{{"format","PentarnionMapEnsemble"},{"version",1},{"fingerprint",r.fingerprint},{"payload_hash",ode::detail::sha256(data.dump())},{"results",data},{"rng","mt19937_64_splitmix_task_box_muller_v1"},{"restart_semantics","completed runs reused; interrupted runs restart from initial and task seed"}};const auto bytes=document.dump();require(bytes.size()<=32*1024*1024,"Map ensemble checkpoint exceeds32MiB.");if(!o.checkpoint.parent_path().empty())std::filesystem::create_directories(o.checkpoint.parent_path());runtime::dataio::writeBytesAtomic(o.checkpoint,bytes);}
    catch(const std::exception& error){r.checkpointOkay=false;r.checkpointMessage=error.what();}
}
void restore(Result& r,const Options& o){
    if(o.resume)require(!o.checkpoint.empty()&&std::filesystem::exists(o.checkpoint),"Map ensemble resume requires an existing checkpoint.");
    else{require(o.checkpoint.empty()||!std::filesystem::exists(o.checkpoint)||o.overwriteCheckpoint,"Map ensemble checkpoint exists; select resume or overwrite_checkpoint.");return;}
    require(std::filesystem::file_size(o.checkpoint)<=32*1024*1024,"Map ensemble checkpoint exceeds32MiB.");const auto document=Json::parse(runtime::dataio::readBytes(o.checkpoint));const auto& data=document.at("results");require(document.at("format")=="PentarnionMapEnsemble"&&document.at("version")==1&&document.at("fingerprint")==r.fingerprint&&document.at("payload_hash")==ode::detail::sha256(data.dump()),"Map ensemble checkpoint identity/integrity changed; prior results were retained.");
    auto vector=[&](const char* key,auto& values){using Vector=std::decay_t<decltype(values)>;auto copy=data.at(key).get<Vector>();require(copy.size()==values.size(),"Map ensemble checkpoint dimensions changed.");values=std::move(copy);};vector("states",r.states);vector("status",r.status);vector("iterations",r.iterations);vector("evaluations",r.evaluations);vector("messages",r.messages);
    for(const auto value:r.states)require(std::isfinite(value),"Map ensemble checkpoint contains nonfinite states.");
    for(std::size_t row=0;row<o.runs;++row){require(r.status[row]<=3&&r.iterations[row]<=o.iterations&&r.evaluations[row]<=o.iterations&&r.messages[row].size()<=4096,"Map ensemble checkpoint task metadata is invalid.");if(r.status[row]==1)require(r.iterations[row]==o.iterations,"A completed map checkpoint run has an incomplete iteration count.");if(o.retryFailed&&r.status[row]==3)r.status[row]=0;if(r.status[row])++r.restored;else{std::copy_n(o.initial.begin()+static_cast<std::ptrdiff_t>(row*o.coordinates),o.coordinates,r.states.begin()+static_cast<std::ptrdiff_t>(row*o.coordinates));r.iterations[row]=r.evaluations[row]=0;r.messages[row].clear();}}
}
struct Batch {
    const Options& o;Result& r;Clock::time_point started;
    bool interrupted()const{return (o.cancellation&&o.cancellation->load(std::memory_order_relaxed))||(o.stopRequested&&o.stopRequested());}
    bool expired()const{return std::chrono::duration<double>(Clock::now()-started).count()>=o.maximumSeconds;}
    static int execute(void* raw,std::uint64_t begin,std::uint64_t end,char*,std::uint64_t)noexcept{
        auto& b=*static_cast<Batch*>(raw);
        try{for(std::size_t row=begin;row<end;++row){if(b.r.status[row])continue;if(b.interrupted())return PENTA_NUMERIC_CANCELLED;if(b.expired())return PENTA_NUMERIC_OK;auto input=b.o.fixedInput;std::copy_n(b.o.initial.begin()+static_cast<std::ptrdiff_t>(row*b.o.coordinates),b.o.coordinates,input.begin());std::vector<double> output(b.o.coordinates);std::mt19937_64 random(mix(b.o.seed^mix(row+1)));std::uint32_t status=1;std::uint64_t completed=0,evaluations=0;std::string message;
            try{for(std::size_t step=0;step<b.o.iterations;++step){if(b.interrupted())return PENTA_NUMERIC_CANCELLED;if(b.expired())return PENTA_NUMERIC_OK;if(b.o.randomCoordinate!=std::numeric_limits<std::size_t>::max()){double sample=static_cast<double>(random()>>11)*0x1.0p-53;if(b.o.distribution=="normal"){const double u=(static_cast<double>(random()>>11)+.5)*0x1.0p-53;sample=std::sqrt(-2*std::log(u))*std::cos(2*std::acos(-1.)*sample);}input[b.o.randomCoordinate]=sample;}
                b.o.callback->evaluateValues(b.o.gamma,input,output);++evaluations;std::copy(output.begin(),output.end(),input.begin());completed=step+1;double radius=0;for(const auto value:output)radius=std::hypot(radius,value);if(b.o.escapeRadius>0&&radius>b.o.escapeRadius){status=2;message="escape_radius";break;}}
            }catch(const std::exception& error){if(b.interrupted())return PENTA_NUMERIC_CANCELLED;status=3;message=std::string(error.what()).substr(0,4096);}catch(...){status=3;message="Native map evaluation failed.";}
            std::copy_n(input.begin(),b.o.coordinates,b.r.states.begin()+static_cast<std::ptrdiff_t>(row*b.o.coordinates));b.r.iterations[row]=completed;b.r.evaluations[row]=evaluations;b.r.messages[row]=std::move(message);b.r.status[row]=status;
        }return PENTA_NUMERIC_OK;}catch(...){return PENTA_NUMERIC_INTERNAL;}
    }
};
}
Result run(const Options& o){
    require(o.callback&&o.runs>=1&&o.runs<=4096&&o.coordinates>=1&&o.coordinates<=128&&o.iterations>=1&&o.iterations<=1000000&&o.runs<=1000000/o.iterations,"Map ensemble dimensions/iteration budget are invalid.");require(o.initial.size()==o.runs*o.coordinates&&o.fixedInput.size()==o.callback->inputCount()&&o.callback->outputCount()==o.coordinates&&o.fixedInput.size()>=o.coordinates,"Map ensemble native coordinate layout differs from its declaration.");require(o.threads<=64&&o.batchSize>=1&&o.batchSize<=256&&std::isfinite(o.maximumSeconds)&&o.maximumSeconds>0&&std::isfinite(o.escapeRadius)&&o.escapeRadius>=0,"Map ensemble scheduling or numerical bounds are invalid.");require(o.distribution=="uniform"||o.distribution=="normal","Map ensemble random distribution is uniform or normal.");require(o.randomCoordinate==std::numeric_limits<std::size_t>::max()||(o.randomCoordinate>=o.coordinates&&o.randomCoordinate<o.fixedInput.size()),"A map random parameter must be a trailing real input coordinate.");for(const auto* values:{&o.initial,&o.fixedInput})for(const auto value:*values)require(std::isfinite(value),"Map ensemble inputs must be finite.");
    const Json configuration{{"version",1},{"kernel",o.callback->key()},{"initial",o.initial},{"fixed",o.fixedInput},{"coordinates",o.coordinates},{"runs",o.runs},{"iterations",o.iterations},{"gamma",o.gamma},{"escape",o.escapeRadius},{"seed",o.seed},{"random_coordinate",o.randomCoordinate},{"distribution",o.distribution},{"rng","mt19937_64_splitmix_task_box_muller_v1"}};
    Result r;r.fingerprint=ode::detail::sha256(configuration.dump());r.states=o.initial;r.status.resize(o.runs);r.iterations.resize(o.runs);r.evaluations.resize(o.runs);r.messages.resize(o.runs);restore(r,o);const auto started=Clock::now();Batch batch{o,r,started};runtime::ParallelSettings settings;settings.threads=o.threads;settings.serialThreshold=o.threads?1:16384;r.parallel.requestedThreads=o.threads;
    for(std::size_t begin=0;begin<o.runs;begin+=o.batchSize){if(batch.interrupted()){r.cancelled=true;break;}if(batch.expired()){r.timedOut=true;break;}const auto end=std::min(o.runs,begin+o.batchSize);runtime::ParallelStatistics statistics;std::string error;
        struct Range{Batch* batch;std::size_t offset;static int call(void* raw,std::uint64_t begin,std::uint64_t end,char* e,std::uint64_t cap){auto& r=*static_cast<Range*>(raw);return Batch::execute(r.batch,begin+r.offset,end+r.offset,e,cap);}}range{&batch,begin};
        const int status=runtime::ThreadBudget::instance().run(end-begin,std::max<std::size_t>(4096,o.coordinates*o.iterations),Range::call,&range,settings,o.cancellation,statistics,error);r.parallel.threadsUsed=std::max(r.parallel.threadsUsed,statistics.threadsUsed);r.parallel.peakActiveThreads=std::max(r.parallel.peakActiveThreads,statistics.peakActiveThreads);r.parallel.parallelCalls+=statistics.parallelCalls;r.parallel.serialCalls+=statistics.serialCalls;
        r.completed=std::count_if(r.status.begin(),r.status.end(),[](auto status){return status!=0;});save(r,o);if(status==PENTA_NUMERIC_CANCELLED){r.cancelled=true;break;}require(status==PENTA_NUMERIC_OK,error.empty()?"Map ensemble worker failed.":error.c_str());bool stop=false;if(o.progress)runtime::ThreadBudget::instance().withReservation([&]{stop=o.progress(r.completed,o.runs);});if(stop){r.userStopped=true;break;}
    }
    r.timedOut=r.timedOut||batch.expired();r.completed=std::count_if(r.status.begin(),r.status.end(),[](auto status){return status!=0;});r.elapsedSeconds=std::chrono::duration<double>(Clock::now()-started).count();return r;
}
}
