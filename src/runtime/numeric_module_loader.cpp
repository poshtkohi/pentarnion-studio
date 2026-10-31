#include "numeric_module_loader.hpp"
#include <algorithm>
#include <cstring>
#include <map>
#include <mutex>
#include <system_error>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#include <unistd.h>
#endif
namespace penta::runtime {
namespace {
struct LoadedModule {
    void* handle=nullptr;const penta_numeric_api_v1* api=nullptr;
    // Deliberately process-resident: code pointers must remain valid for the
    // host pool, cached callbacks and immutable results until final shutdown.
};
std::mutex& registryMutex(){static std::mutex mutex;return mutex;}
std::map<std::filesystem::path,LoadedModule>& registry(){static std::map<std::filesystem::path,LoadedModule> modules;return modules;}
std::filesystem::path executableDirectory(){
#ifdef _WIN32
    std::vector<wchar_t> buffer(32768);const auto count=GetModuleFileNameW(nullptr,buffer.data(),static_cast<DWORD>(buffer.size()));
    if(count==0||count>=buffer.size())throw NumericModuleError(PENTA_NUMERIC_INTERNAL,"Cannot determine Studio's executable directory.");
    return std::filesystem::path(std::wstring(buffer.data(),count)).parent_path();
#else
    std::vector<char> buffer(32768);const auto count=readlink("/proc/self/exe",buffer.data(),buffer.size());
    if(count<=0||static_cast<std::size_t>(count)>=buffer.size())throw NumericModuleError(PENTA_NUMERIC_INTERNAL,"Cannot determine Studio's executable directory.");
    return std::filesystem::path(std::string(buffer.data(),static_cast<std::size_t>(count))).parent_path();
#endif
}
void message(char* destination,std::uint64_t capacity,const std::string& text){
    if(!destination||!capacity)return;
    const auto length=std::min<std::uint64_t>(capacity-1,text.size());std::memcpy(destination,text.data(),static_cast<std::size_t>(length));destination[length]=0;
}
struct CallContext {const ParallelSettings& settings;const std::shared_ptr<std::atomic_bool>& cancellation;ParallelStatistics& statistics;};
int cancelled(void* raw){const auto& call=*static_cast<CallContext*>(raw);return call.cancellation&&call.cancellation->load(std::memory_order_relaxed);}
int parallelFor(void* raw,std::uint64_t count,std::uint64_t cost,penta_numeric_range_task_v1 task,void* taskContext,char* error,std::uint64_t capacity){
    auto& call=*static_cast<CallContext*>(raw);
    try{std::string text;const auto status=ThreadBudget::instance().run(static_cast<std::size_t>(count),static_cast<std::size_t>(cost),task,taskContext,call.settings,call.cancellation,call.statistics,text);message(error,capacity,text);return status;}
    catch(const std::exception& exception){message(error,capacity,exception.what());return PENTA_NUMERIC_INTERNAL;}
    catch(...){message(error,capacity,"Unknown failure in the shared numerical worker pool.");return PENTA_NUMERIC_INTERNAL;}
}
template<class F>void invoke(F operation,const ParallelSettings& settings,const std::shared_ptr<std::atomic_bool>& cancellation,ParallelStatistics& statistics){
    static_cast<void>(ThreadBudget::effectiveThreads(settings.threads));
    if(cancellation&&cancellation->load())throw NumericModuleError(PENTA_NUMERIC_CANCELLED,"Numerical execution cancelled.");
    auto granted=settings;
    // A native caller chooses this call's maximum; an outer numerical worker
    // already owns one CPU slot and cannot create a nested worker allocation.
    const auto limit=ThreadBudget::backendThreadLimit(settings.threads);
    if(ThreadBudget::inNumericalTask())granted.threads=limit;
    CallContext context{granted,cancellation,statistics};penta_numeric_host_v1 host{sizeof(penta_numeric_host_v1),PENTA_NUMERIC_ABI_V1,&context,cancelled,parallelFor};char error[1024]{};
    const auto status=operation(&host,error,sizeof(error));
    if(status!=PENTA_NUMERIC_OK)throw NumericModuleError(status,error[0]?error:"The numerical module reported a failure.");
}
}
NumericModule::NumericModule(std::filesystem::path directory){
    if(directory.empty())directory=executableDirectory()/"modules";
    if(!directory.is_absolute())throw NumericModuleError(PENTA_NUMERIC_ARGUMENT,"The numerical module directory must be an absolute host-controlled path.");
#ifdef _WIN32
    path_=(directory/"penta_numerics.dll").lexically_normal();
#else
    path_=(directory/"libpenta_numerics.so").lexically_normal();
#endif
}
bool NumericModule::loaded() const {std::lock_guard lock(registryMutex());return registry().contains(path_);}
const penta_numeric_api_v1& NumericModule::api() const {
    std::lock_guard lock(registryMutex());if(const auto found=registry().find(path_);found!=registry().end())return *found->second.api;
    LoadedModule loaded;
#ifdef _WIN32
    loaded.handle=LoadLibraryExW(path_.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if(!loaded.handle)throw NumericModuleError(PENTA_NUMERIC_INTERNAL,"Cannot load the optional numerical module; check its DLL dependencies and application modules directory. Windows error "+std::to_string(GetLastError())+".");
    const auto query=reinterpret_cast<penta_numeric_query_function_v1>(reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(loaded.handle),"penta_numeric_query_v1")));
#else
    loaded.handle=dlopen(path_.c_str(),RTLD_NOW|RTLD_LOCAL);
    if(!loaded.handle){const char* error=dlerror();throw NumericModuleError(PENTA_NUMERIC_INTERNAL,std::string("Cannot load the optional numerical module: ")+(error?error:"unknown loader failure"));}
    const auto query=reinterpret_cast<penta_numeric_query_function_v1>(dlsym(loaded.handle,"penta_numeric_query_v1"));
#endif
    auto release=[&]{
#ifdef _WIN32
        FreeLibrary(static_cast<HMODULE>(loaded.handle));
#else
        dlclose(loaded.handle);
#endif
    };
    if(!query){release();throw NumericModuleError(PENTA_NUMERIC_VERSION,"The optional numerical module does not export penta_numeric_query_v1.");}
    loaded.api=query(PENTA_NUMERIC_ABI_V1);
    if(!loaded.api||loaded.api->abi_version!=PENTA_NUMERIC_ABI_V1||loaded.api->struct_size<sizeof(penta_numeric_api_v1)||!loaded.api->vector||!loaded.api->reduce||!loaded.api->calculus){release();throw NumericModuleError(PENTA_NUMERIC_VERSION,"The optional numerical module has an incompatible API/ABI version or missing capability.");}
    return *registry().emplace(path_,loaded).first->second.api;
}
void NumericModule::vector(const penta_numeric_vector_request_v1& request,const ParallelSettings& settings,const std::shared_ptr<std::atomic_bool>& cancellation,ParallelStatistics& statistics) const {
    invoke([&](const auto* host,char* error,auto capacity){return api().vector(host,&request,error,capacity);},settings,cancellation,statistics);
}
void NumericModule::reduce(const penta_numeric_reduce_request_v1& request,const ParallelSettings& settings,const std::shared_ptr<std::atomic_bool>& cancellation,ParallelStatistics& statistics) const {
    invoke([&](const auto* host,char* error,auto capacity){return api().reduce(host,&request,error,capacity);},settings,cancellation,statistics);
    if(!ThreadBudget::inNumericalTask())statistics.lastElements=static_cast<std::size_t>(request.count);
}
void NumericModule::calculus(const penta_numeric_calculus_request_v1& request,const ParallelSettings& settings,const std::shared_ptr<std::atomic_bool>& cancellation,ParallelStatistics& statistics) const {
    invoke([&](const auto* host,char* error,auto capacity){return api().calculus(host,&request,error,capacity);},settings,cancellation,statistics);
}
}
