#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
namespace penta::desktop {
struct WorkerActivity {
    std::size_t poolActive=0,poolPeak=0,blasTeamWidth=0,blasOwners=0;
    std::uint64_t poolSequence=0;
    // Actual running host-pool callbacks, never a configured backend width.
    std::size_t workers()const noexcept;
    // BLAS leases describe an upper-bound reservation, not active CPU workers.
    std::size_t reservedWorkers()const noexcept;
};
// Lock-free pool-only snapshot. Never calls a numerical module or its mutexes.
WorkerActivity sampleWorkerActivity()noexcept;

// GUI reads are pool atomics plus a cached BLAS reservation. A single lazy,
// low-priority sampler inspects the already-loaded coordinator away from the
// GUI thread only while execution is active; it never loads a module itself.
class WorkerActivityMonitor final {
public:
    WorkerActivityMonitor();
    ~WorkerActivityMonitor();
    WorkerActivityMonitor(const WorkerActivityMonitor&)=delete;
    WorkerActivityMonitor& operator=(const WorkerActivityMonitor&)=delete;
    void setRunning(bool)noexcept;
    WorkerActivity snapshot()const noexcept;
private:
    struct State;std::unique_ptr<State> state_;
};
}
