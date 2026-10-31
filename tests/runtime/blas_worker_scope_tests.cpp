#include "modules/linalg_api.h"
#include "modules/blas_coordinator_scope.hpp"
#include "runtime/thread_budget.hpp"
#include <cblas.h>
#include <atomic>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <future>
#include <iostream>
#include <thread>
#include <vector>
#if defined(_OPENMP)
#include <omp.h>
#endif
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif
namespace {
unsigned checks=0,failures=0;void expect(bool okay,const char* text){++checks;if(!okay){++failures;std::cerr<<"FAIL "<<text<<'\n';}}
const auto* coordinator=penta_blas_coordinator_query_v1(PENTA_BLAS_COORDINATOR_ABI_V1);
penta_blas_status_v1 state(){penta_blas_status_v1 status{};status.struct_size=sizeof(status);char message[512]{};if(coordinator->status(&status,message,sizeof(message)))throw std::runtime_error(message);return status;}
template<class Predicate>bool until(Predicate predicate){const auto limit=std::chrono::steady_clock::now()+std::chrono::seconds(5);while(!predicate()){if(std::chrono::steady_clock::now()>limit)return false;std::this_thread::sleep_for(std::chrono::milliseconds(1));}return true;}
struct Host{
    std::shared_ptr<std::atomic_bool> cancellation=std::make_shared<std::atomic_bool>(false);std::atomic_bool held{false},release{false},capped{true},restored{true};std::atomic_uint workers{0};penta::runtime::ParallelStatistics statistics;static inline thread_local Host* inside=nullptr;static inline thread_local bool observed=false;
    static int cancelled(void* raw){auto& self=*static_cast<Host*>(raw);
#if defined(_OPENMP)
        if(inside==&self&&!observed&&omp_get_max_threads()==1){observed=true;const auto status=state();int count=0;
#pragma omp parallel reduction(+:count)
            {count+=1;}
            if(count!=1||status.configured_threads!=1)self.capped.store(false);
            self.workers.fetch_add(1);if(!self.held.exchange(true)&&!until([&]{return self.release.load()||self.cancellation->load();}))self.cancellation->store(true);}
#endif
        return self.cancellation->load()?1:0;}
    static int parallel(void* raw,uint64_t count,uint64_t cost,penta_numeric_range_task_v1 callback,void* context,char* error,uint64_t capacity){auto& self=*static_cast<Host*>(raw);struct Work{Host& host;penta_numeric_range_task_v1 body;void* context;} work{self,callback,context};auto task=+[](void* raw,uint64_t begin,uint64_t end,char* error,uint64_t capacity)->int{auto& job=*static_cast<Work*>(raw);
#if defined(_OPENMP)
            const int previous=omp_get_max_threads();omp_set_num_threads(8);
#endif
            inside=&job.host;observed=false;const auto result=job.body(job.context,begin,end,error,capacity);inside=nullptr;
#if defined(_OPENMP)
            if(omp_get_max_threads()!=8)job.host.restored.store(false);
            omp_set_num_threads(previous);
#endif
            return result;};std::string message;const auto result=penta::runtime::ThreadBudget::instance().run(count,cost,task,&work,{2,1},self.cancellation,self.statistics,message);if(error&&capacity){const auto n=std::min<std::size_t>(capacity-1,message.size());std::copy_n(message.data(),n,error);error[n]=0;}return result;}
};
}
int main(int argc,char** argv){
#if !defined(_OPENMP)
    (void)argc;(void)argv;std::cout<<"OpenMP worker ICV regression requires matching compiler OpenMP support.\n";return 77;
#else
    try{if(openblas_get_parallel()!=OPENBLAS_OPENMP){std::cout<<"OpenMP worker ICV regression only applies to the OpenMP BLAS backend.\n";return 77;}const auto directory=argc>1?std::filesystem::absolute(argv[1]):std::filesystem::absolute(argv[0]).parent_path()/"modules";
#ifdef _WIN32
        const auto module=LoadLibraryExW((directory/"penta_linalg.dll").c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);const auto query=module?reinterpret_cast<penta_linalg_query_function_v1>(reinterpret_cast<void*>(GetProcAddress(module,"penta_linalg_query_v1"))):nullptr;
#else
        const auto module=dlopen((directory/"libpenta_linalg.so").c_str(),RTLD_NOW|RTLD_LOCAL);const auto query=module?reinterpret_cast<penta_linalg_query_function_v1>(dlsym(module,"penta_linalg_query_v1")):nullptr;
#endif
        if(!query)throw std::runtime_error("Cannot load the real linalg module for worker ICV regression.");
        const auto* api=query(PENTA_LINALG_ABI_V1);Host host;constexpr unsigned n=128;std::vector<double> a(n*n),b(n*n,0),output(n*n,0);for(unsigned r=0;r<n;++r)for(unsigned c=0;c<n;++c)a[r*n+c]=double(r)-c;for(unsigned r=0;r<n;++r)b[r*n+r]=2;
        penta_linalg_request_v1 request{};request.struct_size=sizeof(request);request.operation=PENTA_LA_MULTIPLY;request.threads=2;request.batches=1;request.gamma=1;request.residual_tolerance=1e-10;request.a={n,n,n*n,1,0,a.data()};request.b={n,n,n*n,1,0,b.data()};penta_linalg_result_v1 result{};result.struct_size=sizeof(result);result.output[0]={output.data(),output.size(),0,0,0,0};penta_numeric_host_v1 callbacks{sizeof(callbacks),PENTA_NUMERIC_ABI_V1,&host,Host::cancelled,Host::parallel};char message[1024]{};
        auto product=std::async(std::launch::async,[&]{return api->compute(&callbacks,&request,&result,message,sizeof(message));});const bool entered=until([&]{return host.held.load();});expect(entered,"An actual matrix tile worker owns its own one-thread OpenMP ICV");std::atomic_bool writerFinished{false};auto writer=std::async(std::launch::async,[&]{penta::blas::Scope scope(2,2,false);writerFinished=true;return omp_get_max_threads()==2;});expect(until([]{return state().waiting_leases>0;})&&!writerFinished.load(),"A different-count writer waits while the actual cblas worker is active");host.release=true;const bool done=product.wait_for(std::chrono::seconds(5))==std::future_status::ready;if(!done){host.cancellation->store(true);host.release=true;}const auto code=product.get();expect(done&&code==0&&writer.get(),"Product workers and a queued reconfiguration finish without a parent-lease deadlock");if(code)std::cerr<<message<<'\n';expect(host.workers>=2&&host.capped&&host.statistics.threadsUsed==2,"Both real host workers execute actual one-thread OpenMP teams");expect(host.restored,"Each tile worker restores its deliberately different original ICV");bool correct=true;for(unsigned at=0;at<n*n;++at)correct=correct&&output[at]==2*a[at];expect(correct&&state().active_leases==0,"Parallel tile outputs match independent scalar reference and release every lease");std::cout<<checks<<" actual OpenMP matrix-worker checks, "<<failures<<" failures\n";return failures?1:0;
    }catch(const std::exception& failure){std::cerr<<failure.what()<<'\n';return 1;}
#endif
}
