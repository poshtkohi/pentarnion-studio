#include "root_module.hpp"
#include <algorithm>
#include <cstring>
#include <exception>
#include <map>
#include <mutex>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#include <unistd.h>
#endif
namespace penta::roots {
namespace {
struct Loaded {void* handle=nullptr;const penta_roots_api_v1* api=nullptr;};
std::mutex& mutex(){static std::mutex lock;return lock;}
std::map<std::filesystem::path,Loaded>& registry(){static std::map<std::filesystem::path,Loaded> modules;return modules;}
std::filesystem::path executableDirectory(){
#ifdef _WIN32
    std::vector<wchar_t> buffer(32768);const auto count=GetModuleFileNameW(nullptr,buffer.data(),static_cast<DWORD>(buffer.size()));if(!count||count>=buffer.size())throw Error(PENTA_ROOTS_INTERNAL,"Cannot determine executable directory for roots module.");return std::filesystem::path(std::wstring(buffer.data(),count)).parent_path();
#else
    std::vector<char> buffer(32768);const auto count=readlink("/proc/self/exe",buffer.data(),buffer.size());if(count<=0||static_cast<std::size_t>(count)>=buffer.size())throw Error(PENTA_ROOTS_INTERNAL,"Cannot determine executable directory for roots module.");return std::filesystem::path(std::string(buffer.data(),static_cast<std::size_t>(count))).parent_path();
#endif
}
void message(char* output,uint64_t capacity,const char* text){if(output&&capacity){const auto count=std::min<uint64_t>(std::strlen(text),capacity-1);std::memcpy(output,text,static_cast<std::size_t>(count));output[count]=0;}}
struct Context {const std::shared_ptr<std::atomic_bool>& cancellation;const std::function<void(std::size_t)>& charge;std::exception_ptr failure;const std::function<bool(const penta_iteration_snapshot_v1&)>& observer;};
int cancelled(void* raw){const auto& context=*static_cast<Context*>(raw);return context.cancellation&&context.cancellation->load(std::memory_order_relaxed);}
int charge(void* raw,uint64_t count,char* error,uint64_t capacity)noexcept{auto& context=*static_cast<Context*>(raw);try{if(context.charge)context.charge(static_cast<std::size_t>(count));return 0;}catch(const std::exception& failure){context.failure=std::current_exception();message(error,capacity,failure.what());return 1;}catch(...){context.failure=std::current_exception();message(error,capacity,"Host root operation budget failed.");return 1;}}
int output(void* raw,const penta_iteration_snapshot_v1* snapshot,char* error,uint64_t capacity)noexcept{auto& c=*static_cast<Context*>(raw);try{if(!snapshot||snapshot->struct_size<sizeof(*snapshot)||snapshot->abi_version!=PENTA_ITERATION_ABI_V1)throw std::runtime_error("Invalid native iteration snapshot.");return c.observer&&c.observer(*snapshot)?1:0;}catch(const std::exception& e){c.failure=std::current_exception();message(error,capacity,e.what());return -1;}catch(...){c.failure=std::current_exception();message(error,capacity,"OutputFcn failed.");return -1;}}

}
Module::Module(std::filesystem::path directory){if(directory.empty())directory=executableDirectory()/"modules";if(!directory.is_absolute())throw Error(PENTA_ROOTS_ARGUMENT,"Roots module directory must be an absolute host-controlled path.");
#ifdef _WIN32
path_=(directory/"penta_roots.dll").lexically_normal();
#else
path_=(directory/"libpenta_roots.so").lexically_normal();
#endif
}
bool Module::loaded()const{std::lock_guard lock(mutex());return registry().contains(path_);}
const penta_roots_api_v1& Module::api()const{
    std::lock_guard lock(mutex());if(const auto found=registry().find(path_);found!=registry().end())return *found->second.api;
    Loaded value;
#ifdef _WIN32
    value.handle=LoadLibraryExW(path_.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if(!value.handle)throw Error(PENTA_ROOTS_INTERNAL,"Cannot load optional penta_roots module or its dependencies (Windows error "+std::to_string(GetLastError())+").");
    const auto query=reinterpret_cast<penta_roots_query_function_v1>(reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(value.handle),"penta_roots_query_v1")));
    auto release=[&]{FreeLibrary(static_cast<HMODULE>(value.handle));};
#else
    value.handle=dlopen(path_.c_str(),RTLD_NOW|RTLD_LOCAL);if(!value.handle){const char* failure=dlerror();throw Error(PENTA_ROOTS_INTERNAL,std::string("Cannot load optional penta_roots module: ")+(failure?failure:"unknown loader error"));}
    const auto query=reinterpret_cast<penta_roots_query_function_v1>(dlsym(value.handle,"penta_roots_query_v1"));auto release=[&]{dlclose(value.handle);};
#endif
    if(!query){release();throw Error(PENTA_ROOTS_VERSION,"Root module has no penta_roots_query_v1 export.");}
    value.api=query(PENTA_ROOTS_ABI_V1);if(!value.api||value.api->struct_size<sizeof(penta_roots_api_v1)||value.api->abi_version!=PENTA_ROOTS_ABI_V1||!value.api->solve||(value.api->capabilities&3)!=3){release();throw Error(PENTA_ROOTS_VERSION,"Root module API/ABI or capabilities are incompatible.");}
    return *registry().emplace(path_,value).first->second.api;
}
Result Module::solve(const penta_roots_request_v1& request,const std::shared_ptr<std::atomic_bool>& cancellation,const std::function<void(std::size_t)>& operationCharge,const std::function<bool(const penta_iteration_snapshot_v1&)>& observer)const{
    if(cancellation&&cancellation->load())throw Error(PENTA_ROOTS_CANCELLED,"Root solve cancelled before module load.");
    if(request.dimension<1||request.dimension>128)throw Error(PENTA_ROOTS_ARGUMENT,"Root dimension must be 1..128 real coordinates.");
    Result result;result.solution.resize(static_cast<std::size_t>(request.dimension));result.residual.resize(result.solution.size());result.report.struct_size=sizeof(result.report);result.report.solution=result.solution.data();result.report.solution_capacity=result.solution.size();result.report.residual=result.residual.data();result.report.residual_capacity=result.residual.size();
    Context context{cancellation,operationCharge,{},observer};penta_roots_host_v1 host{sizeof(host),PENTA_ROOTS_ABI_V1,&context,cancelled,charge,observer?output:nullptr};char error[2048]{};if(observer&&!(api().capabilities&PENTA_ITERATION_CAPABILITY_V1))throw Error(PENTA_ROOTS_VERSION,"Loaded module does not support OutputFcn snapshots.");
    const auto status=api().solve(&host,&request,&result.report,error,sizeof(error));if(context.failure)std::rethrow_exception(context.failure);if(status)throw Error(status,error[0]?error:"Native root solver failed.");
    result.report.solution=nullptr;result.report.residual=nullptr;return result;
}
}
