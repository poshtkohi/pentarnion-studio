#include "map_module.hpp"
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
namespace penta::maps {
namespace {
struct Loaded {void* handle=nullptr;const penta_maps_api_v1* api=nullptr;};
std::mutex& mutex(){static std::mutex lock;return lock;}
std::map<std::filesystem::path,Loaded>& registry(){static std::map<std::filesystem::path,Loaded> modules;return modules;}
std::filesystem::path executableDirectory(){
#ifdef _WIN32
    std::vector<wchar_t> buffer(32768);const auto count=GetModuleFileNameW(nullptr,buffer.data(),static_cast<DWORD>(buffer.size()));if(!count||count>=buffer.size())throw Error(PENTA_MAPS_INTERNAL,"Cannot determine executable directory for maps module.");return std::filesystem::path(std::wstring(buffer.data(),count)).parent_path();
#else
    std::vector<char> buffer(32768);const auto count=readlink("/proc/self/exe",buffer.data(),buffer.size());if(count<=0||static_cast<std::size_t>(count)>=buffer.size())throw Error(PENTA_MAPS_INTERNAL,"Cannot determine executable directory for maps module.");return std::filesystem::path(std::string(buffer.data(),static_cast<std::size_t>(count))).parent_path();
#endif
}
void message(char* output,uint64_t capacity,const char* text){if(output&&capacity){const auto count=std::min<uint64_t>(std::strlen(text),capacity-1);std::memcpy(output,text,static_cast<std::size_t>(count));output[count]=0;}}
struct Context {const std::shared_ptr<std::atomic_bool>& cancellation;const std::function<void(std::size_t)>& charge;std::exception_ptr failure;};
int cancelled(void* raw){const auto& context=*static_cast<Context*>(raw);return context.cancellation&&context.cancellation->load(std::memory_order_relaxed);}
int charge(void* raw,uint64_t count,char* error,uint64_t capacity)noexcept{auto& context=*static_cast<Context*>(raw);try{if(context.charge)context.charge(static_cast<std::size_t>(count));return 0;}catch(const std::exception& failure){context.failure=std::current_exception();message(error,capacity,failure.what());return 1;}catch(...){context.failure=std::current_exception();message(error,capacity,"Host map operation budget failed.");return 1;}}
}
Module::Module(std::filesystem::path directory){if(directory.empty())directory=executableDirectory()/"modules";if(!directory.is_absolute())throw Error(PENTA_MAPS_ARGUMENT,"Maps module directory must be an absolute host-controlled path.");
#ifdef _WIN32
path_=(directory/"penta_maps.dll").lexically_normal();
#else
path_=(directory/"libpenta_maps.so").lexically_normal();
#endif
}
bool Module::loaded()const{std::lock_guard lock(mutex());return registry().contains(path_);}
const penta_maps_api_v1& Module::api()const{
    std::lock_guard lock(mutex());if(const auto found=registry().find(path_);found!=registry().end())return *found->second.api;
    Loaded value;
#ifdef _WIN32
    value.handle=LoadLibraryExW(path_.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if(!value.handle)throw Error(PENTA_MAPS_INTERNAL,"Cannot load optional penta_maps module or its dependencies (Windows error "+std::to_string(GetLastError())+").");
    const auto query=reinterpret_cast<penta_maps_query_function_v1>(reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(value.handle),"penta_maps_query_v1")));
    auto release=[&]{FreeLibrary(static_cast<HMODULE>(value.handle));};
#else
    value.handle=dlopen(path_.c_str(),RTLD_NOW|RTLD_LOCAL);if(!value.handle){const char* failure=dlerror();throw Error(PENTA_MAPS_INTERNAL,std::string("Cannot load optional penta_maps module: ")+(failure?failure:"unknown loader error"));}
    const auto query=reinterpret_cast<penta_maps_query_function_v1>(dlsym(value.handle,"penta_maps_query_v1"));auto release=[&]{dlclose(value.handle);};
#endif
    if(!query){release();throw Error(PENTA_MAPS_VERSION,"Map module has no penta_maps_query_v1 export.");}
    value.api=query(PENTA_MAPS_ABI_V1);if(!value.api||value.api->struct_size<sizeof(penta_maps_api_v1)||value.api->abi_version!=PENTA_MAPS_ABI_V1||!value.api->iterate||(value.api->capabilities&1)!=1){release();throw Error(PENTA_MAPS_VERSION,"Map module API/ABI or capabilities are incompatible.");}
    return *registry().emplace(path_,value).first->second.api;
}
Result Module::iterate(const penta_maps_request_v1& request,const std::shared_ptr<std::atomic_bool>& cancellation,const std::function<void(std::size_t)>& operationCharge)const{
    if(cancellation&&cancellation->load())throw Error(PENTA_MAPS_CANCELLED,"Map iteration cancelled before module load.");
    if(request.dimension<1||request.dimension>128||request.iterations>1000000||request.transient>request.iterations||request.stride<1||request.stride>1000000)throw Error(PENTA_MAPS_ARGUMENT,"Map dimensions, iteration/transient count or stride are outside their bounds.");
    const auto samples=(request.iterations-request.transient)/request.stride+1+((request.iterations-request.transient)%request.stride?1:0);
    if(samples>1000000||request.dimension>8000000/samples)throw Error(PENTA_MAPS_ARGUMENT,"Map retained sample/coordinate budget exceeded.");
    Result result;result.solution.resize(static_cast<std::size_t>(request.dimension));result.states.resize(static_cast<std::size_t>(samples*request.dimension));result.indices.resize(static_cast<std::size_t>(samples));if(request.final_jacobian)result.jacobian.resize(static_cast<std::size_t>(request.dimension*request.dimension));
    auto& out=result.report;out.struct_size=sizeof(out);out.solution=result.solution.data();out.solution_capacity=result.solution.size();out.states=result.states.data();out.states_capacity=result.states.size();out.indices=result.indices.data();out.indices_capacity=result.indices.size();out.jacobian=result.jacobian.data();out.jacobian_capacity=result.jacobian.size();
    Context context{cancellation,operationCharge,{}};penta_maps_host_v1 host{sizeof(host),PENTA_MAPS_ABI_V1,&context,cancelled,charge};char error[2048]{};
    const auto status=api().iterate(&host,&request,&out,error,sizeof(error));if(context.failure)std::rethrow_exception(context.failure);if(status)throw Error(status,error[0]?error:"Native map iteration failed.");
    result.states.resize(static_cast<std::size_t>(out.samples*request.dimension));result.indices.resize(static_cast<std::size_t>(out.samples));if(!out.jacobian_evaluations)result.jacobian.clear();
    out.solution=nullptr;out.states=nullptr;out.indices=nullptr;out.jacobian=nullptr;return result;
}
}
