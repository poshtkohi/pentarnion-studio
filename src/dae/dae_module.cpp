#include "dae_module.hpp"
#include "runtime/thread_budget.hpp"
#include <algorithm>
#include <cmath>
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
namespace penta::dae {
namespace {
struct Loaded{void* handle;const penta_dae_api_v1* api;};std::mutex mutex;std::map<std::filesystem::path,Loaded> modules;
std::filesystem::path executable(){
#ifdef _WIN32
    std::vector<wchar_t> data(32768);const auto n=GetModuleFileNameW(nullptr,data.data(),static_cast<DWORD>(data.size()));if(!n||n>=data.size())throw Error(1,"Cannot locate DAE module executable directory.");return std::filesystem::path(std::wstring(data.data(),n)).parent_path();
#else
    std::vector<char> data(32768);const auto n=readlink("/proc/self/exe",data.data(),data.size());if(n<=0||static_cast<std::size_t>(n)>=data.size())throw Error(1,"Cannot locate DAE module executable directory.");return std::filesystem::path(std::string(data.data(),n)).parent_path();
#endif
}
struct Host{const std::shared_ptr<std::atomic_bool>& flag;const std::function<void(std::size_t)>& charge;std::exception_ptr failure;};
int cancelled(void* raw){auto& h=*static_cast<Host*>(raw);return h.flag&&h.flag->load(std::memory_order_relaxed);}
int charge(void* raw,uint64_t work,char* error,uint64_t capacity)noexcept{auto& h=*static_cast<Host*>(raw);try{if(h.charge)h.charge(work);return 0;}catch(const std::exception& e){h.failure=std::current_exception();if(error&&capacity){const auto n=std::min<uint64_t>(capacity-1,std::strlen(e.what()));std::memcpy(error,e.what(),n);error[n]=0;}return 1;}catch(...){h.failure=std::current_exception();return 1;}}
}
Module::Module(std::filesystem::path directory){if(directory.empty())directory=executable()/"modules";if(!directory.is_absolute())throw Error(1,"DAE module directory must be absolute.");
#ifdef _WIN32
path_=directory/"penta_dae.dll";
#else
path_=directory/"libpenta_dae.so";
#endif
}
const penta_dae_api_v1& Module::api()const{std::lock_guard lock(mutex);if(const auto found=modules.find(path_);found!=modules.end())return *found->second.api;
#ifdef _WIN32
    auto handle=LoadLibraryExW(path_.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);if(!handle)throw Error(1,"Cannot load optional penta_dae or a dependency.");const auto query=reinterpret_cast<penta_dae_query_function_v1>(reinterpret_cast<void*>(GetProcAddress(handle,"penta_dae_query_v1")));auto release=[&]{FreeLibrary(handle);};
#else
    auto handle=dlopen(path_.c_str(),RTLD_NOW|RTLD_LOCAL);if(!handle)throw Error(1,"Cannot load optional penta_dae or a dependency.");const auto query=reinterpret_cast<penta_dae_query_function_v1>(dlsym(handle,"penta_dae_query_v1"));auto release=[&]{dlclose(handle);};
#endif
    const auto* result=query?query(PENTA_DAE_ABI_V1):nullptr;if(!result||result->struct_size<sizeof(*result)||result->abi_version!=PENTA_DAE_ABI_V1||!result->solve){release();throw Error(5,"Incompatible DAE module ABI.");}modules.emplace(path_,Loaded{handle,result});return *result;
}
Result Module::solve(const penta_dae_request_v1& request,const std::shared_ptr<std::atomic_bool>& flag,const std::function<void(std::size_t)>& operationCharge)const{
    const auto n=request.dimension;if(!n||n>32||!std::isfinite(request.t0)||!std::isfinite(request.t1)||!std::isfinite(request.sample_step)||request.t1<=request.t0||request.sample_step<=0)throw Error(1,"Invalid DAE dimensions/time settings.");
    const double count=std::ceil((request.t1-request.t0)/request.sample_step)+2;const auto events=request.event?request.max_events:0;
    if(!std::isfinite(count)||count>1000000||events>100000||(count+events)*(2*n+1)>8000000)throw Error(1,"DAE retained-coordinate budget exceeded.");
    const auto samples=static_cast<std::size_t>(count);
    Result result;result.times.resize(samples);result.values.resize(samples*n);result.derivatives.resize(samples*n);result.initial.resize(n);result.initialDerivative.resize(n);result.eventTimes.resize(events);result.eventBefore.resize(events*n);result.eventAfter.resize(events*n);auto& r=result.report;r.struct_size=sizeof(r);r.capacity=samples;r.event_capacity=events;r.times=result.times.data();r.values=result.values.data();r.derivatives=result.derivatives.data();r.consistent_initial=result.initial.data();r.consistent_derivative=result.initialDerivative.data();r.event_times=result.eventTimes.data();r.event_before=result.eventBefore.data();r.event_after=result.eventAfter.data();
    Host context{flag,operationCharge,{}};penta_dae_host_v1 host{sizeof(host),PENTA_DAE_ABI_V1,&context,cancelled,charge};char error[2048]{};int status=0;runtime::ThreadBudget::instance().withReservation([&]{status=api().solve(&host,&request,&r,error,sizeof(error));});if(context.failure)std::rethrow_exception(context.failure);if(status)throw Error(cancelled(&context)?3:status,error[0]?error:"DAE integration failed.");result.times.resize(r.samples);result.values.resize(r.samples*n);result.derivatives.resize(r.samples*n);result.eventTimes.resize(r.events);result.eventBefore.resize(r.events*n);result.eventAfter.resize(r.events*n);return result;
}
}
