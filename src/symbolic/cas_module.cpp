#include "cas_module.hpp"
#include "runtime/thread_budget.hpp"
#include <algorithm>
#include <cstring>
#include <exception>
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
namespace penta::symbolic {
namespace {
struct Loaded {void* handle;const penta_cas_api_v1* api;};std::mutex mutex;std::map<std::filesystem::path,Loaded> modules;
std::filesystem::path executableDirectory(){
#ifdef _WIN32
    std::vector<wchar_t> buffer(32768);const auto size=GetModuleFileNameW(nullptr,buffer.data(),static_cast<DWORD>(buffer.size()));if(!size||size>=buffer.size())throw CasError(4,"Cannot locate native CAS module directory.");return std::filesystem::path(std::wstring(buffer.data(),size)).parent_path();
#else
    std::vector<char> buffer(32768);const auto size=readlink("/proc/self/exe",buffer.data(),buffer.size());if(size<=0||static_cast<std::size_t>(size)>=buffer.size())throw CasError(4,"Cannot locate native CAS module directory.");return std::filesystem::path(std::string(buffer.data(),size)).parent_path();
#endif
}
struct Host {const std::shared_ptr<std::atomic_bool>& cancellation;const std::function<void(std::size_t)>& charge;std::exception_ptr failure;static int cancelled(void* raw)noexcept{const auto& self=*static_cast<Host*>(raw);return self.cancellation&&self.cancellation->load(std::memory_order_relaxed);}static int account(void* raw,uint64_t amount,char* output,uint64_t capacity)noexcept{auto& self=*static_cast<Host*>(raw);try{if(self.charge)self.charge(static_cast<std::size_t>(amount));return 0;}catch(const std::exception& error){self.failure=std::current_exception();if(output&&capacity){const auto n=std::min<std::size_t>(capacity-1,std::strlen(error.what()));std::memcpy(output,error.what(),n);output[n]=0;}return 1;}catch(...){self.failure=std::current_exception();return 1;}}};
}
CasModule::CasModule(std::filesystem::path directory){if(directory.empty())directory=executableDirectory()/"modules";if(!directory.is_absolute())throw CasError(1,"Native CAS module directory must be absolute.");
#ifdef _WIN32
    path_=directory/"penta_cas.dll";
#else
    path_=directory/"libpenta_cas.so";
#endif
}
const penta_cas_api_v1& CasModule::api()const{std::lock_guard lock(mutex);if(const auto found=modules.find(path_);found!=modules.end())return *found->second.api;
#ifdef _WIN32
    const auto handle=LoadLibraryExW(path_.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);if(!handle)throw CasError(4,"Cannot load penta_cas or its dependencies.");const auto query=reinterpret_cast<penta_cas_query_function_v1>(reinterpret_cast<void*>(GetProcAddress(handle,"penta_cas_query_v1")));auto release=[&]{FreeLibrary(handle);};
#else
    const auto handle=dlopen(path_.c_str(),RTLD_NOW|RTLD_LOCAL);if(!handle)throw CasError(4,"Cannot load penta_cas or its dependencies.");const auto query=reinterpret_cast<penta_cas_query_function_v1>(dlsym(handle,"penta_cas_query_v1"));auto release=[&]{dlclose(handle);};
#endif
    const auto extension=query?query(PENTA_CAS_ABI_V1):nullptr;if(!extension||extension->struct_size<sizeof(*extension)||extension->abi_version!=PENTA_CAS_ABI_V1||!extension->execute){release();throw CasError(5,"Native CAS module ABI is incompatible.");}return *modules.emplace(path_,Loaded{handle,extension}).first->second.api;
}
std::string CasModule::execute(const std::string& input,std::uint64_t maximum,const std::shared_ptr<std::atomic_bool>& cancellation,const std::function<void(std::size_t)>& charge)const{if(input.size()>8*1024*1024)throw CasError(7,"Native CAS request exceeds8 MiB.");Host host{cancellation,charge,{}};penta_cas_host_v1 context{sizeof(context),PENTA_CAS_ABI_V1,&host,Host::cancelled,Host::account};std::vector<char> output(4*1024*1024+1);penta_cas_request_v1 request{sizeof(request),input.c_str(),input.size(),maximum,output.data(),output.size()};char error[2048]{};int status=0;runtime::ThreadBudget::instance().withReservation([&]{status=api().execute(&context,&request,error,sizeof(error));});if(host.failure)std::rethrow_exception(host.failure);if(status)throw CasError(status,error[0]?error:"Native CAS evaluation failed.");return std::string(output.data());}
}
