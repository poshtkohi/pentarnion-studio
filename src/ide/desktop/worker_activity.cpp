#include "worker_activity.hpp"
#include "runtime/thread_budget.hpp"
#include "modules/blas_coordinator_api.h"
#include <QThread>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <thread>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif
namespace penta::desktop {
std::size_t WorkerActivity::workers()const noexcept{return poolActive;}
std::size_t WorkerActivity::reservedWorkers()const noexcept{
    if(!blasTeamWidth||!blasOwners)return 0;
    constexpr std::size_t maximum=runtime::ParallelSettings::maximumThreads;
    if(blasTeamWidth>=maximum||blasOwners>(maximum-1)/blasTeamWidth)return maximum;
    return blasTeamWidth*blasOwners;
}
WorkerActivity sampleWorkerActivity()noexcept{
    WorkerActivity result;
    try{
        const auto pool=runtime::ThreadBudget::instance().activity();
        result.poolActive=pool.active;result.poolPeak=pool.peak;result.poolSequence=pool.sequence;
    }catch(...){} // Status presentation cannot fail a calculation.
    return result;
}
namespace {
// A temporary reference closes the race between observing a loaded library and
// a failed optional-module load releasing its last owner. Neither path loads it.
struct ExistingModule {
#ifdef _WIN32
    HMODULE handle=nullptr;
    ExistingModule(){GetModuleHandleExW(0,L"penta_blas.dll",&handle);}
    ~ExistingModule(){if(handle)FreeLibrary(handle);}
    penta_blas_coordinator_query_function_v1 query()const{
        if(!handle)return nullptr;
        const auto symbol=GetProcAddress(handle,"penta_blas_coordinator_query_v1");if(!symbol)return nullptr;
        penta_blas_coordinator_query_function_v1 function=nullptr;
        static_assert(sizeof(function)==sizeof(symbol));
        std::memcpy(&function,&symbol,sizeof(function));return function;
    }
#elif defined(RTLD_NOLOAD)
    void* handle=dlopen("libpenta_blas.so",RTLD_LAZY|RTLD_NOLOAD);
    ExistingModule()=default;
    ~ExistingModule(){if(handle)dlclose(handle);}
    penta_blas_coordinator_query_function_v1 query()const{
        return handle?reinterpret_cast<penta_blas_coordinator_query_function_v1>(dlsym(handle,"penta_blas_coordinator_query_v1")):nullptr;
    }
#else
    ExistingModule()=default;
    penta_blas_coordinator_query_function_v1 query()const{return nullptr;}
#endif
    ExistingModule(const ExistingModule&)=delete;
    ExistingModule& operator=(const ExistingModule&)=delete;
};
std::uint64_t observeBlasReservations()noexcept{
    try{
        const ExistingModule module;
        const auto query=module.query();if(!query)return 0;
        const auto* api=query(PENTA_BLAS_COORDINATOR_ABI_V1);
        if(!api||api->struct_size<sizeof(*api)||api->abi_version!=PENTA_BLAS_COORDINATOR_ABI_V1||!api->status)return 0;
        penta_blas_status_v1 status{};status.struct_size=sizeof(status);char error[128]{};
        if(api->status(&status,error,sizeof(error))!=0||!status.active_leases)return 0;
        return (std::uint64_t(status.configured_threads)<<32)|status.active_owners;
    }catch(...){return 0;}
}
}
struct WorkerActivityMonitor::State {
    std::mutex mutex;std::condition_variable changed;std::thread worker;
    bool running=false,stopping=false;
    std::uint64_t epoch=0;
    std::atomic_uint64_t blasReservations{0};
    void run(){
        QThread::currentThread()->setPriority(QThread::LowPriority);
        std::unique_lock lock(mutex);
        for(;;){
            changed.wait(lock,[this]{return stopping||running;});
            if(stopping)return;
            const auto sampledEpoch=epoch;
            lock.unlock();const auto reservations=observeBlasReservations();lock.lock();
            if(stopping)return;
            // A paused/finished request must not publish a late old lease.
            if(running&&sampledEpoch==epoch)blasReservations.store(reservations,std::memory_order_relaxed);
            changed.wait_for(lock,std::chrono::milliseconds(120),[this,sampledEpoch]{return stopping||!running||epoch!=sampledEpoch;});
        }
    }
};
WorkerActivityMonitor::WorkerActivityMonitor():state_(std::make_unique<State>()){}
WorkerActivityMonitor::~WorkerActivityMonitor(){
    {
        std::lock_guard lock(state_->mutex);state_->stopping=true;state_->running=false;++state_->epoch;
        state_->blasReservations.store(0,std::memory_order_relaxed);
    }
    state_->changed.notify_all();
    // Host destroys this monitor after cancelling/joining the scientific worker.
    // The sampler never owns numerical configuration or changes a thread grant.
    if(state_->worker.joinable())state_->worker.join();
}
void WorkerActivityMonitor::setRunning(bool running)noexcept{
    try{
        {
            std::lock_guard lock(state_->mutex);
            if(state_->stopping||state_->running==running)return;
            state_->running=running;++state_->epoch;
            if(!running)state_->blasReservations.store(0,std::memory_order_relaxed);
            if(running&&!state_->worker.joinable()){
                try{state_->worker=std::thread([state=state_.get()]{state->run();});}
                catch(...){state_->running=false;state_->blasReservations.store(0,std::memory_order_relaxed);}
            }
        }
        state_->changed.notify_all();
    }catch(...){}
}
WorkerActivity WorkerActivityMonitor::snapshot()const noexcept{
    auto result=sampleWorkerActivity();
    const auto reservations=state_->blasReservations.load(std::memory_order_relaxed);
    result.blasTeamWidth=std::uint32_t(reservations>>32);result.blasOwners=std::uint32_t(reservations);
    return result;
}
}
