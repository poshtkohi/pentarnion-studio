#pragma once
#include "native_functions/native_callback.hpp"
#include "runtime/thread_budget.hpp"
#include <filesystem>
#include <limits>

namespace penta::maps::ensemble {
enum class TaskStatus:std::uint32_t {Pending=0,Completed=1,Escaped=2,Failed=3};
struct Options {
    const native_functions::Callback* callback=nullptr;
    std::vector<double> initial,fixedInput;
    std::size_t runs=0,coordinates=0,iterations=1,threads=0,batchSize=16;
    double gamma=1,escapeRadius=0,maximumSeconds=60;
    std::uint64_t seed=0;
    std::size_t randomCoordinate=std::numeric_limits<std::size_t>::max();
    std::string distribution="uniform";
    std::shared_ptr<std::atomic_bool> cancellation;
    std::function<bool()> stopRequested;
    // Invoked on the initiating thread after a checkpointed completion batch;
    // true requests a clean pause. Never an optimization-iteration surrogate.
    std::function<bool(std::size_t,std::size_t)> progress;
    std::filesystem::path checkpoint;
    bool resume=false,overwriteCheckpoint=false,retryFailed=false;
};
struct Result {
    std::vector<double> states;
    std::vector<std::uint32_t> status;
    std::vector<std::uint64_t> iterations,evaluations;
    std::vector<std::string> messages;
    runtime::ParallelStatistics parallel;
    std::size_t completed=0,restored=0;
    bool cancelled=false,userStopped=false,timedOut=false,checkpointOkay=true;
    std::string checkpointMessage,fingerprint;
    double elapsedSeconds=0;
};
Result run(const Options&);
}
