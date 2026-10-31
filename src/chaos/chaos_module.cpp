#include "chaos_module.hpp"
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
namespace penta::chaos {
namespace {
struct Loaded {void* handle=nullptr;const penta_chaos_api_v1* api=nullptr;};
std::mutex& mutex(){static std::mutex value;return value;}
std::map<std::filesystem::path,Loaded>& registry(){static std::map<std::filesystem::path,Loaded> value;return value;}
std::filesystem::path executableDirectory(){
#ifdef _WIN32
    std::vector<wchar_t> buffer(32768);const auto count=GetModuleFileNameW(nullptr,buffer.data(),static_cast<DWORD>(buffer.size()));if(!count||count>=buffer.size())throw Error(PENTA_CHAOS_INTERNAL,"Cannot determine executable directory for chaos module.");return std::filesystem::path(std::wstring(buffer.data(),count)).parent_path();
#else
    std::vector<char> buffer(32768);const auto count=readlink("/proc/self/exe",buffer.data(),buffer.size());if(count<=0||static_cast<std::size_t>(count)>=buffer.size())throw Error(PENTA_CHAOS_INTERNAL,"Cannot determine executable directory for chaos module.");return std::filesystem::path(std::string(buffer.data(),static_cast<std::size_t>(count))).parent_path();
#endif
}
}
Module::Module(std::filesystem::path directory){if(directory.empty())directory=executableDirectory()/"modules";if(!directory.is_absolute())throw Error(PENTA_CHAOS_ARGUMENT,"Chaos module directory must be absolute and host controlled.");
#ifdef _WIN32
path_=(directory/"penta_chaos.dll").lexically_normal();
#else
path_=(directory/"libpenta_chaos.so").lexically_normal();
#endif
}
bool Module::loaded()const{std::lock_guard lock(mutex());return registry().contains(path_);}
const penta_chaos_api_v1& Module::api()const{
    std::lock_guard lock(mutex());if(const auto found=registry().find(path_);found!=registry().end())return *found->second.api;Loaded value;
#ifdef _WIN32
    value.handle=LoadLibraryExW(path_.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);if(!value.handle)throw Error(PENTA_CHAOS_INTERNAL,"Cannot load optional penta_chaos module or its dependencies (Windows error "+std::to_string(GetLastError())+").");
    const auto query=reinterpret_cast<penta_chaos_query_function_v1>(reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(value.handle),"penta_chaos_query_v1")));auto release=[&]{FreeLibrary(static_cast<HMODULE>(value.handle));};
#else
    value.handle=dlopen(path_.c_str(),RTLD_NOW|RTLD_LOCAL);if(!value.handle){const char* failure=dlerror();throw Error(PENTA_CHAOS_INTERNAL,std::string("Cannot load optional penta_chaos module: ")+(failure?failure:"unknown loader error"));}const auto query=reinterpret_cast<penta_chaos_query_function_v1>(dlsym(value.handle,"penta_chaos_query_v1"));auto release=[&]{dlclose(value.handle);};
#endif
    if(!query){release();throw Error(PENTA_CHAOS_VERSION,"Chaos module has no penta_chaos_query_v1 export.");}value.api=query(PENTA_CHAOS_ABI_V1);
    if(!value.api||value.api->struct_size<sizeof(penta_chaos_api_v1)||value.api->abi_version!=PENTA_CHAOS_ABI_V1||!value.api->orthonormalize||!value.api->recurrence||(value.api->capabilities&3)!=3){release();throw Error(PENTA_CHAOS_VERSION,"Chaos module API/ABI or capabilities are incompatible.");}
    return *registry().emplace(path_,value).first->second.api;
}
QR Module::orthonormalize(const std::vector<double>& tangent,std::size_t n)const{
    if(n<1||n>128||tangent.size()!=n*n)throw Error(PENTA_CHAOS_ARGUMENT,"Invalid tangent matrix size.");
    QR result;result.basis.resize(n*n);result.logarithms.resize(n);
    penta_chaos_qr_v1 request{};request.struct_size=sizeof(request);request.dimension=n;request.tangent=tangent.data();request.basis=result.basis.data();request.basis_capacity=result.basis.size();request.logarithms=result.logarithms.data();request.logarithms_capacity=result.logarithms.size();char error[1024]{};const int status=api().orthonormalize(&request,error,sizeof(error));if(status)throw Error(status,error[0]?error:"Native tangent QR failed.");result.orthogonalityError=request.orthogonality_error;return result;
}
Recurrence Module::recurrence(const std::vector<double>& states,std::size_t rows,std::size_t n,double radius,std::size_t theiler)const{
    if(rows<2||rows>256||n<1||n>128||states.size()!=rows*n)throw Error(PENTA_CHAOS_ARGUMENT,"Invalid recurrence sample dimensions.");
    Recurrence result;result.matrix.resize(rows*rows);
    penta_chaos_recurrence_v1 request{};request.struct_size=sizeof(request);request.samples=rows;request.dimension=n;request.states=states.data();request.radius=radius;request.theiler=theiler;request.matrix=result.matrix.data();request.matrix_capacity=result.matrix.size();char error[1024]{};const int status=api().recurrence(&request,error,sizeof(error));if(status)throw Error(status,error[0]?error:"Native recurrence analysis failed.");result.eligible=request.eligible;result.recurrent=request.recurrent;return result;
}
}
