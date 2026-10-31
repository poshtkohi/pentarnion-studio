#include "petsc_solver.hpp"
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
namespace penta::petsc {
namespace {
struct Loaded {void* handle;const penta_petsc_api_v1* api;};std::map<std::filesystem::path,Loaded> modules;std::mutex mutex;
std::filesystem::path executableDirectory(){
#ifdef _WIN32
    std::vector<wchar_t> buffer(32768);const auto length=GetModuleFileNameW(nullptr,buffer.data(),static_cast<DWORD>(buffer.size()));if(!length||length>=buffer.size())throw Error(PENTA_NUMERIC_INTERNAL,"Cannot locate PETSc module executable directory.");return std::filesystem::path(std::wstring(buffer.data(),length)).parent_path();
#else
    std::vector<char> buffer(32768);const auto length=readlink("/proc/self/exe",buffer.data(),buffer.size());if(length<=0||static_cast<std::size_t>(length)>=buffer.size())throw Error(PENTA_NUMERIC_INTERNAL,"Cannot locate PETSc module executable directory.");return std::filesystem::path(std::string(buffer.data(),static_cast<std::size_t>(length))).parent_path();
#endif
}
void message(char* out,uint64_t cap,const char* text){if(out&&cap){const auto n=std::min<uint64_t>(cap-1,std::strlen(text));std::memcpy(out,text,n);out[n]=0;}}
struct Host {
    const std::shared_ptr<std::atomic_bool>& cancellation;const std::function<void(std::size_t)>& charge;const std::function<int(const penta_iteration_snapshot_v1&,std::string&)>& observer;runtime::ParallelSettings settings;runtime::ParallelStatistics stats{};std::exception_ptr failure;
    static int cancelled(void* raw)noexcept{const auto& self=*static_cast<Host*>(raw);return self.cancellation&&self.cancellation->load(std::memory_order_relaxed);}
    static int account(void* raw,uint64_t amount,char* error,uint64_t capacity)noexcept{auto& self=*static_cast<Host*>(raw);try{if(self.charge)self.charge(static_cast<std::size_t>(amount));return 0;}catch(const std::exception& e){self.failure=std::current_exception();message(error,capacity,e.what());return 1;}catch(...){self.failure=std::current_exception();return 1;}}
    static int parallel(void* raw,uint64_t n,uint64_t cost,penta_numeric_range_task_v1 task,void* context,char* error,uint64_t capacity)noexcept{auto& self=*static_cast<Host*>(raw);try{runtime::ParallelStatistics stats;std::string detail;const auto result=runtime::ThreadBudget::instance().run(n,cost,task,context,self.settings,self.cancellation,stats,detail);self.stats.parallelCalls+=stats.parallelCalls;self.stats.serialCalls+=stats.serialCalls;self.stats.threadsUsed=std::max(self.stats.threadsUsed,stats.threadsUsed);self.stats.peakActiveThreads=std::max(self.stats.peakActiveThreads,stats.peakActiveThreads);message(error,capacity,detail.c_str());return result;}catch(const std::exception& e){self.failure=std::current_exception();message(error,capacity,e.what());return PENTA_NUMERIC_INTERNAL;}catch(...){self.failure=std::current_exception();return PENTA_NUMERIC_INTERNAL;}}
    static int output(void* raw,const penta_iteration_snapshot_v1* view,char* error,uint64_t capacity)noexcept{auto& self=*static_cast<Host*>(raw);try{std::string detail;const int result=self.observer(*view,detail);message(error,capacity,detail.c_str());return result;}catch(const std::exception& e){self.failure=std::current_exception();message(error,capacity,e.what());return -1;}catch(...){self.failure=std::current_exception();return -1;}}
};
}
Module::Module(std::filesystem::path directory){if(directory.empty())directory=executableDirectory()/"modules";if(!directory.is_absolute())throw Error(PENTA_NUMERIC_ARGUMENT,"PETSc module directory must be absolute.");
#ifdef _WIN32
path_=directory/"penta_petsc.dll";
#else
path_=directory/"libpenta_petsc.so";
#endif
}
const penta_petsc_api_v1& Module::api()const{std::lock_guard lock(mutex);if(const auto found=modules.find(path_);found!=modules.end())return *found->second.api;Loaded module{};
#ifdef _WIN32
module.handle=LoadLibraryExW(path_.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);if(!module.handle)throw Error(PENTA_NUMERIC_INTERNAL,"Cannot load optional penta_petsc module or its dependency DLLs.");const auto query=reinterpret_cast<penta_petsc_query_function_v1>(reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(module.handle),"penta_petsc_query_v1")));auto release=[&]{FreeLibrary(static_cast<HMODULE>(module.handle));};
#else
module.handle=dlopen(path_.c_str(),RTLD_NOW|RTLD_LOCAL);if(!module.handle)throw Error(PENTA_NUMERIC_INTERNAL,"Cannot load optional penta_petsc module or its shared dependencies.");const auto query=reinterpret_cast<penta_petsc_query_function_v1>(dlsym(module.handle,"penta_petsc_query_v1"));auto release=[&]{dlclose(module.handle);};
#endif
module.api=query?query(PENTA_PETSC_ABI_V1):nullptr;if(!module.api||module.api->struct_size<sizeof(*module.api)||module.api->abi_version!=PENTA_PETSC_ABI_V1||!module.api->solve){release();throw Error(PENTA_NUMERIC_VERSION,"Incompatible optional PETSc module ABI.");}return *modules.emplace(path_,module).first->second.api;}
bool Module::isLoaded()noexcept{std::lock_guard lock(mutex);return !modules.empty();}
Result Module::solve(const penta_petsc_request_v1& supplied,std::size_t threads,const std::shared_ptr<std::atomic_bool>& cancellation,const std::function<void(std::size_t)>& charge,const std::function<int(const penta_iteration_snapshot_v1&,std::string&)>& observer)const{
    if(threads>64||supplied.dimension<1||supplied.dimension>100000)throw Error(PENTA_NUMERIC_ARGUMENT,"PETSc dimensions or CPU request exceed bounds.");
    Result result;result.solution.resize(supplied.dimension);result.residual.resize(supplied.dimension);auto& report=result.report;report.struct_size=sizeof(report);report.capacity=supplied.dimension;report.solution=result.solution.data();report.residual=result.residual.data();Host context{cancellation,charge,observer,runtime::ParallelSettings{threads,threads?1u:32768u},{},{}};context.stats.requestedThreads=threads;context.stats.effectiveThreads=runtime::ThreadBudget::backendThreadLimit(threads);result.threadsGranted=context.stats.effectiveThreads;auto host=penta_petsc_host_v1{sizeof(penta_petsc_host_v1),PENTA_PETSC_ABI_V1,&context,Host::cancelled,Host::account,Host::parallel,observer?Host::output:nullptr,result.threadsGranted};auto request=supplied;request.struct_size=sizeof(request);char error[1024]{};int status=0;const auto& module=api();runtime::ThreadBudget::instance().withReservation([&]{status=module.solve(&host,&request,&report,error,sizeof(error));});if(context.failure)std::rethrow_exception(context.failure);if(status)throw Error(status,error[0]?error:"PETSc backend failed.");report.solution=report.residual=nullptr;result.statistics=context.stats;return result;
}
}
