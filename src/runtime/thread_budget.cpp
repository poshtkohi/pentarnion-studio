#include "thread_budget.hpp"
#include <algorithm>
#include <bit>
#include <condition_variable>
#include <chrono>
#include <cmath>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

namespace penta::runtime {
namespace {
thread_local bool insideNumericalTask=false;
thread_local bool ownsNumericalReservation=false;
thread_local const std::function<void()>* ownerPump=nullptr;
}
struct ThreadBudget::State {
    std::mutex executionMutex,mutex,errorMutex;
    std::condition_variable ready,finished;
    std::vector<std::thread> workers;
    bool stopping=false;
    std::uint64_t generation=0;
    std::size_t activeWorkers=0,remainingWorkers=0,count=0,grain=512;
    penta_numeric_range_task_v1 task=nullptr;void* context=nullptr;
    std::shared_ptr<std::atomic_bool> cancellation;
    std::atomic_size_t next{0},active{0},peak{0};
    std::atomic_uint64_t participants{0};
    std::atomic_uint64_t activitySequence{0};
    std::atomic_int status{0};
    std::string error;
    void work(std::size_t participant) noexcept {
        while(status.load(std::memory_order_relaxed)==0) {
            if(cancellation&&cancellation->load(std::memory_order_relaxed)){status.store(PENTA_NUMERIC_CANCELLED);break;}
            const auto begin=next.fetch_add(grain,std::memory_order_relaxed);if(begin>=count)break;
            participants.fetch_or(std::uint64_t{1}<<participant,std::memory_order_relaxed);
            const auto now=active.fetch_add(1,std::memory_order_relaxed)+1;
            auto maximum=peak.load(std::memory_order_relaxed);while(maximum<now&&!peak.compare_exchange_weak(maximum,now,std::memory_order_relaxed)){}
            char message[512]{};int result=PENTA_NUMERIC_INTERNAL;
            struct Scope {bool previous=insideNumericalTask;Scope(){insideNumericalTask=true;}~Scope(){insideNumericalTask=previous;}} scope;
            try{result=task(context,begin,std::min(count,begin+grain),message,sizeof(message));}
            catch(...){std::strncpy(message,"A numerical worker threw across its C ABI.",sizeof(message)-1);}
            active.fetch_sub(1,std::memory_order_relaxed);
            if(result!=PENTA_NUMERIC_OK){
                int expected=0;if(status.compare_exchange_strong(expected,result)){
                    try{std::lock_guard lock(errorMutex);error=message;}catch(...){}
                }
                break;
            }
        }
    }
    void worker(std::size_t index) noexcept {
        std::uint64_t seen=0;
        for(;;){
            std::unique_lock lock(mutex);ready.wait(lock,[&]{return stopping||generation!=seen;});
            if(stopping)return;
            seen=generation;
            if(index>=activeWorkers)continue;
            lock.unlock();work(index+1);lock.lock();
            if(--remainingWorkers==0)finished.notify_one();
        }
    }
};
ThreadBudget::ThreadBudget():state_(std::make_unique<State>()){}
ThreadBudget::~ThreadBudget(){
    {std::lock_guard lock(state_->mutex);state_->stopping=true;}
    state_->ready.notify_all();for(auto& worker:state_->workers)if(worker.joinable())worker.join();
}
ThreadBudget& ThreadBudget::instance(){static ThreadBudget pool;return pool;}
ThreadActivity ThreadBudget::activity()const noexcept{
    return {state_->active.load(std::memory_order_relaxed),state_->peak.load(std::memory_order_relaxed),state_->activitySequence.load(std::memory_order_relaxed)};
}
bool ThreadBudget::inNumericalTask() noexcept{return insideNumericalTask;}
std::size_t ThreadBudget::backendThreadLimit(std::size_t requested){
    const auto effective=effectiveThreads(requested);
    return insideNumericalTask?1:effective;
}
void ThreadBudget::withReservation(const std::function<void()>& action){
    if(insideNumericalTask||ownsNumericalReservation){action();return;}
    std::lock_guard lock(state_->executionMutex);
    struct Reservation {Reservation(){ownsNumericalReservation=true;}~Reservation(){ownsNumericalReservation=false;}} reservation;
    action();
}
void ThreadBudget::withOwnerPump(const std::function<void()>& pump,const std::function<void()>& action){
    struct Scope {
        const std::function<void()>* previous=ownerPump;
        explicit Scope(const std::function<void()>& service){ownerPump=&service;}
        ~Scope(){ownerPump=previous;}
    } scope(pump);
    action();
}
std::size_t ThreadBudget::effectiveThreads(std::size_t requested){
    if(requested>ParallelSettings::maximumThreads)throw std::invalid_argument("Thread count must be 0 (automatic) or between 1 and 64.");
    return requested?requested:std::clamp<std::size_t>(std::thread::hardware_concurrency(),1,ParallelSettings::maximumThreads);
}
int ThreadBudget::run(std::size_t count,std::size_t cost,penta_numeric_range_task_v1 task,void* context,
    const ParallelSettings& settings,const std::shared_ptr<std::atomic_bool>& cancellation,
    ParallelStatistics& statistics,std::string& error){
    const auto effective=effectiveThreads(settings.threads);
    if(!task){error="A numerical task callback is missing.";return PENTA_NUMERIC_ARGUMENT;}
    if(cancellation&&cancellation->load()){error="Numerical execution cancelled.";return PENTA_NUMERIC_CANCELLED;}
    if(insideNumericalTask){
        // A module may reuse a numerical kernel inside an existing worker.
        // Execute that nested operation inline, without acquiring the outer
        // execution lock, creating workers, or racing on its aggregate stats.
        for(std::size_t begin=0;begin<count;begin+=512){
            if(cancellation&&cancellation->load()){error="Numerical execution cancelled.";return PENTA_NUMERIC_CANCELLED;}
            char local[512]{};const int status=task(context,begin,std::min(count,begin+512),local,sizeof(local));
            if(status){error=local;return status;}
        }
        return PENTA_NUMERIC_OK;
    }
    statistics.requestedThreads=settings.threads;statistics.effectiveThreads=effective;statistics.lastElements=count;
    statistics.threadsUsed=statistics.peakActiveThreads=count?1:0;
    if(count==0){++statistics.serialCalls;return PENTA_NUMERIC_OK;}
    const auto workCost=std::max<std::size_t>(1,cost);
    const auto threshold=std::max<std::size_t>(1,settings.serialThreshold);
    const bool parallel=effective>1&&count>1&&count>=((threshold-1)/workCost+1);
    // One execution lock is shared by all callers/modules, preventing nested
    // independent pools from multiplying the process-wide CPU worker budget.
    auto& state=*state_;std::unique_lock execution(state.executionMutex,std::defer_lock);
    if(!ownsNumericalReservation)execution.lock();
    std::unique_lock lock(state.mutex);
    state.count=count;state.grain=std::clamp<std::size_t>(2048/workCost,1,512);state.next=0;state.active=0;state.peak=0;state.participants=0;state.status=0;state.error.clear();
    state.activitySequence.fetch_add(1,std::memory_order_relaxed);
    state.task=task;state.context=context;state.cancellation=cancellation;
    std::size_t available=effective;
    if(settings.threads==0&&parallel){
        const long double worthwhile=std::ceil(static_cast<long double>(count)*workCost/threshold);
        available=std::min(effective,static_cast<std::size_t>(std::clamp<long double>(worthwhile,2,effective)));
    }
    const std::size_t threads=parallel?std::min(available,(count+state.grain-1)/state.grain):1;
    while(state.workers.size()+1<threads){const auto index=state.workers.size();state.workers.emplace_back([&state,index]{state.worker(index);});}
    state.activeWorkers=threads-1;state.remainingWorkers=threads-1;++state.generation;
    if(threads>1){++statistics.parallelCalls;state.ready.notify_all();}else ++statistics.serialCalls;
    lock.unlock();state.work(0);lock.lock();
    std::exception_ptr pumpFailure;
    if(ownerPump&&*ownerPump){
        // A worker may be waiting for an immutable iteration snapshot to be
        // observed by this owner. Never pump under the pool state mutex. Mark
        // the owner's reserved CPU slot active so callback native array math
        // cannot overwrite the currently dispatched persistent-pool job.
        do{
            lock.unlock();
            struct Scope {bool previous=insideNumericalTask;Scope(){insideNumericalTask=true;}~Scope(){insideNumericalTask=previous;}} scope;
            try{(*ownerPump)();}catch(...){if(!pumpFailure)pumpFailure=std::current_exception();}
            lock.lock();
            if(state.remainingWorkers)state.finished.wait_for(lock,std::chrono::milliseconds(2));
        }while(state.remainingWorkers);
    }else state.finished.wait(lock,[&]{return state.remainingWorkers==0;});
    statistics.threadsUsed=std::popcount(state.participants.load());statistics.peakActiveThreads=state.peak.load();
    const auto status=state.status.load();error=state.error;
    state.task=nullptr;state.context=nullptr;state.cancellation.reset();
    if(pumpFailure)std::rethrow_exception(pumpFailure);
    if(status==PENTA_NUMERIC_CANCELLED&&error.empty())error="Numerical execution cancelled.";
    return status;
}
}
