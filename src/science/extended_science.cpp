#include "extended_science.hpp"
#include "runtime/lazy_module.hpp"
#include <array>
#include <cstring>
#include <mutex>
namespace penta::science {
namespace {
struct Context {
    const std::shared_ptr<std::atomic_bool>& cancellation;const std::function<void(std::size_t)>& charge;
    std::size_t threads;runtime::ParallelStatistics statistics;std::mutex mutex;std::exception_ptr failure;
    static void message(char* out,uint64_t capacity,const char* text){if(out&&capacity){const auto size=std::min<uint64_t>(capacity-1,std::strlen(text));std::memcpy(out,text,size);out[size]=0;}}
    static int cancelled(void* raw){const auto& c=*static_cast<Context*>(raw);return c.cancellation&&c.cancellation->load(std::memory_order_relaxed);}
    static int account(void* raw,uint64_t n,char* error,uint64_t capacity)noexcept{auto& c=*static_cast<Context*>(raw);try{if(c.charge)c.charge(n);return 0;}catch(const std::exception& e){std::lock_guard lock(c.mutex);if(!c.failure)c.failure=std::current_exception();message(error,capacity,e.what());return 1;}catch(...){std::lock_guard lock(c.mutex);if(!c.failure)c.failure=std::current_exception();return 1;}}
    static int parallel(void* raw,uint64_t count,uint64_t cost,penta_numeric_range_task_v1 task,void* data,char* error,uint64_t capacity)noexcept{auto& c=*static_cast<Context*>(raw);try{runtime::ParallelSettings settings;settings.threads=c.threads;settings.serialThreshold=1;runtime::ParallelStatistics observed;std::string text;const auto status=runtime::ThreadBudget::instance().run(count,cost,task,data,settings,c.cancellation,observed,text);{std::lock_guard lock(c.mutex);c.statistics.requestedThreads=c.threads;c.statistics.effectiveThreads=std::max(c.statistics.effectiveThreads,observed.effectiveThreads);c.statistics.threadsUsed=std::max(c.statistics.threadsUsed,observed.threadsUsed);c.statistics.peakActiveThreads=std::max(c.statistics.peakActiveThreads,observed.peakActiveThreads);c.statistics.parallelCalls+=observed.parallelCalls;c.statistics.serialCalls+=observed.serialCalls;}message(error,capacity,text.c_str());return status;}catch(const std::exception& e){message(error,capacity,e.what());return PENTA_NUMERIC_INTERNAL;}catch(...){return PENTA_NUMERIC_INTERNAL;}}
};
}
ExtendedModule::ExtendedModule(std::filesystem::path directory):path_(runtime::detail::nativeModulePath(std::move(directory),"penta_science")){}
ExtendedResult ExtendedModule::execute(penta_science_request_v2 request,nlohmann::json options,std::size_t capacity,std::size_t threads,const std::shared_ptr<std::atomic_bool>& cancellation,const std::function<void(std::size_t)>& charge)const{
    if(capacity<1||capacity>8000000||threads>64)throw Error(PENTA_NUMERIC_ARGUMENT,"Extended science output/thread budget is invalid.");
    ExtendedResult result;result.values.resize(capacity);const auto encoded=options.dump();std::array<char,32768> metadata{};request.struct_size=sizeof(request);request.options=encoded.data();request.options_bytes=encoded.size();request.output=result.values.data();request.output_capacity=capacity;request.metadata=metadata.data();request.metadata_capacity=metadata.size();Context context{cancellation,charge,threads,{},{},{}};const penta_science_host_v2 host{sizeof(host),PENTA_SCIENCE_ABI_V2,&context,Context::cancelled,Context::account,Context::parallel};const auto& api=runtime::detail::NativeModuleRegistry<penta_science_api_v2>::get(path_,"penta_science_query_v2",PENTA_SCIENCE_ABI_V2);if(!api.execute||(api.capabilities&31)!=31)throw Error(PENTA_NUMERIC_VERSION,"Science module lacks the requested extended native capabilities.");uint64_t count=0;char error[2048]{};int status=0;runtime::ThreadBudget::instance().withReservation([&]{status=api.execute(&host,&request,&count,error,sizeof(error));});if(context.failure)std::rethrow_exception(context.failure);if(status)throw Error(status,error[0]?error:"Extended science native call failed.");if(count>capacity)throw Error(PENTA_NUMERIC_VERSION,"Science module returned an invalid output count.");result.values.resize(count);result.metadata=nlohmann::json::parse(metadata.data());result.parallel=context.statistics;return result;
}
}
