#include "pde_module.hpp"
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
namespace penta::pde {
namespace{
struct Loaded{void* handle;const penta_pde_api_v1* api;};
std::mutex registryMutex;std::map<std::filesystem::path,Loaded> registry;
std::filesystem::path executable(){
#ifdef _WIN32
    std::vector<wchar_t> b(32768);const auto n=GetModuleFileNameW(nullptr,b.data(),static_cast<DWORD>(b.size()));if(!n||n>=b.size())throw Error(1,"Cannot find executable location for PDE module.");return std::filesystem::path(std::wstring(b.data(),n)).parent_path();
#else
    std::vector<char> b(32768);const auto n=readlink("/proc/self/exe",b.data(),b.size());if(n<=0||static_cast<std::size_t>(n)>=b.size())throw Error(1,"Cannot find executable location for PDE module.");return std::filesystem::path(std::string(b.data(),n)).parent_path();
#endif
}
struct Host{
    const std::shared_ptr<std::atomic_bool>& cancellation;
    const std::function<void(std::size_t)>& charge;
    std::exception_ptr failure;
    std::size_t requested=1,granted=1;
    runtime::ParallelStatistics statistics{};
    std::mutex chargeMutex{};
};
int cancelled(void* p){const auto& h=*static_cast<Host*>(p);return h.cancellation&&h.cancellation->load();}
int charge(void* p,uint64_t n,char* error,uint64_t capacity)noexcept{auto& h=*static_cast<Host*>(p);std::lock_guard lock(h.chargeMutex);try{if(h.charge)h.charge(n);return 0;}catch(const std::exception& e){if(!h.failure)h.failure=std::current_exception();if(error&&capacity){const auto k=std::min<uint64_t>(capacity-1,std::strlen(e.what()));std::memcpy(error,e.what(),k);error[k]=0;}return 1;}catch(...){if(!h.failure)h.failure=std::current_exception();return 1;}}
int parallelFor(void* raw,uint64_t count,uint64_t cost,penta_numeric_range_task_v1 task,void* data,char* error,uint64_t capacity)noexcept{
    auto& host=*static_cast<Host*>(raw);
    try{
        runtime::ParallelSettings settings;settings.threads=host.granted==1?1:host.requested;
        // Explicit limits request parallel execution of worthwhile PDE ranges;
        // automatic selection retains a modest work threshold to avoid tiny jobs.
        settings.serialThreshold=host.requested==0?16384:1;
        runtime::ParallelStatistics observed;std::string detail;
        const int status=runtime::ThreadBudget::instance().run(count,cost,task,data,settings,host.cancellation,observed,detail);
        host.statistics.threadsUsed=std::max(host.statistics.threadsUsed,observed.threadsUsed);
        host.statistics.peakActiveThreads=std::max(host.statistics.peakActiveThreads,observed.peakActiveThreads);
        host.statistics.parallelCalls+=observed.parallelCalls;host.statistics.serialCalls+=observed.serialCalls;
        host.statistics.lastElements=count;
        if(status&&error&&capacity){const auto length=std::min<uint64_t>(capacity-1,detail.size());std::memcpy(error,detail.data(),length);error[length]=0;}
        return status;
    }catch(const std::exception& failure){if(error&&capacity){const auto length=std::min<uint64_t>(capacity-1,std::strlen(failure.what()));std::memcpy(error,failure.what(),length);error[length]=0;}return PENTA_NUMERIC_INTERNAL;}
    catch(...){return PENTA_NUMERIC_INTERNAL;}
}
}
#include "pde_stiff_wrapper.inc"
Module::Module(std::filesystem::path directory){if(directory.empty())directory=executable()/"modules";if(!directory.is_absolute())throw Error(1,"PDE module directory must be an absolute host-controlled location.");
#ifdef _WIN32
path_=directory/"penta_pde.dll";
#else
path_=directory/"libpenta_pde.so";
#endif
}
const penta_pde_api_v1& Module::api()const{std::lock_guard lock(registryMutex);if(const auto it=registry.find(path_);it!=registry.end())return *it->second.api;
#ifdef _WIN32
    auto handle=LoadLibraryExW(path_.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);if(!handle)throw Error(1,"Cannot load penta_pde or a dependency (Windows error "+std::to_string(GetLastError())+").");const auto query=reinterpret_cast<penta_pde_query_function_v1>(reinterpret_cast<void*>(GetProcAddress(handle,"penta_pde_query_v1")));auto release=[&]{FreeLibrary(handle);};
#else
    auto handle=dlopen(path_.c_str(),RTLD_NOW|RTLD_LOCAL);if(!handle)throw Error(1,"Cannot load penta_pde or a dependency.");const auto query=reinterpret_cast<penta_pde_query_function_v1>(dlsym(handle,"penta_pde_query_v1"));auto release=[&]{dlclose(handle);};
#endif
    const auto* a=query?query(PENTA_PDE_ABI_V1):nullptr;if(!a||a->struct_size<sizeof(*a)||a->abi_version!=PENTA_PDE_ABI_V1||!a->solve){release();throw Error(1,"Incompatible PDE module ABI.");}registry.emplace(path_,Loaded{handle,a});return *a;
}
Result Module::solve(const penta_pde_request_v1& request,const std::shared_ptr<std::atomic_bool>& cancellation,const std::function<void(std::size_t)>& operationCharge,std::size_t threads)const{
    if(threads>runtime::ParallelSettings::maximumThreads)throw Error(1,"PDE threads must be 0 (automatic) or between 1 and 64.");
    if(request.nodes<3||request.nodes>4096||(request.components!=1&&request.components!=5)||!std::isfinite(request.t0)||!std::isfinite(request.t1)||!std::isfinite(request.sample_step)||request.t1<request.t0||request.sample_step<=0)throw Error(1,"Invalid PDE dimensions/time settings.");
    const double count=std::ceil((request.t1-request.t0)/request.sample_step)+2;
    if(!std::isfinite(count)||count>1000000||count*request.nodes*request.components>8000000)throw Error(1,"PDE retained-field coordinate budget exceeded; increase sample_step.");
    const auto samples=static_cast<std::size_t>(count);Result result;result.times.resize(samples);result.values.resize(samples*request.nodes*request.components);result.means.resize(samples*request.components);result.rms.resize(samples);penta_pde_time_result_v1 extended{};auto& r=extended.base;r.struct_size=sizeof(extended);r.times=result.times.data();r.values=result.values.data();r.means=result.means.data();r.spatial_rms=result.rms.data();r.sample_capacity=samples;r.value_capacity=result.values.size();
    const auto& a=api();
    const bool supportsParallel=a.struct_size>=sizeof(penta_pde_parallel_api_v1)&&
        (reinterpret_cast<const penta_pde_parallel_api_v1*>(&a)->capabilities&PENTA_PDE_CAP_PARALLEL_V1);
    if(request.struct_size>=sizeof(penta_pde_time_request_v1)&&reinterpret_cast<const penta_pde_time_request_v1*>(&request)->integrator==PENTA_PDE_IMEX_EULER_V1&&
        (a.struct_size<sizeof(penta_pde_parallel_api_v1)||!(reinterpret_cast<const penta_pde_parallel_api_v1*>(&a)->capabilities&PENTA_PDE_CAP_IMEX_EULER_V1)))throw Error(1,"Loaded PDE module lacks the IMEX Euler capability.");
    if(threads>1&&!supportsParallel)throw Error(1,"Loaded PDE module does not support parallel spatial execution.");
    Host context{cancellation,operationCharge,{}};context.requested=threads;
    context.granted=supportsParallel?runtime::ThreadBudget::backendThreadLimit(threads):1;
    context.statistics.requestedThreads=threads;context.statistics.effectiveThreads=context.granted;
    penta_pde_parallel_host_v1 host{};host.base={sizeof(host),PENTA_PDE_ABI_V1,&context,cancelled,charge};
    host.threads_requested=threads;host.threads_granted=context.granted;host.parallel_for=supportsParallel?parallelFor:nullptr;
    char error[2048]{};int status=0;
    runtime::ThreadBudget::instance().withReservation([&]{status=a.solve(&host.base,&request,&r,error,sizeof(error));});
    if(context.failure)std::rethrow_exception(context.failure);
    if(status)throw Error(cancelled(&context)?3:1,error[0]?error:"Native PDE solve failed.");
    result.times.resize(r.samples);result.values.resize(r.samples*request.nodes*request.components);result.means.resize(r.samples*request.components);result.rms.resize(r.samples);
    result.report=r;result.report.struct_size=sizeof(result.report);
    result.linearSolves=extended.linear_solves;result.factorizations=extended.factorizations;
    result.statistics=context.statistics;result.threadsGranted=context.granted;return result;
}
Result Module::solveGrid(const penta_pde_grid_request_v1& request,const std::shared_ptr<std::atomic_bool>& cancellation,const std::function<void(std::size_t)>& operationCharge,std::size_t threads)const{
    if(threads>runtime::ParallelSettings::maximumThreads)throw Error(1,"PDE threads must be 0 (automatic) or between 1 and 64.");
    if(request.nx<3||request.ny<3||request.nx>256||request.ny>256||request.nx>16384/request.ny||(request.components!=1&&request.components!=5)||!std::isfinite(request.t0)||!std::isfinite(request.t1)||!std::isfinite(request.sample_step)||request.t1<request.t0||request.sample_step<=0)throw Error(1,"Invalid two-dimensional PDE dimensions/time settings.");
    const auto nodes=request.nx*request.ny;const double count=std::ceil((request.t1-request.t0)/request.sample_step)+2;
    if(!std::isfinite(count)||count>1000000||count*nodes*request.components>8000000)throw Error(1,"PDE retained-field coordinate budget exceeded; increase sample_step.");
    const auto& a=api();(void)a;const penta_pde_grid_api_v1* grid=nullptr;
    {
        std::lock_guard lock(registryMutex);const auto handle=registry.at(path_).handle;
#ifdef _WIN32
        const auto query=reinterpret_cast<penta_pde_grid_query_function_v1>(reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(handle),"penta_pde_grid_query_v1")));
#else
        const auto query=reinterpret_cast<penta_pde_grid_query_function_v1>(dlsym(handle,"penta_pde_grid_query_v1"));
#endif
        grid=query?query(PENTA_PDE_GRID_ABI_V1):nullptr;
    }
    if(!grid||grid->struct_size<sizeof(*grid)||grid->abi_version!=PENTA_PDE_GRID_ABI_V1||!grid->solve)throw Error(1,"Loaded PDE module lacks the compatible structured-grid capability.");
    const auto samples=static_cast<std::size_t>(count);Result result;result.times.resize(samples);result.values.resize(samples*nodes*request.components);result.means.resize(samples*request.components);result.rms.resize(samples);
    penta_pde_time_result_v1 extended{};auto& r=extended.base;r.struct_size=sizeof(extended);r.times=result.times.data();r.values=result.values.data();r.means=result.means.data();r.spatial_rms=result.rms.data();r.sample_capacity=samples;r.value_capacity=result.values.size();
    Host context{cancellation,operationCharge,{}};context.requested=threads;context.granted=runtime::ThreadBudget::backendThreadLimit(threads);context.statistics.requestedThreads=threads;context.statistics.effectiveThreads=context.granted;
    penta_pde_parallel_host_v1 host{};host.base={sizeof(host),PENTA_PDE_ABI_V1,&context,cancelled,charge};host.threads_requested=threads;host.threads_granted=context.granted;host.parallel_for=parallelFor;
    char error[2048]{};int status=0;runtime::ThreadBudget::instance().withReservation([&]{status=grid->solve(&host.base,&request,&extended,error,sizeof(error));});
    if(context.failure)std::rethrow_exception(context.failure);
    if(status)throw Error(cancelled(&context)?3:1,error[0]?error:"Native structured-grid PDE solve failed.");
    result.times.resize(r.samples);result.values.resize(r.samples*nodes*request.components);result.means.resize(r.samples*request.components);result.rms.resize(r.samples);
    result.report=r;result.report.struct_size=sizeof(result.report);result.linearSolves=extended.linear_solves;result.factorizations=extended.factorizations;result.statistics=context.statistics;result.threadsGranted=context.granted;return result;
}
Mesh Module::geometry(const penta_geometry_request_v1& request,std::size_t maximumPoints,const std::shared_ptr<std::atomic_bool>& cancellation,const std::function<void(std::size_t)>& operationCharge)const{
    const auto& checked=api();(void)checked;penta_geometry_build_v1 build=nullptr;
    {
        std::lock_guard lock(registryMutex);const auto handle=registry.at(path_).handle;
#ifdef _WIN32
        using Query=penta_geometry_build_v1(*)(uint32_t);const auto query=reinterpret_cast<Query>(reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(handle),"penta_geometry_query_v1")));
#else
        using Query=penta_geometry_build_v1(*)(uint32_t);const auto query=reinterpret_cast<Query>(dlsym(handle,"penta_geometry_query_v1"));
#endif
        build=query?query(PENTA_PDE_ABI_V1):nullptr;
    }
    if(!build)throw Error(1,"PDE module lacks the compatible geometry capability.");
    if(request.nx<2||request.ny<2||request.nx>1000000/request.ny||(request.operation==3&&(request.nz<2||request.nx*request.ny>1000000/request.nz)))throw Error(1,"Geometry dimensions exceed the sample budget.");
    const auto cells=(request.nx-1)*(request.ny-1)*(request.operation==3?request.nz-1:1);
    const auto estimated=request.operation<=1?request.nx*request.ny:request.operation==2?cells*4*request.level_count:cells*36;
    const auto count=std::min<std::size_t>({static_cast<std::size_t>(estimated),maximumPoints,1000000});
    Mesh mesh;mesh.xyz.resize(3*count);mesh.scalars.resize(count);mesh.faces.resize(request.operation==2?0:6*count);mesh.edges.resize(request.operation==2?count:0);
    penta_geometry_result_v1 out{};out.struct_size=sizeof(out);out.point_capacity=count;out.triangle_capacity=mesh.faces.size()/3;out.segment_capacity=mesh.edges.size()/2;out.xyz=mesh.xyz.data();out.scalars=mesh.scalars.data();out.faces=mesh.faces.data();out.edges=mesh.edges.data();
    Host context{cancellation,operationCharge,{}};penta_pde_host_v1 host{sizeof(host),PENTA_PDE_ABI_V1,&context,cancelled,charge};char error[2048]{};int status=0;runtime::ThreadBudget::instance().withReservation([&]{status=build(&host,&request,&out,error,sizeof(error));});if(context.failure)std::rethrow_exception(context.failure);if(status)throw Error(cancelled(&context)?3:1,error[0]?error:"Native geometry failed.");mesh.xyz.resize(out.points*3);mesh.scalars.resize(out.points);mesh.faces.resize(out.triangles*3);mesh.edges.resize(out.segments*2);return mesh;
}
}
