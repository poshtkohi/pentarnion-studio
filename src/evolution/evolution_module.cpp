#include "evolution_module.hpp"
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
namespace penta::evolution {
namespace {
struct Loaded{void* handle=nullptr;const penta_evolution_api_v1* api=nullptr;};
std::mutex& registryMutex(){static std::mutex value;return value;}
std::map<std::filesystem::path,Loaded>& registry(){static std::map<std::filesystem::path,Loaded> value;return value;}
std::filesystem::path executableDirectory(){
#ifdef _WIN32
    std::vector<wchar_t> buffer(32768);const auto count=GetModuleFileNameW(nullptr,buffer.data(),static_cast<DWORD>(buffer.size()));if(!count||count>=buffer.size())throw Error(PENTA_EVOLUTION_INTERNAL,"Cannot locate executable for evolutionary module.");return std::filesystem::path(std::wstring(buffer.data(),count)).parent_path();
#else
    std::vector<char> buffer(32768);const auto count=readlink("/proc/self/exe",buffer.data(),buffer.size());if(count<=0||static_cast<std::size_t>(count)>=buffer.size())throw Error(PENTA_EVOLUTION_INTERNAL,"Cannot locate executable for evolutionary module.");return std::filesystem::path(std::string(buffer.data(),static_cast<std::size_t>(count))).parent_path();
#endif
}
void message(char* out,uint64_t capacity,const char* text)noexcept{if(out&&capacity){const auto n=std::min<uint64_t>(std::strlen(text),capacity-1);std::memcpy(out,text,n);out[n]=0;}}
struct Context{const std::shared_ptr<std::atomic_bool>& cancellation;const std::function<void(std::size_t)>& charge;std::size_t threads;runtime::ParallelStatistics statistics;std::exception_ptr failure;std::mutex chargeMutex;const std::function<bool(const penta_iteration_snapshot_v1&)>& observer;};
int cancelled(void* raw){const auto& c=*static_cast<Context*>(raw);return c.cancellation&&c.cancellation->load(std::memory_order_relaxed);}
int charge(void* raw,uint64_t count,char* error,uint64_t capacity)noexcept{auto& c=*static_cast<Context*>(raw);std::lock_guard lock(c.chargeMutex);try{if(c.charge)c.charge(static_cast<std::size_t>(count));return 0;}catch(const std::exception& e){if(!c.failure)c.failure=std::current_exception();message(error,capacity,e.what());return 1;}catch(...){if(!c.failure)c.failure=std::current_exception();message(error,capacity,"Evolution work budget failed.");return 1;}}
int parallelFor(void* raw,uint64_t count,uint64_t cost,penta_numeric_range_task_v1 task,void* data,char* error,uint64_t capacity)noexcept{
    auto& c=*static_cast<Context*>(raw);try{runtime::ParallelSettings settings;settings.threads=c.threads;settings.serialThreshold=1;runtime::ParallelStatistics statistics;std::string failure;const int status=runtime::ThreadBudget::instance().run(count,cost,task,data,settings,c.cancellation,statistics,failure);c.statistics.threadsUsed=std::max(c.statistics.threadsUsed,statistics.threadsUsed);c.statistics.peakActiveThreads=std::max(c.statistics.peakActiveThreads,statistics.peakActiveThreads);c.statistics.parallelCalls+=statistics.parallelCalls;c.statistics.serialCalls+=statistics.serialCalls;if(status)message(error,capacity,failure.c_str());return status;}catch(const std::exception& e){message(error,capacity,e.what());return PENTA_NUMERIC_INTERNAL;}catch(...){message(error,capacity,"Evolution parallel dispatch failed.");return PENTA_NUMERIC_INTERNAL;}
}
int output(void* raw,const penta_iteration_snapshot_v1* snapshot,char* error,uint64_t capacity)noexcept{auto& c=*static_cast<Context*>(raw);try{if(!snapshot||snapshot->struct_size<sizeof(*snapshot)||snapshot->abi_version!=PENTA_ITERATION_ABI_V1)throw std::runtime_error("Invalid native iteration snapshot.");return c.observer&&c.observer(*snapshot)?1:0;}catch(const std::exception& e){c.failure=std::current_exception();message(error,capacity,e.what());return -1;}catch(...){c.failure=std::current_exception();message(error,capacity,"OutputFcn failed.");return -1;}}

}
Module::Module(std::filesystem::path directory){if(directory.empty())directory=executableDirectory()/"modules";if(!directory.is_absolute())throw Error(PENTA_EVOLUTION_ARGUMENT,"Evolution module directory must be an absolute host-controlled path.");
#ifdef _WIN32
    path_=(directory/"penta_evolution.dll").lexically_normal();
#else
    path_=(directory/"libpenta_evolution.so").lexically_normal();
#endif
}
bool Module::loaded()const{std::lock_guard lock(registryMutex());return registry().contains(path_);}
const penta_evolution_api_v1& Module::api()const{
    std::lock_guard lock(registryMutex());if(const auto it=registry().find(path_);it!=registry().end())return *it->second.api;Loaded value;
#ifdef _WIN32
    value.handle=LoadLibraryExW(path_.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);if(!value.handle)throw Error(PENTA_EVOLUTION_INTERNAL,"Cannot load penta_evolution or dependencies (Windows error "+std::to_string(GetLastError())+").");const auto query=reinterpret_cast<penta_evolution_query_function_v1>(reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(value.handle),"penta_evolution_query_v1")));auto release=[&]{FreeLibrary(static_cast<HMODULE>(value.handle));};
#else
    value.handle=dlopen(path_.c_str(),RTLD_NOW|RTLD_LOCAL);if(!value.handle){const auto* detail=dlerror();throw Error(PENTA_EVOLUTION_INTERNAL,std::string("Cannot load penta_evolution: ")+(detail?detail:"unknown loader error"));}const auto query=reinterpret_cast<penta_evolution_query_function_v1>(dlsym(value.handle,"penta_evolution_query_v1"));auto release=[&]{dlclose(value.handle);};
#endif
    if(!query){release();throw Error(PENTA_EVOLUTION_VERSION,"Evolution module has no query export.");}value.api=query(PENTA_EVOLUTION_ABI_V1);if(!value.api||value.api->struct_size<sizeof(penta_evolution_api_v1)||value.api->abi_version!=PENTA_EVOLUTION_ABI_V1||!value.api->solve||(value.api->capabilities&31)!=31){release();throw Error(PENTA_EVOLUTION_VERSION,"Evolution module API/ABI or capabilities are incompatible.");}return *registry().emplace(path_,value).first->second.api;
}
Result Module::solve(const penta_evolution_request_v1& q,std::size_t threads,const std::shared_ptr<std::atomic_bool>& cancellation,const std::function<void(std::size_t)>& operationCharge,const std::function<bool(const penta_iteration_snapshot_v1&)>& observer)const{
    if(cancellation&&cancellation->load())throw Error(PENTA_EVOLUTION_CANCELLED,"Evolution cancelled before module load.");
    if(q.dimension<1||q.dimension>128||q.objectives<1||q.objectives>16||q.population<8||q.population>4096||q.restarts>4||q.history_capacity>100000||threads>64)throw Error(PENTA_EVOLUTION_ARGUMENT,"Evolution dimensions/population/options exceed native bounds.");
    const uint64_t capacity=q.algorithm==3?std::min<uint64_t>(4096,uint64_t(q.population)<<q.restarts):q.population;
    Result result;result.solution.resize(q.dimension);result.history.resize(q.history_capacity*4);result.populationX.resize(capacity*q.dimension);result.populationF.resize(capacity*q.objectives);result.paretoX.resize(capacity*q.dimension);result.paretoF.resize(capacity*q.objectives);auto& r=result.report;r.struct_size=sizeof(r);r.solution=result.solution.data();r.solution_capacity=result.solution.size();r.history=result.history.data();r.history_capacity=result.history.size();r.population_x=result.populationX.data();r.population_x_capacity=result.populationX.size();r.population_f=result.populationF.data();r.population_f_capacity=result.populationF.size();r.pareto_x=result.paretoX.data();r.pareto_x_capacity=result.paretoX.size();r.pareto_f=result.paretoF.data();r.pareto_f_capacity=result.paretoF.size();
    Context context{cancellation,operationCharge,threads,{}, {},{},observer};context.statistics.threadsUsed=1;context.statistics.peakActiveThreads=1;penta_evolution_host_v1 host{sizeof(host),PENTA_EVOLUTION_ABI_V1,&context,cancelled,charge,parallelFor,observer?output:nullptr};char error[2048]{};if(observer&&!(api().capabilities&PENTA_ITERATION_CAPABILITY_V1))throw Error(PENTA_EVOLUTION_VERSION,"Loaded module does not support OutputFcn snapshots.");int status=0;runtime::ThreadBudget::instance().withReservation([&]{status=api().solve(&host,&q,&r,error,sizeof(error));});if(context.failure)std::rethrow_exception(context.failure);if(status)throw Error(status,error[0]?error:"Evolution failed.");
    result.history.resize(r.history_rows*4);result.populationX.resize(r.population_rows*q.dimension);result.populationF.resize(r.population_rows*q.objectives);result.paretoX.resize(r.pareto_rows*q.dimension);result.paretoF.resize(r.pareto_rows*q.objectives);result.statistics=context.statistics;r.solution=r.history=r.population_x=r.population_f=r.pareto_x=r.pareto_f=nullptr;return result;
}
}
