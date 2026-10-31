#include "modules/blas_coordinator_api.h"
#include "modules/blas_coordinator_scope.hpp"
#include <cblas.h>
#if defined(_OPENMP)
#include <omp.h>
#endif
#include <algorithm>
#include <atomic>
#include <barrier>
#include <chrono>
#include <cmath>
#include <future>
#include <iostream>
#include <thread>
#include <vector>
namespace {
unsigned checks=0,failures=0;void verify(bool value,const char* message){++checks;if(!value){++failures;std::cerr<<"FAIL "<<message<<'\n';}}
const auto* api=penta_blas_coordinator_query_v1(PENTA_BLAS_COORDINATOR_ABI_V1);
penta_blas_status_v1 status(){penta_blas_status_v1 value{};value.struct_size=sizeof(value);char error[1024]{};if(api->status(&value,error,sizeof(error)))throw std::runtime_error(error);return value;}
int begin(unsigned threads,unsigned grant,unsigned flags,penta_blas_lease_v1& lease,std::atomic_bool* cancel=nullptr){penta_blas_host_v1 host{sizeof(host),PENTA_BLAS_COORDINATOR_ABI_V1,grant,0,cancel,+[](void* p){return p&&static_cast<std::atomic_bool*>(p)->load()?1:0;}};penta_blas_request_v1 request{sizeof(request),threads,flags,0};char error[1024]{};return api->begin(&host,&request,&lease,error,sizeof(error));}
template<class Predicate>bool until(Predicate predicate){const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);while(!predicate()){if(std::chrono::steady_clock::now()>deadline)return false;std::this_thread::sleep_for(std::chrono::milliseconds(1));}return true;}
bool product(unsigned n=64){std::vector<double> a(n*n),b(n*n,0),c(n*n,0);for(unsigned row=0;row<n;++row)for(unsigned column=0;column<n;++column)a[row*n+column]=(double(row)-column)*.03125;for(unsigned row=0;row<n;++row)b[row*n+row]=2;cblas_dgemm(CblasRowMajor,CblasNoTrans,CblasNoTrans,n,n,n,1,a.data(),n,b.data(),n,0,c.data(),n);for(unsigned at=0;at<n*n;++at)if(c[at]!=2*a[at])return false;return true;}
}
int main(){verify(api&&api->struct_size>=sizeof(*api)&&!penta_blas_coordinator_query_v1(2),"Versioned shared coordinator ABI");const auto previous=openblas_get_num_threads();penta_blas_lease_v1 invalid{sizeof(invalid),0,0,0,0};verify(begin(2,1,0,invalid)==1,"Backend requests cannot exceed the host CPU grant");verify(begin(0,1,0,invalid)==1,"Automatic thread counts are resolved by the host before admission");
    penta_blas_lease_v1 outer{sizeof(outer),0,0,0,0};verify(!begin(1,1,0,outer)&&status().backend_threads==1,"An active epoch applies its exact bounded thread configuration");auto copied=outer;penta_blas_lease_v1 inner{sizeof(inner),0,0,0,0};verify(!begin(1,1,0,inner)&&status().active_leases==2&&status().active_owners==1,"Nested same-owner compatible leases are supported");penta_blas_lease_v1 upgrade{sizeof(upgrade),0,0,0,0};verify(begin(1,1,PENTA_BLAS_EXCLUSIVE,upgrade)==1,"Nested exclusive upgrades fail instead of deadlocking");char error[1024]{};verify(!api->end(&inner,error,sizeof(error)),"Nested leases end independently");const auto wrongOwner=std::async(std::launch::async,[copied]()mutable{char error[1024]{};return api->end(&copied,error,sizeof(error));}).get();verify(wrongOwner==1&&status().active_leases==1,"Cross-thread stale/copy ownership cannot end another call's lease");verify(!api->end(&outer,error,sizeof(error))&&openblas_get_num_threads()==previous,"Last lease restores the prior external BLAS configuration");verify(api->end(&copied,error,sizeof(error))==1,"Double end is rejected without corrupting epoch accounting");
#if defined(_OPENMP)
    if(openblas_get_parallel()==OPENBLAS_OPENMP){const auto beforeIcv=omp_get_max_threads(),beforeDynamic=omp_get_dynamic();{penta::blas::Scope one(1,1,false);int team=0;
#pragma omp parallel reduction(+:team)
        {team+=1;}
        verify(omp_get_max_threads()==1&&team==1,"OpenMP actual worker team obeys the one-thread caller grant");}
        verify(omp_get_max_threads()==beforeIcv&&omp_get_dynamic()==beforeDynamic,"OpenMP caller ICV and dynamic setting restore at the last owner lease");
        omp_set_num_threads(3);omp_set_dynamic(1);penta_blas_lease_v1 firstToken{sizeof(firstToken),0,0,0,0},lastToken{sizeof(lastToken),0,0,0,0};verify(!begin(1,1,0,firstToken)&&!begin(1,1,0,lastToken),"OpenMP nested tokens share one original caller ICV snapshot");verify(!api->end(&firstToken,error,sizeof(error))&&omp_get_max_threads()==1&&omp_get_dynamic()==0,"Ending an outer token first does not uncap remaining nested work");verify(!api->end(&lastToken,error,sizeof(error))&&omp_get_max_threads()==3&&omp_get_dynamic()==1,"Last nested token restores the original nondefault owner ICV");omp_set_num_threads(beforeIcv);omp_set_dynamic(beforeDynamic);
        auto four=std::async(std::launch::async,[]{penta::blas::Scope lease(4,4,false);int team=0;
#pragma omp parallel reduction(+:team)
            {team+=1;}
            return omp_get_max_threads()==4&&team==4;});verify(four.get(),"An independent OpenMP caller applies its own explicit four-thread grant");}
#endif
    double overlapSeconds=0;for(const unsigned grant:{1u,4u})if(status().backend_parallel!=OPENBLAS_SEQUENTIAL){std::barrier ready(3),release(3);std::atomic_bool numerical{true};const auto started=std::chrono::steady_clock::now();auto worker=[&]{penta::blas::Scope lease(grant,grant,false);ready.arrive_and_wait();if(!product())numerical.store(false);release.arrive_and_wait();};std::thread a(worker),b(worker);ready.arrive_and_wait();verify(status().active_owners==2&&status().configured_threads==grant,"Two compatible clients actually hold the same epoch concurrently");std::this_thread::sleep_for(std::chrono::milliseconds(60));release.arrive_and_wait();a.join();b.join();overlapSeconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();verify(numerical.load(),"Concurrent BLAS products retain independent correct output buffers");}
    {penta::blas::Scope holder(1,1,false);std::atomic_bool cancel{false};auto waiter=std::async(std::launch::async,[&]{penta_blas_lease_v1 lease{sizeof(lease),0,0,0,0};return begin(2,2,0,lease,&cancel);});verify(until([]{return status().waiting_leases>0;})&&status().backend_threads==1,"Incompatible settings wait without resizing an active backend pool");cancel.store(true);verify(waiter.get()==3&&status().waiting_leases==0,"Cancellation releases configuration waiters without leaking tokens");
    // End the holder before testing the initialization-style exclusive epoch.
    {
        // Same-owner nested work remains admissible even while a writer waits.
        std::atomic_bool writerCancel{false};auto writer=std::async(std::launch::async,[&]{penta_blas_lease_v1 lease{sizeof(lease),0,0,0,0};return begin(2,2,0,lease,&writerCancel);});verify(until([]{return status().waiting_leases>0;}),"A queued reconfiguration is observable");{penta::blas::Scope nested(1,1,false);verify(product(),"Nested callback math cannot deadlock behind a waiting configuration writer");}writerCancel.store(true);verify(writer.get()==3,"Nested callback writer cancellation preserves its parent epoch");
    }
    }
    // The holder is gone before the initialization-style exclusive epoch.
    penta_blas_lease_v1 exclusive{sizeof(exclusive),0,0,0,0};verify(!begin(1,1,PENTA_BLAS_EXCLUSIVE,exclusive),"Initialization gets a sole exclusive configuration epoch");openblas_set_num_threads(4);verify(api->downgrade(&exclusive,error,sizeof(error))==1,"An initializer cannot publish an environment-reset configuration");verify(!api->reassert(&exclusive,error,sizeof(error))&&openblas_get_num_threads()==1,"Exclusive reassertion repairs a vendor's environment-driven thread reset");
    if(status().backend_parallel!=OPENBLAS_SEQUENTIAL){std::atomic_bool entered{false},release{false},correct{true};auto reader=std::async(std::launch::async,[&]{penta::blas::Scope lease(1,1,false);entered.store(true);while(!release.load())std::this_thread::sleep_for(std::chrono::milliseconds(1));correct.store(product());});verify(until([]{return status().waiting_leases>0;})&&!entered.load(),"Exclusive initialization blocks independent active BLAS clients");verify(!api->downgrade(&exclusive,error,sizeof(error))&&until([&]{return entered.load();})&&status().active_owners==2,"After initialization, downgrade admits compatible module calls immediately");release.store(true);reader.get();verify(correct.load(),"Post-initialization compatible BLAS computation stays correct");}else verify(!api->downgrade(&exclusive,error,sizeof(error)),"Sequential backend releases initialization exclusivity safely");verify(!api->end(&exclusive,error,sizeof(error))&&openblas_get_num_threads()==previous,"Initializer and readers restore prior external state when idle");
    std::vector<penta_blas_lease_v1> leases(4096);bool bounded=true;for(auto& lease:leases){lease.struct_size=sizeof(lease);if(begin(1,1,0,lease)){bounded=false;break;}}penta_blas_lease_v1 excess{sizeof(excess),0,0,0,0};verify(bounded&&begin(1,1,0,excess)==1,"Lease registry has an explicit bounded capacity");for(auto at=leases.rbegin();at!=leases.rend();++at)if(at->token&&api->end(&*at,error,sizeof(error)))bounded=false;verify(bounded&&status().active_leases==0,"Capacity stress releases every token without growing retained allocations");
    std::atomic_bool stressCorrect{true};std::vector<std::thread> workers;for(unsigned worker=0;worker<8;++worker)workers.emplace_back([&]{try{for(unsigned repeat=0;repeat<30;++repeat){penta::blas::Scope lease(1,1,false);if(!product())stressCorrect.store(false);}}catch(...){stressCorrect.store(false);}});for(auto& worker:workers)worker.join();verify(stressCorrect.load()&&status().active_leases==0&&status().waiting_leases==0&&openblas_get_num_threads()==previous,"Concurrent numerical/lifetime stress preserves buffers and leaves no active leases");
    std::cout<<checks<<" coordinator checks, "<<failures<<" failures; compatible overlap wall="<<overlapSeconds<<" seconds\n";return failures?1:0;
}
