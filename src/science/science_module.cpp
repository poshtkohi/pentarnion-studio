#include "science_module.hpp"
#include "runtime/thread_budget.hpp"
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
namespace penta::science {
namespace {
std::mutex mutex;struct Loaded{void* handle;const penta_science_api_v1* api;};std::map<std::filesystem::path,Loaded> modules;
std::filesystem::path executable(){
#ifdef _WIN32
 std::vector<wchar_t> text(32768);const auto count=GetModuleFileNameW(nullptr,text.data(),static_cast<DWORD>(text.size()));if(!count||count>=text.size())throw Error(PENTA_NUMERIC_INTERNAL,"Cannot locate science module executable directory.");return std::filesystem::path(std::wstring(text.data(),count)).parent_path();
#else
 std::vector<char> text(32768);const auto count=readlink("/proc/self/exe",text.data(),text.size());if(count<=0||static_cast<std::size_t>(count)>=text.size())throw Error(PENTA_NUMERIC_INTERNAL,"Cannot locate science module executable directory.");return std::filesystem::path(std::string(text.data(),count)).parent_path();
#endif
}
int cancelled(void* p){const auto* flag=static_cast<const std::shared_ptr<std::atomic_bool>*>(p);return *flag&&(*flag)->load(std::memory_order_relaxed);}
penta_numeric_host_v1 host(const std::shared_ptr<std::atomic_bool>& flag){return {sizeof(penta_numeric_host_v1),PENTA_NUMERIC_ABI_V1,const_cast<std::shared_ptr<std::atomic_bool>*>(&flag),cancelled,nullptr};}
}
Module::Module(std::filesystem::path directory){if(directory.empty())directory=executable()/"modules";if(!directory.is_absolute())throw Error(PENTA_NUMERIC_ARGUMENT,"Science module directory must be absolute.");
#ifdef _WIN32
 path_=directory/"penta_science.dll";
#else
 path_=directory/"libpenta_science.so";
#endif
}
const penta_science_api_v1& Module::api()const {std::lock_guard lock(mutex);if(auto it=modules.find(path_);it!=modules.end())return *it->second.api;Loaded loaded{};
#ifdef _WIN32
 loaded.handle=LoadLibraryExW(path_.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);if(!loaded.handle)throw Error(PENTA_NUMERIC_INTERNAL,"Cannot load optional penta_science module or dependencies.");auto query=reinterpret_cast<penta_science_query_function_v1>(reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(loaded.handle),"penta_science_query_v1")));
 auto release=[&]{FreeLibrary(static_cast<HMODULE>(loaded.handle));};
#else
 loaded.handle=dlopen(path_.c_str(),RTLD_NOW|RTLD_LOCAL);if(!loaded.handle)throw Error(PENTA_NUMERIC_INTERNAL,"Cannot load optional penta_science module or dependencies.");auto query=reinterpret_cast<penta_science_query_function_v1>(dlsym(loaded.handle,"penta_science_query_v1"));auto release=[&]{dlclose(loaded.handle);};
#endif
 loaded.api=query?query(PENTA_SCIENCE_ABI_V1):nullptr;if(!loaded.api||loaded.api->struct_size<sizeof(penta_science_api_v1)||loaded.api->abi_version!=PENTA_SCIENCE_ABI_V1||!loaded.api->numeric||!loaded.api->symbolic){release();throw Error(PENTA_NUMERIC_VERSION,"Incompatible science module ABI.");}return *modules.emplace(path_,loaded).first->second.api;
}
std::size_t Module::numeric(penta_science_request_v1& request,const std::shared_ptr<std::atomic_bool>& flag)const {auto h=host(flag);penta_science_report_v1 report{sizeof(penta_science_report_v1),0,0};char error[1024]{};int status=0;runtime::ThreadBudget::instance().withReservation([&]{status=api().numeric(&h,&request,&report,error,sizeof(error));});if(status)throw Error(status,error[0]?error:"Science module operation failed.");return report.count;}
std::string Module::symbolic(uint32_t operation,const std::string& expression,const std::string& variable,const std::string& replacement,double& value,const std::shared_ptr<std::atomic_bool>& flag)const {auto h=host(flag);std::vector<char> text(32769);penta_symbolic_request_v1 request{sizeof(penta_symbolic_request_v1),operation,expression.c_str(),variable.c_str(),replacement.c_str(),text.data(),text.size(),0};char error[1024]{};int status=0;runtime::ThreadBudget::instance().withReservation([&]{status=api().symbolic(&h,&request,&value,error,sizeof(error));});if(status)throw Error(status,error[0]?error:"Symbolic module operation failed.");return std::string(text.data());}
}
