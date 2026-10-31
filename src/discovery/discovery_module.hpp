#pragma once
#include "modules/discovery_api.h"
#include "failure_reason.hpp"
#include "basin_candidates.hpp"
#include "ode/native_ode.hpp"
#include "runtime/thread_budget.hpp"
#include <filesystem>
#include <functional>
#include <string>
#include <vector>
namespace penta::discovery {
struct Options {
    std::size_t runs=0,physicalCoordinates=0,threads=0,batchSize=16;
    std::vector<double> initial,parameters,gamma;
    ode::SolveOptions solver;
    ode::NativeOptions native;
    double transient=0,returnTolerance=1e-3,equilibriumTolerance=1e-7,minReturnTime=.1;
    double escapeRadius=0,maximumWallSeconds=300;
    std::size_t maximumBytes=256*1024*1024;
    bool retainTrajectories=false,resume=false,overwriteCheckpoint=false;
    std::filesystem::path checkpoint;
    std::string invocationHash;
    std::function<bool()> stopRequested;
    std::function<void(std::size_t,std::size_t)> progress;
};
struct Result {
    std::vector<std::uint32_t> status;
    std::vector<std::uint32_t> failureReasons;
    std::vector<double> states,stopTimes,maximumNorm,returnError,period,rhsNorm;
    std::vector<std::uint64_t> steps,evaluations;
    std::vector<std::string> messages;
    std::vector<std::shared_ptr<const ode::Solution>> trajectories;
    runtime::ParallelStatistics parallel;
    std::size_t completed=0,restored=0;
    double elapsedSeconds=0;
    std::string fingerprint,kernelKey,checkpointMessage;
    bool cacheHit=false,checkpointOkay=true,cancelled=false;
};
const char* statusName(std::uint32_t)noexcept;
class Module final {
public:
    explicit Module(std::filesystem::path directory={});
    [[nodiscard]] bool loaded()const;
    [[nodiscard]] Result run(const ode::PreparedModel&,const Options&)const;
private:
    std::filesystem::path path_;
    const penta_discovery_api_v1& api()const;
};
}
