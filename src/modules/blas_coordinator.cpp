#ifndef PENTA_BLAS_COORDINATOR_BUILD
#define PENTA_BLAS_COORDINATOR_BUILD
#endif
#include "blas_coordinator_api.h"
#include <cblas.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <thread>
#if defined(PENTA_BLAS_GNU_OPENMP)
#include <omp.h>
#endif
namespace {
struct Failure:std::runtime_error{int code;Failure(int c,const char* text):std::runtime_error(text),code(c){}};
void require(bool value,const char* text){if(!value)throw Failure(1,text);}
void message(char* out,uint64_t capacity,const char* text){if(out&&capacity){const auto n=std::min<uint64_t>(capacity-1,std::strlen(text));std::memcpy(out,text,n);out[n]=0;}}
template<class Function>int protect(Function&& function,char* error,uint64_t capacity)noexcept{try{function();return 0;}catch(const Failure& failure){message(error,capacity,failure.what());return failure.code;}catch(const std::exception& failure){message(error,capacity,failure.what());return 4;}catch(...){message(error,capacity,"Unexpected shared BLAS coordinator failure.");return 4;}}
constexpr bool supportedOpenMP(){
#if defined(PENTA_BLAS_GNU_OPENMP)
    return true;
#else
    return false;
#endif
}
int localThreads(){
#if defined(PENTA_BLAS_GNU_OPENMP)
    return openblas_get_parallel()==OPENBLAS_OPENMP?omp_get_max_threads():0;
#else
    return 0;
#endif
}
int localDynamic(){
#if defined(PENTA_BLAS_GNU_OPENMP)
    return openblas_get_parallel()==OPENBLAS_OPENMP?omp_get_dynamic():0;
#else
    return 0;
#endif
}
void configureLocal(unsigned threads){
#if defined(PENTA_BLAS_GNU_OPENMP)
    if(openblas_get_parallel()==OPENBLAS_OPENMP){omp_set_dynamic(0);omp_set_num_threads(static_cast<int>(threads));}
#else
    (void)threads;
#endif
}
void restoreLocal(int threads,int dynamic){
#if defined(PENTA_BLAS_GNU_OPENMP)
    if(threads>0){omp_set_num_threads(threads);omp_set_dynamic(dynamic);}
#else
    (void)threads;(void)dynamic;
#endif
}
struct Entry{uint64_t token=0;std::thread::id owner;uint32_t flags=0;int previousIcv=0,previousDynamic=0;};
struct Epoch{
    std::mutex mutex;std::condition_variable changed;
    std::array<Entry,4096> entries{};uint32_t active=0,owners=0,waiting=0,waitingExclusive=0,waitingSettings=0,threads=0;
    // OpenMP needs both a stable global OpenBLAS buffer configuration and a
    // scoped ICV for each calling owner. GNU builds link the same libgomp SDK.
    int previous=1;bool exclusive=false,concurrent=openblas_get_parallel()==OPENBLAS_THREAD||(openblas_get_parallel()==OPENBLAS_OPENMP&&supportedOpenMP());
    std::thread::id exclusiveOwner;uint64_t serial=0,epochs=0,changes=0,overlaps=0,maximumOwners=0,waitNanoseconds=0;
    unsigned owned(const std::thread::id& owner)const{unsigned count=0;for(unsigned i=0;i<active;++i)count+=entries[i].owner==owner;return count;}
    uint32_t find(const penta_blas_lease_v1& lease)const{for(uint32_t i=0;i<active;++i)if(entries[i].token==lease.token)return i;throw Failure(1,"The BLAS lease is stale, foreign, or already ended.");}
};
Epoch& epoch(){static Epoch value;return value;}
void cancelled(const penta_blas_host_v1& host){if(host.cancelled&&host.cancelled(host.context))throw Failure(3,"Waiting for a shared BLAS configuration was cancelled.");}
int begin(const penta_blas_host_v1* host,const penta_blas_request_v1* request,penta_blas_lease_v1* lease,char* error,uint64_t capacity)noexcept{return protect([&]{
    require(host&&host->struct_size>=sizeof(*host)&&host->abi_version==PENTA_BLAS_COORDINATOR_ABI_V1&&host->granted_threads>=1&&host->granted_threads<=64&&request&&request->struct_size>=sizeof(*request)&&request->threads>=1&&request->threads<=host->granted_threads&&(request->flags&~PENTA_BLAS_EXCLUSIVE)==0&&lease&&lease->struct_size>=sizeof(*lease)&&lease->token==0,"Invalid BLAS coordinator host grant, request, or nonempty lease.");cancelled(*host);auto& state=epoch();const auto owner=std::this_thread::get_id();std::unique_lock lock(state.mutex);const bool exclusive=(request->flags&PENTA_BLAS_EXCLUSIVE)!=0;const auto owned=state.owned(owner);require(openblas_get_parallel()!=OPENBLAS_OPENMP||supportedOpenMP(),"This OpenMP BLAS backend requires a coordinator built with matching GNU OpenMP support.");int previousIcv=localThreads(),previousDynamic=localDynamic();if(owned)for(unsigned n=0;n<state.active;++n)if(state.entries[n].owner==owner){previousIcv=state.entries[n].previousIcv;previousDynamic=state.entries[n].previousDynamic;break;}require(!owned||(!exclusive&&request->threads==state.threads),"A nested BLAS call cannot upgrade a shared lease or change its thread count.");require(state.active<state.entries.size()&&state.waiting<4096,"Shared BLAS lease/waiter capacity exceeded.");const bool settings=!exclusive&&state.active&&request->threads!=state.threads;
    struct Waiting{Epoch& state;bool exclusive,settings;bool present=true;Waiting(Epoch& value,bool x,bool s):state(value),exclusive(x),settings(s){++state.waiting;if(x)++state.waitingExclusive;if(s)++state.waitingSettings;}void leave(){if(!present)return;present=false;--state.waiting;if(exclusive)--state.waitingExclusive;if(settings)--state.waitingSettings;state.changed.notify_all();}~Waiting(){leave();}} waiting(state,exclusive,settings);const auto started=std::chrono::steady_clock::now();bool waited=false;
    auto admissible=[&]{if(!state.active){if(state.waitingExclusive&&!exclusive)return false;if(state.waitingSettings&&!settings&&!exclusive)return false;return true;}if(owned)return !exclusive&&request->threads==state.threads&&(!state.exclusive||state.exclusiveOwner==owner);return !exclusive&&!state.exclusive&&state.concurrent&&request->threads==state.threads&&!state.waitingExclusive&&!state.waitingSettings;};
    // Host cancellation is external code; allow read-only status inspection
    // without reentering this mutex. Recheck admission after relocking.
    for(;;){lock.unlock();try{cancelled(*host);}catch(...){lock.lock();throw;}lock.lock();if(admissible())break;waited=true;state.changed.wait_for(lock,std::chrono::milliseconds(10));}
    if(waited)state.waitNanoseconds+=static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-started).count());
    waiting.leave();require(state.active<state.entries.size()&&state.serial<std::numeric_limits<uint64_t>::max(),"Shared BLAS lease capacity/token namespace exhausted.");
    if(!state.active){state.previous=openblas_get_num_threads();require(state.previous>=1,"OpenBLAS returned an invalid prior thread configuration.");if(state.previous!=static_cast<int>(request->threads)){openblas_set_num_threads(request->threads);++state.changes;}state.threads=request->threads;++state.epochs;}else if(!owned)++state.overlaps;
    configureLocal(request->threads);state.entries[state.active++]={++state.serial,owner,request->flags,previousIcv,previousDynamic};if(!owned)++state.owners;state.maximumOwners=std::max<uint64_t>(state.maximumOwners,state.owners);if(exclusive){state.exclusive=true;state.exclusiveOwner=owner;}lease->threads=request->threads;lease->flags=request->flags;lease->token=state.serial;
},error,capacity);}
uint32_t checked(Epoch& state,const penta_blas_lease_v1* lease){require(lease&&lease->struct_size>=sizeof(*lease)&&lease->token,"A live BLAS lease is required.");const auto at=state.find(*lease);require(state.entries[at].owner==std::this_thread::get_id()&&lease->threads==state.threads&&lease->flags==state.entries[at].flags,"The BLAS lease owner or immutable descriptor does not match.");return at;}
int reassert(const penta_blas_lease_v1* lease,char* error,uint64_t capacity)noexcept{return protect([&]{auto& state=epoch();std::lock_guard lock(state.mutex);const auto at=checked(state,lease);require((state.entries[at].flags&PENTA_BLAS_EXCLUSIVE)&&state.exclusive,"BLAS configuration reassertion requires the exclusive owner.");if(openblas_get_num_threads()!=static_cast<int>(state.threads)){openblas_set_num_threads(state.threads);++state.changes;}configureLocal(state.threads);},error,capacity);}
int downgrade(penta_blas_lease_v1* lease,char* error,uint64_t capacity)noexcept{return protect([&]{auto& state=epoch();std::lock_guard lock(state.mutex);const auto at=checked(state,lease);require((state.entries[at].flags&PENTA_BLAS_EXCLUSIVE)&&state.exclusive&&state.active==1,"Only a sole exclusive BLAS lease can be downgraded.");require(openblas_get_num_threads()==static_cast<int>(state.threads)&&(!localThreads()||localThreads()==static_cast<int>(state.threads)),"Reassert the protected BLAS configuration before downgrading.");state.entries[at].flags=lease->flags=0;state.exclusive=false;state.exclusiveOwner={};state.changed.notify_all();},error,capacity);}
int end(penta_blas_lease_v1* lease,char* error,uint64_t capacity)noexcept{return protect([&]{auto& state=epoch();std::lock_guard lock(state.mutex);const auto at=checked(state,lease);const auto entry=state.entries[at];require(!(entry.flags&PENTA_BLAS_EXCLUSIVE)||state.active==1,"End nested BLAS leases before their exclusive parent.");state.entries[at]=state.entries[--state.active];const bool ownerFinished=!state.owned(entry.owner);if(ownerFinished)--state.owners;if(entry.flags&PENTA_BLAS_EXCLUSIVE){state.exclusive=false;state.exclusiveOwner={};}if(!state.active){if(openblas_get_num_threads()!=state.previous){openblas_set_num_threads(state.previous);++state.changes;}state.threads=0;}if(ownerFinished)restoreLocal(entry.previousIcv,entry.previousDynamic);else configureLocal(state.threads);lease->token=0;lease->flags=lease->threads=0;state.changed.notify_all();},error,capacity);}
int status(penta_blas_status_v1* output,char* error,uint64_t capacity)noexcept{return protect([&]{require(output&&output->struct_size>=sizeof(*output),"Invalid BLAS coordinator status buffer.");auto& state=epoch();std::lock_guard lock(state.mutex);output->backend_parallel=openblas_get_parallel();output->backend_threads=openblas_get_num_threads();output->configured_threads=state.threads;output->active_leases=state.active;output->active_owners=state.owners;output->waiting_leases=state.waiting;output->exclusive=state.exclusive;output->epochs=state.epochs;output->configuration_changes=state.changes;output->compatible_overlaps=state.overlaps;output->maximum_active_owners=state.maximumOwners;output->wait_nanoseconds=state.waitNanoseconds;},error,capacity);}
const penta_blas_coordinator_api_v1 api{sizeof(api),PENTA_BLAS_COORDINATOR_ABI_V1,begin,reassert,downgrade,end,status};
}
extern "C" PENTA_BLAS_COORDINATOR_EXPORT const penta_blas_coordinator_api_v1* penta_blas_coordinator_query_v1(uint32_t version){return version==PENTA_BLAS_COORDINATOR_ABI_V1?&api:nullptr;}
