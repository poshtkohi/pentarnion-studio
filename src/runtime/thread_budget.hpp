#pragma once
#include "modules/numeric_api.h"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <functional>
#include <string>
namespace penta::runtime {
struct ParallelSettings {
    std::size_t threads=0; // Automatic logical CPU selection, bounded to maximumThreads.
    std::size_t serialThreshold=32768;
    static constexpr std::size_t maximumThreads=64;
};
struct ParallelStatistics {
    std::size_t requestedThreads=0,effectiveThreads=1,threadsUsed=1,peakActiveThreads=1;
    std::size_t lastElements=0,parallelCalls=0,serialCalls=0;
};
// Read-only host observability. This does not reserve CPUs or change scheduling.
struct ThreadActivity {std::size_t active=0,peak=0;std::uint64_t sequence=0;};
class ThreadBudget final {
public:
    static ThreadBudget& instance();
    static std::size_t effectiveThreads(std::size_t requested);
    // Native callers can choose each call's ParallelSettings. An inner backend
    // invoked by an existing numerical worker receives one CPU slot, so its
    // own BLAS/OpenMP pool cannot multiply the outer worker reservation.
    static bool inNumericalTask() noexcept;
    static std::size_t backendThreadLimit(std::size_t requested);
    ThreadActivity activity() const noexcept;
    // Reserve the process CPU budget before taking a backend configuration lock.
    // Calls to run() from the reserving driver still dispatch normal workers;
    // calls from an existing numerical worker remain inline.
    void withReservation(const std::function<void()>& action);
    // Install a scoped owner-thread service for synchronous worker observers.
    // The nonblocking pump runs outside pool/state locks while run() joins its
    // workers. It must release its waiting clients even if an observer fails.
    // Nested numerical work from the pump stays inline in the reserved slot;
    // nested pump scopes restore the previous service on every exit path.
    void withOwnerPump(const std::function<void()>& pump,const std::function<void()>& action);
    int run(std::size_t count,std::size_t cost,penta_numeric_range_task_v1 task,void* taskContext,
        const ParallelSettings&,const std::shared_ptr<std::atomic_bool>& cancellation,
        ParallelStatistics&,std::string& error);
    ~ThreadBudget();
    ThreadBudget(const ThreadBudget&)=delete;
    ThreadBudget& operator=(const ThreadBudget&)=delete;
private:
    ThreadBudget();
    struct State;std::unique_ptr<State> state_;
};
}
