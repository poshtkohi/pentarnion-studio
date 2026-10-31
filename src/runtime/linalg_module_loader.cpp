#include "linalg_module_loader.hpp"
#include <algorithm>
#include <cstring>
#include <map>
#include <mutex>
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
struct Loaded {void* handle=nullptr;const penta_linalg_api_v1* api=nullptr;};
std::mutex& registryMutex(){static std::mutex mutex;return mutex;}
std::map<std::filesystem::path,Loaded>& registry(){static std::map<std::filesystem::path,Loaded> modules;return modules;}
std::filesystem::path executableDirectory(){
#ifdef _WIN32
    std::vector<wchar_t> buffer(32768);const auto count=GetModuleFileNameW(nullptr,buffer.data(),static_cast<DWORD>(buffer.size()));if(!count||count>=buffer.size())throw NumericModuleError(PENTA_NUMERIC_INTERNAL,"Cannot determine executable directory for linear algebra module.");return std::filesystem::path(std::wstring(buffer.data(),count)).parent_path();
#else
    std::vector<char> buffer(32768);const auto count=readlink("/proc/self/exe",buffer.data(),buffer.size());if(count<=0||static_cast<size_t>(count)>=buffer.size())throw NumericModuleError(PENTA_NUMERIC_INTERNAL,"Cannot determine executable directory for linear algebra module.");return std::filesystem::path(std::string(buffer.data(),static_cast<size_t>(count))).parent_path();
#endif
}
struct Host {ParallelSettings settings;const std::shared_ptr<std::atomic_bool>& cancellation;ParallelStatistics& statistics;};
void message(char* target,uint64_t capacity,const std::string& source){if(target&&capacity){const auto length=std::min<uint64_t>(capacity-1,source.size());std::memcpy(target,source.data(),static_cast<size_t>(length));target[length]=0;}}
int cancelled(void* raw){const auto& host=*static_cast<Host*>(raw);return host.cancellation&&host.cancellation->load(std::memory_order_relaxed);}
int parallel(void* raw,uint64_t count,uint64_t cost,penta_numeric_range_task_v1 task,void* context,char* error,uint64_t capacity){try{auto& host=*static_cast<Host*>(raw);std::string text;const auto status=ThreadBudget::instance().run(static_cast<size_t>(count),static_cast<size_t>(cost),task,context,host.settings,host.cancellation,host.statistics,text);message(error,capacity,text);return status;}catch(const std::exception& failure){message(error,capacity,failure.what());return PENTA_NUMERIC_INTERNAL;}catch(...){message(error,capacity,"Unknown host thread-budget error.");return PENTA_NUMERIC_INTERNAL;}}
}
LinalgModule::LinalgModule(std::filesystem::path directory){if(directory.empty())directory=executableDirectory()/"modules";if(!directory.is_absolute())throw NumericModuleError(PENTA_NUMERIC_ARGUMENT,"Linear algebra modules require an absolute host-controlled directory.");
#ifdef _WIN32
    path_=(directory/"penta_linalg.dll").lexically_normal();
#else
    path_=(directory/"libpenta_linalg.so").lexically_normal();
#endif
}
bool LinalgModule::loaded()const{std::lock_guard lock(registryMutex());return registry().contains(path_);}
const penta_linalg_api_v1& LinalgModule::api()const{std::lock_guard lock(registryMutex());if(const auto found=registry().find(path_);found!=registry().end())return *found->second.api;Loaded module;
#ifdef _WIN32
    module.handle=LoadLibraryExW(path_.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);if(!module.handle)throw NumericModuleError(PENTA_NUMERIC_INTERNAL,"Cannot load penta_linalg; check the module and OpenBLAS/runtime dependencies. Windows error "+std::to_string(GetLastError())+".");const auto query=reinterpret_cast<penta_linalg_query_function_v1>(reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(module.handle),"penta_linalg_query_v1")));
#else
    module.handle=dlopen(path_.c_str(),RTLD_NOW|RTLD_LOCAL);if(!module.handle){const char* error=dlerror();throw NumericModuleError(PENTA_NUMERIC_INTERNAL,std::string("Cannot load penta_linalg: ")+(error?error:"unknown loader error"));}const auto query=reinterpret_cast<penta_linalg_query_function_v1>(dlsym(module.handle,"penta_linalg_query_v1"));
#endif
    auto release=[&]{
#ifdef _WIN32
        FreeLibrary(static_cast<HMODULE>(module.handle));
#else
        dlclose(module.handle);
#endif
    };if(!query){release();throw NumericModuleError(PENTA_NUMERIC_VERSION,"Linear algebra module has no penta_linalg_query_v1 export.");}module.api=query(PENTA_LINALG_ABI_V1);if(!module.api||module.api->struct_size<sizeof(penta_linalg_api_v1)||module.api->abi_version!=PENTA_LINALG_ABI_V1||!module.api->compute){release();throw NumericModuleError(PENTA_NUMERIC_VERSION,"Linear algebra module API/ABI is incompatible.");}return *registry().emplace(path_,module).first->second.api;
}
void LinalgModule::compute(penta_linalg_request_v1 request,penta_linalg_result_v1& result,const ParallelSettings& settings,const std::shared_ptr<std::atomic_bool>& cancellation,ParallelStatistics& statistics)const{
    if(cancellation&&cancellation->load())throw NumericModuleError(PENTA_NUMERIC_CANCELLED,"Linear algebra cancelled.");
    const bool nested=ThreadBudget::inNumericalTask();
    const bool smallAutomaticDense=!nested&&settings.threads==0&&request.operation>=PENTA_LA_SOLVE&&request.operation<PENTA_LA_SPARSE_MULTIPLY&&std::max(request.a.rows,request.a.columns)<=256;
    const auto available=ThreadBudget::backendThreadLimit(settings.threads);
    // On the measured32..256 dense factorization workloads a serial backend
    // avoided the substantial extra threading overhead. Explicit limits remain
    // authoritative, and GEMM retains the host pool's workload adaptation.
    const auto granted=smallAutomaticDense?std::size_t(1):available;Host context{settings,cancellation,statistics};if(nested||smallAutomaticDense)context.settings.threads=granted;request.threads=static_cast<uint32_t>(granted);penta_numeric_host_v1 host{sizeof(host),PENTA_NUMERIC_ABI_V1,&context,cancelled,parallel};char error[1024]{};int status=PENTA_NUMERIC_INTERNAL;
    ThreadBudget::instance().withReservation([&]{const auto& backend=api();
        if((backend.capabilities&PENTA_LINALG_CAP_VALUE_ONLY)==0){if(request.operation==PENTA_LA_DETERMINANT_VALUE)request.operation=PENTA_LA_DETERMINANT;else if(request.operation==PENTA_LA_CHOLESKY_FACTOR)request.operation=PENTA_LA_CHOLESKY;}
        status=backend.compute(&host,&request,&result,error,sizeof(error));
        if(!ThreadBudget::inNumericalTask()){statistics.requestedThreads=settings.threads;statistics.effectiveThreads=granted;}
    });
    if(status)throw NumericModuleError(status,error[0]?error:"Linear algebra module failed.");
}
}
