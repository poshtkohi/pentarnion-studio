#include "network_solver.hpp"
#include "modules/network_operator_api.h"
#include <chrono>
#include <cmath>
#include <span>
#include "runtime/thread_budget.hpp"
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

namespace penta::networks {
namespace {
struct Loaded {void* handle=nullptr;const penta_network_api_v1* api=nullptr;};
std::mutex moduleMutex;
std::map<std::filesystem::path,Loaded> modules;
std::filesystem::path executableDirectory() {
#ifdef _WIN32
    std::vector<wchar_t> value(32768);const auto count=GetModuleFileNameW(nullptr,value.data(),static_cast<DWORD>(value.size()));
    if(!count||count>=value.size())throw Error(PENTA_NUMERIC_INTERNAL,"Cannot locate graph module executable directory.");
    return std::filesystem::path(std::wstring(value.data(),count)).parent_path();
#else
    std::vector<char> value(32768);const auto count=readlink("/proc/self/exe",value.data(),value.size());
    if(count<=0||static_cast<std::size_t>(count)>=value.size())throw Error(PENTA_NUMERIC_INTERNAL,"Cannot locate graph module executable directory.");
    return std::filesystem::path(std::string(value.data(),static_cast<std::size_t>(count))).parent_path();
#endif
}
struct Host {
    const ode::PreparedModel& model;double gamma;const std::vector<double>& parameters;const ode::NativeOptions& native;runtime::ParallelSettings settings;Result& result;std::mutex mutex;std::exception_ptr failure;
    static int cancelled(void* raw)noexcept{const auto& h=*static_cast<Host*>(raw);return h.native.cancellation&&h.native.cancellation->load(std::memory_order_relaxed);}
    void failed(){std::lock_guard lock(mutex);if(!failure)failure=std::current_exception();}
    static void errorText(char* out,uint64_t cap,const char* text){if(out&&cap){const auto n=std::min<uint64_t>(cap-1,std::strlen(text));std::memcpy(out,text,n);out[n]=0;}}
    static int account(void* raw,uint64_t amount,char* error,uint64_t cap)noexcept{auto& h=*static_cast<Host*>(raw);try{if(h.native.chargeOperations)h.native.chargeOperations(static_cast<std::size_t>(amount));return 0;}catch(const std::exception& e){h.failed();errorText(error,cap,e.what());return 1;}catch(...){h.failed();return 1;}}
    static int local(void* raw,double time,const double* state,double* out,double* jac,char* error,uint64_t cap)noexcept{auto& h=*static_cast<Host*>(raw);try{const auto n=h.model.coordinateCount();if(jac)h.model.evaluateJacobian(h.gamma,time,{state,n},h.parameters,{out,n},{jac,n*n},h.native);else h.model.evaluateValues(h.gamma,time,{state,n},h.parameters,{out,n},h.native);return 0;}catch(const std::exception& e){h.failed();errorText(error,cap,e.what());return 1;}catch(...){h.failed();return 1;}}
    static int parallel(void* raw,uint64_t count,uint64_t cost,penta_numeric_range_task_v1 task,void* data,char* error,uint64_t cap)noexcept{auto& h=*static_cast<Host*>(raw);try{runtime::ParallelStatistics observed;std::string detail;const int status=runtime::ThreadBudget::instance().run(count,cost,task,data,h.settings,h.native.cancellation,observed,detail);auto& stats=h.result.parallel;stats.threadsUsed=std::max(stats.threadsUsed,observed.threadsUsed);stats.peakActiveThreads=std::max(stats.peakActiveThreads,observed.peakActiveThreads);stats.parallelCalls+=observed.parallelCalls;stats.serialCalls+=observed.serialCalls;if(status)errorText(error,cap,detail.c_str());return status;}catch(const std::exception& e){h.failed();errorText(error,cap,e.what());return 4;}catch(...){h.failed();return 4;}}
};}

Module::Module(std::filesystem::path directory) {
    if(directory.empty())directory=executableDirectory()/"modules";
    if(!directory.is_absolute())throw Error(PENTA_NUMERIC_ARGUMENT,"Graph module directory must be absolute.");
#ifdef _WIN32
    path_=directory/"penta_networks.dll";
#else
    path_=directory/"libpenta_networks.so";
#endif
}
const penta_network_api_v1& Module::api() const {
    std::lock_guard lock(moduleMutex);
    if(const auto found=modules.find(path_);found!=modules.end())return *found->second.api;
    Loaded loaded;
#ifdef _WIN32
    loaded.handle=LoadLibraryExW(path_.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if(!loaded.handle)throw Error(PENTA_NUMERIC_INTERNAL,"Cannot load optional penta_networks module or its dependencies.");
    const auto query=reinterpret_cast<penta_network_query_function_v1>(reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(loaded.handle),"penta_network_query_v1")));
    auto release=[&]{FreeLibrary(static_cast<HMODULE>(loaded.handle));};
#else
    loaded.handle=dlopen(path_.c_str(),RTLD_NOW|RTLD_LOCAL);
    if(!loaded.handle)throw Error(PENTA_NUMERIC_INTERNAL,"Cannot load optional penta_networks module or its dependencies.");
    const auto query=reinterpret_cast<penta_network_query_function_v1>(dlsym(loaded.handle,"penta_network_query_v1"));
    auto release=[&]{dlclose(loaded.handle);};
#endif
    loaded.api=query?query(PENTA_NETWORK_ABI_V1):nullptr;
    if(!loaded.api||loaded.api->struct_size<sizeof(penta_network_api_v1)||loaded.api->abi_version!=PENTA_NETWORK_ABI_V1||!loaded.api->solve){release();throw Error(PENTA_NUMERIC_VERSION,"Incompatible graph module ABI.");}
    return *modules.emplace(path_,loaded).first->second.api;
}
bool Module::isLoaded() noexcept {std::lock_guard lock(moduleMutex);return !modules.empty();}
Result Module::solve(const language::OdeDefinition& definition,const Graph& graph,double gamma,const std::vector<double>& initial,const std::vector<double>& parameters,const Options& o,const ode::NativeOptions& native)const{
    Result result;result.nodes=graph.nodes;result.gamma=gamma;std::size_t parameterCount=0;for(const auto& state:definition.states){result.stateNames.push_back(state.name);const auto width=language::coordinateWidth(state.type,state.schema);result.stateWidths.push_back(width);result.coordinates+=width;}for(const auto& p:definition.parameters)parameterCount+=language::coordinateWidth(p.type,p.schema);
    const auto n=result.coordinates;if(!graph.nodes||graph.nodes>100000||n<1||n>32||graph.nodes>8000000/n||graph.endpoints.size()%2||graph.weights.size()!=graph.endpoints.size()/2||graph.weights.size()>500000||initial.size()!=graph.nodes*n||parameters.size()!=parameterCount||!std::isfinite(gamma)||gamma<0||gamma>1||(o.method!=1&&o.method!=2)||o.threads>64||o.maximumSamples<1||o.maximumSamples>100000||o.maximumCoordinates>8000000||o.maximumBytes<1024||o.maximumBytes>1024ull*1024*1024||!o.maximumWork||!o.maximumSteps||!std::isfinite(o.t0)||!std::isfinite(o.t1)||o.t1<o.t0||!std::isfinite(o.step)||(o.method==1?o.step<=0:o.step<0)||!std::isfinite(o.sampleStep)||o.sampleStep<=0||!std::isfinite(o.maximumSeconds)||o.maximumSeconds<=0)throw Error(PENTA_NUMERIC_ARGUMENT,"Invalid network input shape, time interval or storage/thread budget.");
    if(o.method==2&&(!std::isfinite(o.rtol)||o.rtol<=0||o.rtol>1||!std::isfinite(o.atol)||o.atol<=0||o.krylovDimension<1||o.krylovDimension>64))throw Error(PENTA_NUMERIC_ARGUMENT,"BDF tolerances or Krylov dimension are invalid.");
    for(const auto& values:{initial,parameters,graph.weights,o.diffusion,o.edgeOperators})for(double value:values)if(!std::isfinite(value))throw Error(PENTA_NUMERIC_ARGUMENT,"Network inputs must be finite.");
    if(!o.edgeOperators.empty()&&(!o.generalCoupling||graph.weights.size()>8000000/n/n||o.edgeOperators.size()!=graph.weights.size()*n*n))throw Error(PENTA_NUMERIC_ARGUMENT,"Ordered edge operators need explicit general coupling and one n-by-n block per physical edge.");
    auto diffusion=o.diffusion;if(diffusion.size()==1)diffusion.resize(n,diffusion[0]);if(diffusion.size()!=n)throw Error(PENTA_NUMERIC_ARGUMENT,"Diffusion must be a real scalar or contain every flat local coordinate.");if(!o.generalCoupling){for(double weight:graph.weights)if(weight<0)throw Error(PENTA_NUMERIC_ARGUMENT,"Negative real weights require coupling=general.");for(double coefficient:diffusion)if(coefficient<0)throw Error(PENTA_NUMERIC_ARGUMENT,"Negative diffusion requires coupling=general.");}
    const long double duration=static_cast<long double>(o.t1)-o.t0,intervals=std::floor(duration/o.sampleStep);if(intervals+2>o.maximumSamples+1)throw Error(PENTA_NUMERIC_ARGUMENT,"Network sample plan exceeds maximum samples.");
    for(std::size_t k=0;k<=static_cast<std::size_t>(intervals);++k){const auto time=static_cast<double>(static_cast<long double>(o.t0)+k*static_cast<long double>(o.sampleStep));if(!result.times.empty()&&time<=result.times.back())throw Error(PENTA_NUMERIC_ARGUMENT,"Network sample time cannot advance binary64 time.");result.times.push_back(time);}if(result.times.back()!=o.t1)result.times.push_back(o.t1);
    const auto samples=result.times.size(),count=n*graph.nodes;if(samples>o.maximumSamples||static_cast<long double>(samples)*(count+n+2)>o.maximumCoordinates)throw Error(PENTA_NUMERIC_ARGUMENT,"Network retained coefficient budget exceeded.");const long double bytes=sizeof(double)*(static_cast<long double>(samples)*(count+n+2)+(o.method==2?static_cast<long double>(o.krylovDimension+48)*count+2.L*count*n:8.L*count))+sizeof(uint64_t)*(graph.nodes+1)+32.L*graph.weights.size();if(bytes+o.edgeOperators.size()*sizeof(double)>o.maximumBytes)throw Error(PENTA_NUMERIC_ARGUMENT,"Network field/workspace memory estimate exceeds max_bytes.");
    ode::PreparedModel model(definition,native);result.key=model.key();result.cacheHit=model.cacheHit();result.values.resize(samples*count);result.means.resize(samples*n);result.synchronization.resize(samples);
    Host hostContext{model,gamma,parameters,native,{},result,{},{}};hostContext.settings.threads=o.threads;hostContext.settings.serialThreshold=o.threads==0?32768:1;result.parallel.requestedThreads=o.threads;result.parallel.effectiveThreads=runtime::ThreadBudget::backendThreadLimit(o.threads);
    penta_network_host_v1 host{sizeof(host),PENTA_NETWORK_ABI_V1,&hostContext,Host::cancelled,Host::account,Host::parallel,Host::local};penta_network_request_v1 request{sizeof(request),o.method,static_cast<uint32_t>(graph.directed),static_cast<uint32_t>(o.generalCoupling),graph.nodes,graph.weights.size(),n,samples,graph.endpoints.data(),graph.weights.data(),diffusion.data(),initial.data(),result.times.data(),o.step,o.maximumSeconds,o.maximumSteps,o.maximumWork,o.rtol,o.atol,o.krylovDimension};auto& r=result.report;r.struct_size=sizeof(r);r.values=result.values.data();r.means=result.means.data();r.synchronization=result.synchronization.data();r.value_capacity=result.values.size();r.mean_capacity=result.means.size();r.sample_capacity=samples;
    char error[2048]{};int status=0;runtime::ThreadBudget::instance().withReservation([&]{const auto& base=api();if(o.edgeOperators.empty())status=base.solve(&host,&request,&r,error,sizeof(error));else{
        const penta_network_operator_api_v1* ordered=nullptr;{
            std::lock_guard lock(moduleMutex);const auto& loaded=modules.at(path_);
#ifdef _WIN32
            const auto query=reinterpret_cast<penta_network_operator_query_function_v1>(reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(loaded.handle),"penta_network_operator_query_v1")));
#else
            const auto query=reinterpret_cast<penta_network_operator_query_function_v1>(dlsym(loaded.handle,"penta_network_operator_query_v1"));
#endif
            ordered=query?query(PENTA_NETWORK_OPERATOR_ABI_V1):nullptr;
        }if(!ordered||ordered->struct_size<sizeof(*ordered)||ordered->abi_version!=PENTA_NETWORK_OPERATOR_ABI_V1||!ordered->solve)throw Error(PENTA_NUMERIC_VERSION,"The network module lacks ordered-edge support.");status=ordered->solve(&host,&request,o.edgeOperators.data(),o.edgeOperators.size(),&r,error,sizeof(error));
    }});if(hostContext.failure)std::rethrow_exception(hostContext.failure);if(status)throw Error(status,error[0]?error:"Native network solve failed.");r.values=nullptr;r.means=nullptr;r.synchronization=nullptr;return result;
}
}
