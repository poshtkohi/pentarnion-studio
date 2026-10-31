#include "sensitivity_module.hpp"
#include <algorithm>
#include <cstring>
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
namespace penta::sensitivity {
namespace {
struct Loaded{void* handle;const penta_sensitivity_api_v1* api;};std::mutex mutex;std::map<std::filesystem::path,Loaded> modules;
std::filesystem::path executable(){
#ifdef _WIN32
    std::vector<wchar_t> data(32768);const auto n=GetModuleFileNameW(nullptr,data.data(),static_cast<DWORD>(data.size()));if(!n||n>=data.size())throw Error(1,"Cannot locate sensitivity module executable directory.");return std::filesystem::path(std::wstring(data.data(),n)).parent_path();
#else
    std::vector<char> data(32768);const auto n=readlink("/proc/self/exe",data.data(),data.size());if(n<=0||static_cast<std::size_t>(n)>=data.size())throw Error(1,"Cannot locate sensitivity module executable directory.");return std::filesystem::path(std::string(data.data(),n)).parent_path();
#endif
}
struct Host {const std::shared_ptr<std::atomic_bool>& flag;runtime::ParallelSettings settings;runtime::ParallelStatistics statistics;};
int cancelled(void* raw){auto& h=*static_cast<Host*>(raw);return h.flag&&h.flag->load(std::memory_order_relaxed);}
int parallel(void* raw,uint64_t count,uint64_t cost,penta_numeric_range_task_v1 task,void* data,char* error,uint64_t capacity)noexcept{
    auto& h=*static_cast<Host*>(raw);try{runtime::ParallelStatistics observed;std::string detail;const int status=runtime::ThreadBudget::instance().run(count,cost,task,data,h.settings,h.flag,observed,detail);h.statistics.threadsUsed=std::max(h.statistics.threadsUsed,observed.threadsUsed);h.statistics.peakActiveThreads=std::max(h.statistics.peakActiveThreads,observed.peakActiveThreads);h.statistics.parallelCalls+=observed.parallelCalls;h.statistics.serialCalls+=observed.serialCalls;h.statistics.lastElements=count;if(status&&error&&capacity){const auto n=std::min<uint64_t>(capacity-1,detail.size());std::memcpy(error,detail.data(),n);error[n]=0;}return status;}catch(const std::exception& e){if(error&&capacity){const auto n=std::min<uint64_t>(capacity-1,std::strlen(e.what()));std::memcpy(error,e.what(),n);error[n]=0;}return 4;}catch(...){return 4;}
}
}
Module::Module(std::filesystem::path directory){if(directory.empty())directory=executable()/"modules";if(!directory.is_absolute())throw Error(1,"Sensitivity module directory must be absolute.");
#ifdef _WIN32
path_=directory/"penta_sensitivity.dll";
#else
path_=directory/"libpenta_sensitivity.so";
#endif
}
const penta_sensitivity_api_v1& Module::api()const{std::lock_guard lock(mutex);if(const auto found=modules.find(path_);found!=modules.end())return *found->second.api;
#ifdef _WIN32
    auto handle=LoadLibraryExW(path_.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);if(!handle)throw Error(1,"Cannot load optional penta_sensitivity or a dependency.");const auto query=reinterpret_cast<penta_sensitivity_query_function_v1>(reinterpret_cast<void*>(GetProcAddress(handle,"penta_sensitivity_query_v1")));auto release=[&]{FreeLibrary(handle);};
#else
    auto handle=dlopen(path_.c_str(),RTLD_NOW|RTLD_LOCAL);if(!handle)throw Error(1,"Cannot load optional penta_sensitivity or a dependency.");const auto query=reinterpret_cast<penta_sensitivity_query_function_v1>(dlsym(handle,"penta_sensitivity_query_v1"));auto release=[&]{dlclose(handle);};
#endif
    const auto* result=query?query(PENTA_SENSITIVITY_ABI_V1):nullptr;if(!result||result->struct_size<sizeof(*result)||result->abi_version!=PENTA_SENSITIVITY_ABI_V1||!result->analyze){release();throw Error(5,"Incompatible sensitivity module ABI.");}modules.emplace(path_,Loaded{handle,result});return *result;
}
Result Module::analyze(const penta_sensitivity_request_v1& request,const std::shared_ptr<std::atomic_bool>& flag,std::size_t threads)const{
    if(!request.dimensions||request.dimensions>32||threads>64)throw Error(1,"Sensitivity supports 1..32 dimensions and threads=0..64.");
    const auto n=request.dimensions;Result result;for(auto* values:{&result.mu,&result.muStar,&result.sigma,&result.first,&result.total,&result.firstLower,&result.firstUpper,&result.totalLower,&result.totalUpper})values->resize(n);auto& r=result.report;r.struct_size=sizeof(r);r.capacity=n;r.mu=result.mu.data();r.mu_star=result.muStar.data();r.sigma=result.sigma.data();r.first=result.first.data();r.total=result.total.data();r.first_lower=result.firstLower.data();r.first_upper=result.firstUpper.data();r.total_lower=result.totalLower.data();r.total_upper=result.totalUpper.data();
    const auto granted=runtime::ThreadBudget::backendThreadLimit(threads);Host context{flag,{},{}};context.settings.threads=granted==1?1:threads;context.settings.serialThreshold=threads==0?16384:1;context.statistics.requestedThreads=threads;context.statistics.effectiveThreads=granted;penta_numeric_host_v1 host{sizeof(host),PENTA_NUMERIC_ABI_V1,&context,cancelled,parallel};char error[2048]{};int status=0;runtime::ThreadBudget::instance().withReservation([&]{status=api().analyze(&host,&request,&r,error,sizeof(error));});if(status)throw Error(cancelled(&context)?3:status,error[0]?error:"Sensitivity analysis failed.");result.statistics=context.statistics;result.threadsGranted=granted;return result;
}
}
