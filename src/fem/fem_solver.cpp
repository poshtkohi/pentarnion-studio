#include "fem_solver.hpp"
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

namespace penta::fem {
namespace {
struct Loaded {void* handle=nullptr;const penta_fem_api_v1* api=nullptr;};
std::mutex moduleMutex;
std::map<std::filesystem::path,Loaded> modules;
std::filesystem::path executableDirectory() {
#ifdef _WIN32
    std::vector<wchar_t> value(32768);const auto count=GetModuleFileNameW(nullptr,value.data(),static_cast<DWORD>(value.size()));
    if(!count||count>=value.size())throw Error(PENTA_NUMERIC_INTERNAL,"Cannot locate FEM module executable directory.");
    return std::filesystem::path(std::wstring(value.data(),count)).parent_path();
#else
    std::vector<char> value(32768);const auto count=readlink("/proc/self/exe",value.data(),value.size());
    if(count<=0||static_cast<std::size_t>(count)>=value.size())throw Error(PENTA_NUMERIC_INTERNAL,"Cannot locate FEM module executable directory.");
    return std::filesystem::path(std::string(value.data(),static_cast<std::size_t>(count))).parent_path();
#endif
}
struct Host {
    const std::shared_ptr<std::atomic_bool>* cancellation;
    const std::function<void(std::size_t)>* charge;
    std::exception_ptr failure;
    runtime::ParallelSettings settings;runtime::ParallelStatistics statistics;std::size_t granted=1;
    static int cancelled(void* raw) noexcept {
        const auto& self=*static_cast<Host*>(raw);
        return *self.cancellation&&(*self.cancellation)->load(std::memory_order_relaxed);
    }
    static int account(void* raw,uint64_t amount,char* error,uint64_t capacity) noexcept {
        auto& self=*static_cast<Host*>(raw);
        try {if(*self.charge)(*self.charge)(static_cast<std::size_t>(amount));return 0;}
        catch(const std::exception& exception) {
            self.failure=std::current_exception();
            if(error&&capacity){const auto count=std::min<std::size_t>(capacity-1,std::strlen(exception.what()));std::memcpy(error,exception.what(),count);error[count]=0;}
            return 1;
        } catch(...) {self.failure=std::current_exception();return 1;}
    }
    static int parallel(void* raw,uint64_t count,uint64_t cost,penta_numeric_range_task_v1 task,void* data,char* error,uint64_t capacity)noexcept{auto& self=*static_cast<Host*>(raw);try{runtime::ParallelStatistics observed;std::string detail;const auto status=runtime::ThreadBudget::instance().run(count,cost,task,data,self.settings,*self.cancellation,observed,detail);self.statistics.parallelCalls+=observed.parallelCalls;self.statistics.serialCalls+=observed.serialCalls;self.statistics.threadsUsed=std::max(self.statistics.threadsUsed,observed.threadsUsed);self.statistics.peakActiveThreads=std::max(self.statistics.peakActiveThreads,observed.peakActiveThreads);if(error&&capacity){const auto length=std::min<uint64_t>(capacity-1,detail.size());std::memcpy(error,detail.data(),length);error[length]=0;}return status;}catch(const std::exception& failure){if(error&&capacity){const auto length=std::min<uint64_t>(capacity-1,std::strlen(failure.what()));std::memcpy(error,failure.what(),length);error[length]=0;}return PENTA_NUMERIC_INTERNAL;}catch(...){return PENTA_NUMERIC_INTERNAL;}}
};
}

Module::Module(std::filesystem::path directory) {
    if(directory.empty())directory=executableDirectory()/"modules";
    if(!directory.is_absolute())throw Error(PENTA_NUMERIC_ARGUMENT,"FEM module directory must be absolute.");
#ifdef _WIN32
    path_=directory/"penta_fem.dll";
#else
    path_=directory/"libpenta_fem.so";
#endif
}
const penta_fem_api_v1& Module::api() const {
    std::lock_guard lock(moduleMutex);
    if(const auto found=modules.find(path_);found!=modules.end())return *found->second.api;
    Loaded loaded;
#ifdef _WIN32
    loaded.handle=LoadLibraryExW(path_.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if(!loaded.handle)throw Error(PENTA_NUMERIC_INTERNAL,"Cannot load optional penta_fem module or its dependencies.");
    const auto query=reinterpret_cast<penta_fem_query_function_v1>(reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(loaded.handle),"penta_fem_query_v1")));
    auto release=[&]{FreeLibrary(static_cast<HMODULE>(loaded.handle));};
#else
    loaded.handle=dlopen(path_.c_str(),RTLD_NOW|RTLD_LOCAL);
    if(!loaded.handle)throw Error(PENTA_NUMERIC_INTERNAL,"Cannot load optional penta_fem module or its dependencies.");
    const auto query=reinterpret_cast<penta_fem_query_function_v1>(dlsym(loaded.handle,"penta_fem_query_v1"));
    auto release=[&]{dlclose(loaded.handle);};
#endif
    loaded.api=query?query(PENTA_FEM_ABI_V1):nullptr;
    if(!loaded.api||loaded.api->struct_size<sizeof(penta_fem_api_v1)||loaded.api->abi_version!=PENTA_FEM_ABI_V1||!loaded.api->solve){release();throw Error(PENTA_NUMERIC_VERSION,"Incompatible FEM module ABI.");}
    return *modules.emplace(path_,loaded).first->second.api;
}
bool Module::isLoaded() noexcept {std::lock_guard lock(moduleMutex);return !modules.empty();}
Result Module::solve(penta_fem_request_v1 request,const std::shared_ptr<std::atomic_bool>& cancellation,const std::function<void(std::size_t)>& charge)const{return solveImpl(request,SolverOptions{1,false,0,1e-10},cancellation,charge);}
Result Module::solveMean(penta_fem_request_v1 request,double targetMean,double compatibilityTolerance,const std::shared_ptr<std::atomic_bool>& cancellation,const std::function<void(std::size_t)>& charge)const{return solveImpl(request,SolverOptions{1,true,targetMean,compatibilityTolerance},cancellation,charge);}
Result Module::solveConfigured(penta_fem_request_v1 request,const SolverOptions& settings,const std::shared_ptr<std::atomic_bool>& cancellation,const std::function<void(std::size_t)>& charge)const{return solveImpl(request,settings,cancellation,charge);}
Result Module::solveImpl(penta_fem_request_v1 request,const SolverOptions& settings,const std::shared_ptr<std::atomic_bool>& cancellation,const std::function<void(std::size_t)>& charge)const{
    const auto mean=settings.meanConstraint;const auto targetMean=settings.targetMean,compatibilityTolerance=settings.compatibilityTolerance;
    if(settings.threads>runtime::ParallelSettings::maximumThreads)throw Error(PENTA_NUMERIC_ARGUMENT,"FEM threads requires0(auto)..64.");
    request.struct_size=sizeof(request);
    if(request.order<1||request.order>4||request.vertices>100000||request.triangles>100000||request.visualization_refinement<1||request.visualization_refinement>16)throw Error(PENTA_NUMERIC_ARGUMENT,"FEM dimensions/order exceed native bounds.");
    const uint64_t p=request.order,n=request.visualization_refinement;
    const uint64_t dofs=request.vertices+3*request.triangles*(p-1)+(p>=3?request.triangles*(p-1)*(p-2)/2:0);
    const uint64_t vertices=request.triangles*(n+1)*(n+2)/2,triangles=request.triangles*n*n;
    const uint64_t local=(p+1)*(p+2)/2;
    const uint64_t estimate=65536+request.vertices*160+request.triangles*(512+local*local*40)+dofs*128+vertices*24+triangles*24+(mean?request.triangles*local*local*16+dofs*48:0);
    if(estimate>request.max_bytes||estimate>1073741824)throw Error(7,"FEM estimated assembly/output memory exceeds its explicit budget.");
    Result result;result.coordinates.resize(2*dofs);result.values.resize(dofs);result.visualizationCoordinates.resize(2*vertices);result.visualizationValues.resize(vertices);result.visualizationFaces.resize(3*triangles);
    auto& r=result.report;r.struct_size=sizeof(r);r.dof_capacity=dofs;r.vertex_capacity=vertices;r.triangle_capacity=triangles;r.dof_coordinates=result.coordinates.data();r.dof_values=result.values.data();r.visualization_coordinates=result.visualizationCoordinates.data();r.visualization_values=result.visualizationValues.data();r.visualization_faces=result.visualizationFaces.data();
    Host context{&cancellation,&charge,{},runtime::ParallelSettings{},runtime::ParallelStatistics{},1};const auto& baseApi=api();const bool parallel=baseApi.struct_size>=sizeof(penta_fem_parallel_api_v1)&&(reinterpret_cast<const penta_fem_parallel_api_v1*>(&baseApi)->capabilities&PENTA_FEM_CAP_PARALLEL_CG_V1);if(settings.threads>1&&!parallel)throw Error(PENTA_NUMERIC_VERSION,"Loaded FEM module lacks the shared-budget parallel-CG capability.");context.settings.threads=settings.threads;context.settings.serialThreshold=settings.threads==0?32768:1;context.granted=parallel?runtime::ThreadBudget::backendThreadLimit(settings.threads):1;context.statistics.requestedThreads=settings.threads;context.statistics.effectiveThreads=context.granted;penta_fem_parallel_host_v1 host{};host.base={sizeof(host),PENTA_FEM_ABI_V1,&context,Host::cancelled,Host::account};host.threads_requested=settings.threads;host.threads_granted=context.granted;host.parallel_for=parallel?Host::parallel:nullptr;char error[1024]{};int status=0;
    const penta_fem_mean_api_v1* capability=nullptr;if(mean){(void)api();std::lock_guard lock(moduleMutex);const auto handle=modules.at(path_).handle;
#ifdef _WIN32
        const auto query=reinterpret_cast<penta_fem_mean_query_function_v1>(reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(handle),"penta_fem_mean_query_v1")));
#else
        const auto query=reinterpret_cast<penta_fem_mean_query_function_v1>(dlsym(handle,"penta_fem_mean_query_v1"));
#endif
        capability=query?query(PENTA_FEM_MEAN_ABI_V1):nullptr;if(!capability||capability->struct_size<sizeof(*capability)||capability->abi_version!=PENTA_FEM_MEAN_ABI_V1||!capability->solve)throw Error(PENTA_NUMERIC_VERSION,"Loaded FEM module lacks the compatible mean-constraint capability.");}
    // Reserve the shared CPU allowance before the module's MFEM configuration lock.
    runtime::ThreadBudget::instance().withReservation([&]{if(mean){penta_fem_mean_request_v1 supplied{request,PENTA_FEM_MEAN_ABI_V1,0,targetMean,compatibilityTolerance};supplied.base.struct_size=sizeof(supplied);penta_fem_mean_result_v1 output{};output.base=r;output.base.struct_size=sizeof(output);status=capability->solve(&host.base,&supplied,&output,error,sizeof(error));r=output.base;r.struct_size=sizeof(r);result.meanConstraint=true;result.nullspaceComponents=output.nullspace_components;result.compatibilityResidual=output.compatibility_residual;result.meanError=output.maximum_mean_error;}else status=api().solve(&host.base,&request,&r,error,sizeof(error));});
    if(context.failure)std::rethrow_exception(context.failure);
    if(status)throw Error(status,error[0]?error:"Native FEM solve failed.");
    if(r.dofs>dofs||r.visualization_vertices>vertices||r.visualization_triangles>triangles)throw Error(PENTA_NUMERIC_VERSION,"FEM module returned invalid output dimensions.");
    result.coordinates.resize(2*r.dofs);result.values.resize(r.dofs);result.visualizationCoordinates.resize(2*r.visualization_vertices);result.visualizationValues.resize(r.visualization_vertices);result.visualizationFaces.resize(3*r.visualization_triangles);
    r.dof_coordinates=r.dof_values=r.visualization_coordinates=r.visualization_values=nullptr;r.visualization_faces=nullptr;result.statistics=context.statistics;result.threadsGranted=context.granted;return result;
}
#include "fem_system_wrapper.inc"
#include "fem_time_wrapper.inc"
#include "fem_mixed_wrapper.inc"
}
