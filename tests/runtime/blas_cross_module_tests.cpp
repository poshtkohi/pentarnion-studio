#include "modules/blas_coordinator_api.h"
#include "modules/linalg_api.h"
#include "modules/petsc_api.h"
#include "runtime/thread_budget.hpp"
#include <cblas.h>
#if defined(_OPENMP)
#include <omp.h>
#endif
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <future>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif
namespace {
unsigned checks=0,failures=0;void verify(bool value,const char* message){++checks;if(!value){++failures;std::cerr<<"FAIL "<<message<<'\n';}}
const auto* coordinator=penta_blas_coordinator_query_v1(PENTA_BLAS_COORDINATOR_ABI_V1);
penta_blas_status_v1 status(){penta_blas_status_v1 value{};value.struct_size=sizeof(value);char error[256]{};if(coordinator->status(&value,error,sizeof(error)))throw std::runtime_error(error);return value;}
template<class Predicate>bool until(Predicate predicate){const auto limit=std::chrono::steady_clock::now()+std::chrono::seconds(10);while(!predicate()){if(std::chrono::steady_clock::now()>limit)return false;std::this_thread::sleep_for(std::chrono::milliseconds(1));}return true;}
template<class Query>Query load(const std::filesystem::path& directory,const char* windows,const char* linuxName,const char* symbol){
#ifdef _WIN32
    (void)linuxName;const auto handle=LoadLibraryExW((directory/windows).c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);if(!handle)throw std::runtime_error("Cannot load test feature DLL");return reinterpret_cast<Query>(reinterpret_cast<void*>(GetProcAddress(handle,symbol)));
#else
    (void)windows;const auto handle=dlopen((directory/linuxName).c_str(),RTLD_NOW|RTLD_LOCAL);if(!handle)throw std::runtime_error(dlerror());return reinterpret_cast<Query>(dlsym(handle,symbol));
#endif
    // Production module registries retain handles until process shutdown too.
}
bool callerTeamIsOne(){
#if defined(_OPENMP)
    if(openblas_get_parallel()==OPENBLAS_OPENMP){int team=0;
#pragma omp parallel reduction(+:team)
        {team+=1;}
        return omp_get_max_threads()==1&&team==1;}
#endif
    return true;
}
struct Shared{const penta_linalg_api_v1* linalg=nullptr;const penta_petsc_api_v1* petsc=nullptr;std::shared_ptr<std::atomic_bool> cancel=std::make_shared<std::atomic_bool>(false);std::atomic_bool linalgEntered{false},releaseInitial{false},petscEntered{false},releasePetsc{false},capped{true},overlap{false},nestedRejected{false},correct{true};std::atomic_uint64_t evaluations{0};penta::runtime::ParallelStatistics outerStats;double root=0,residual=1;int outerStatus=-1;std::string outerError;
    static int cancelled(void* raw){return static_cast<Shared*>(raw)->cancel->load();}static int charge(void*,uint64_t,char*,uint64_t){return 0;}static int parallel(void* raw,uint64_t count,uint64_t cost,penta_numeric_range_task_v1 task,void* data,char* error,uint64_t cap){auto& self=*static_cast<Shared*>(raw);penta::runtime::ParallelStatistics stats;std::string message;const auto result=penta::runtime::ThreadBudget::instance().run(count,cost,task,data,{1,1},self.cancel,stats,message);if(error&&cap){std::strncpy(error,message.c_str(),cap-1);error[cap-1]=0;}return result;}
    static int native(void* raw,const double* x,uint64_t,double* f,double* jac,char*,uint64_t){auto& self=*static_cast<Shared*>(raw);if(openblas_get_num_threads()!=1||status().configured_threads!=1||!callerTeamIsOne())self.capped.store(false);if(self.evaluations.fetch_add(1)==0){self.petscEntered.store(true);if(!until([&]{return self.releasePetsc.load()||self.cancel->load();}))return 1;}f[0]=x[0]*x[0]-4;if(jac)jac[0]=2*x[0];return 0;}
    penta_petsc_request_v1 request(const double* initial){penta_petsc_request_v1 value{};value.struct_size=sizeof(value);value.operation=PENTA_PETSC_NONLINEAR;value.method=PENTA_PETSC_GMRES;value.preconditioner=PENTA_PETSC_JACOBI;value.dimension=1;value.max_iterations=100;value.max_evaluations=1000;value.max_work=1000000;value.max_bytes=16*1024*1024;value.rtol=1e-12;value.atol=1e-12;value.step_tolerance=1e-12;value.max_seconds=20;value.initial=initial;value.evaluate=native;value.callback_context=this;return value;}
    static int observer(void* raw,const penta_iteration_snapshot_v1*,char*,uint64_t){auto& self=*static_cast<Shared*>(raw);double initial=3,x=0,f=0;auto q=self.request(&initial);penta_petsc_host_v1 host{sizeof(host),PENTA_PETSC_ABI_V1,&self,cancelled,charge,parallel,nullptr,1};penta_petsc_result_v1 result{};result.struct_size=sizeof(result);result.capacity=1;result.solution=&x;result.residual=&f;char error[256]{};if(self.petsc->solve(&host,&q,&result,error,sizeof(error))==PENTA_NUMERIC_ARGUMENT)self.nestedRejected.store(true);return 0;}
    void solve(){double initial=3;auto q=request(&initial);penta_petsc_host_v1 host{sizeof(host),PENTA_PETSC_ABI_V1,this,cancelled,charge,parallel,observer,1};penta_petsc_result_v1 result{};result.struct_size=sizeof(result);result.capacity=1;result.solution=&root;result.residual=&residual;char error[1024]{};const auto code=petsc->solve(&host,&q,&result,error,sizeof(error));if(code||!result.converged){correct.store(false);std::cerr<<"PETSc integration: "<<error<<'\n';}}
};
struct LinalgHost{Shared& shared;bool first;static inline thread_local LinalgHost* inside=nullptr;static inline thread_local bool observed=false;
    static int cancelled(void* raw){auto& self=*static_cast<LinalgHost*>(raw);if(inside==&self&&!observed){const auto epoch=status();if(epoch.active_owners>=(self.first?1u:2u)&&epoch.configured_threads==1){observed=true;if(!callerTeamIsOne())self.shared.capped.store(false);if(self.first){self.shared.linalgEntered.store(true);if(!until([&]{return self.shared.releaseInitial.load()||self.shared.cancel->load();}))self.shared.cancel->store(true);}else self.shared.overlap.store(openblas_get_num_threads()==1);}}return self.shared.cancel->load();}
    static int parallel(void* raw,uint64_t count,uint64_t cost,penta_numeric_range_task_v1 task,void* data,char* error,uint64_t cap){auto& self=*static_cast<LinalgHost*>(raw);struct Work{LinalgHost& host;penta_numeric_range_task_v1 body;void* context;}work{self,task,data};const auto run=+[](void* raw,uint64_t first,uint64_t last,char* error,uint64_t cap)->int{auto& item=*static_cast<Work*>(raw);inside=&item.host;observed=false;const auto result=item.body(item.context,first,last,error,cap);inside=nullptr;return result;};penta::runtime::ParallelStatistics statistics;std::string message;const auto result=penta::runtime::ThreadBudget::instance().run(count,cost,run,&work,{1,1},self.shared.cancel,statistics,message);if(error&&cap){std::strncpy(error,message.c_str(),cap-1);error[cap-1]=0;}return result;}

    void product(){constexpr unsigned n=64;std::vector<double> a(n*n),b(n*n,0),c(n*n,0);for(unsigned row=0;row<n;++row)for(unsigned col=0;col<n;++col)a[row*n+col]=(double(row)-col)*.03125;for(unsigned row=0;row<n;++row)b[row*n+row]=2;penta_linalg_request_v1 q{};q.struct_size=sizeof(q);q.operation=PENTA_LA_MULTIPLY;q.threads=1;q.batches=1;q.gamma=1;q.residual_tolerance=1e-10;q.a={n,n,n*n,1,0,a.data()};q.b={n,n,n*n,1,0,b.data()};penta_linalg_result_v1 result{};result.struct_size=sizeof(result);result.output[0]={c.data(),n*n,0,0,0,0};penta_numeric_host_v1 host{sizeof(host),PENTA_NUMERIC_ABI_V1,this,cancelled,parallel};char error[1024]{};if(shared.linalg->compute(&host,&q,&result,error,sizeof(error))||result.backend_threads!=1){shared.correct.store(false);std::cerr<<"Linalg integration: "<<error<<'\n';}for(unsigned at=0;at<n*n;++at)if(c[at]!=2*a[at])shared.correct.store(false);}
};
int tasks(void* raw,uint64_t first,uint64_t last,char* error,uint64_t capacity)noexcept{auto& shared=*static_cast<Shared*>(raw);try{for(auto at=first;at<last;++at){if(!penta::runtime::ThreadBudget::inNumericalTask())return 4;if(at==0){LinalgHost{shared,true}.product();if(!until([&]{return shared.petscEntered.load()||shared.cancel->load();}))return 3;LinalgHost{shared,false}.product();shared.releasePetsc.store(true);}else{if(!until([&]{return shared.linalgEntered.load()||shared.cancel->load();}))return 3;shared.solve();}}return 0;}catch(const std::exception& failure){if(error&&capacity){std::strncpy(error,failure.what(),capacity-1);error[capacity-1]=0;}return 4;}catch(...){return 4;}}
}
int main(int argc,char** argv){try{if(openblas_get_parallel()==OPENBLAS_SEQUENTIAL){std::cout<<"Cross-module overlap requires a threaded OpenBLAS backend.\n";return 77;}const auto directory=argc>1?std::filesystem::absolute(argv[1]):std::filesystem::absolute(argv[0]).parent_path()/"modules";Shared shared;const auto linalg=load<penta_linalg_query_function_v1>(directory,"penta_linalg.dll","libpenta_linalg.so","penta_linalg_query_v1");const auto petsc=load<penta_petsc_query_function_v1>(directory,"penta_petsc.dll","libpenta_petsc.so","penta_petsc_query_v1");if(!linalg||!petsc)throw std::runtime_error("Missing dynamically loaded module ABI");shared.linalg=linalg(PENTA_LINALG_ABI_V1);shared.petsc=petsc(PENTA_PETSC_ABI_V1);const auto previous=openblas_get_num_threads();const auto* environment=std::getenv("OPENBLAS_NUM_THREADS");verify(environment&&(std::string(environment)=="4"||std::string(environment)=="8"),"Regression runs under an explicit environment BLAS thread override");auto run=std::async(std::launch::async,[&]{shared.outerStatus=penta::runtime::ThreadBudget::instance().run(2,2048,tasks,&shared,{2,1},shared.cancel,shared.outerStats,shared.outerError);});verify(until([&]{return shared.linalgEntered.load();})&&status().active_owners==1,"A real linalg DLL/SO call owns the shared epoch");verify(until([]{return status().waiting_leases>0;})&&openblas_get_num_threads()==1,"First PETSc initialization waits for active linalg work instead of resetting its pool");shared.releaseInitial.store(true);const bool ready=run.wait_for(std::chrono::seconds(15))==std::future_status::ready;if(!ready){shared.cancel->store(true);shared.releaseInitial.store(true);shared.releasePetsc.store(true);}run.get();verify(ready&&shared.outerStatus==0&&shared.correct.load(),"Cross-module nested numerical workers complete with correct independent buffers");if(shared.outerStatus)std::cerr<<shared.outerError<<'\n';verify(shared.capped.load()&&shared.overlap.load(),"PETSc env-reset is capped to1 and compatible linalg work actually overlaps");verify(shared.outerStats.peakActiveThreads==2&&shared.outerStats.threadsUsed==2,"Both modules obey the existing two-worker CPU reservation");verify(std::abs(shared.root-2)<1e-11&&std::abs(shared.residual)<1e-11,"PETSc math and original residual survive shared configuration control");verify(shared.nestedRejected.load(),"Reentrant PETSc iteration calls reject explicitly instead of deadlocking");verify(status().active_leases==0&&status().waiting_leases==0&&openblas_get_num_threads()==previous,"Module lifetimes leave no lease or external configuration residue");std::cout<<checks<<" cross-module BLAS checks, "<<failures<<" failures; OPENBLAS_NUM_THREADS="<<(environment?environment:"")<<'\n';return failures?1:0;}catch(const std::exception& failure){std::cerr<<failure.what()<<'\n';return 1;}}
