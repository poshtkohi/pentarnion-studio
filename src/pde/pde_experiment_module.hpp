#pragma once
#include "pde_module.hpp"
#include "modules/pde_experiment_api.h"
#include <string>
namespace penta::pde::experiments {
struct Options {
    bool grid=false;std::size_t runs=0,threads=0,batchSize=8;double maximumWallSeconds=300;std::size_t maximumBytes=256*1024*1024;
    penta_pde_time_request_v1 one{};penta_pde_grid_request_v1 two{};
    std::vector<double> x,y,left,right,boundary,initial;std::vector<uint8_t> retain;
    void* reactionContext=nullptr;penta_pde_experiment_reaction_v1 reaction=nullptr;
    std::shared_ptr<std::atomic_bool> cancellation;std::function<void(std::size_t)> charge;std::function<bool()> stopRequested;
    std::string modelIdentity;std::filesystem::path checkpoint;bool resume=false,overwriteCheckpoint=false,retryFailed=false;
};
struct Result {
    std::vector<uint32_t> status;std::vector<double> finalValues,finalMeans,finalRms,maximumRms,stopTimes;
    std::vector<uint64_t> steps,evaluations,linearSolves,factorizations,innerThreads;std::vector<char> messages;
    std::vector<std::shared_ptr<const pde::Result>> retained;penta_pde_experiment_result_v1 report{};
    runtime::ParallelStatistics parallel{};std::size_t threadsGranted=1,restored=0;bool checkpointOkay=true;std::string checkpointMessage,fingerprint;
};
class Module {
public:
    explicit Module(std::filesystem::path directory={});Result run(const Options&)const;
private:std::filesystem::path path_,directory_;const penta_pde_experiment_api_v1& api()const;
};
}
