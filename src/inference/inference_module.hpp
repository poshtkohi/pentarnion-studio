#pragma once
#include "modules/inference_api.h"
#include "uncertainty.hpp"
#include "runtime/thread_budget.hpp"
#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
namespace penta::inference {
struct Error:std::runtime_error{int status;Error(int code,const std::string& message):std::runtime_error(message),status(code){}};
struct Options {
    std::shared_ptr<std::atomic_bool> cancellation;std::size_t threads=0;
    std::function<void(std::size_t)> charge;std::function<bool()> stopRequested;
    std::function<bool(const penta_iteration_snapshot_v1&)> observer;
    std::string modelIdentity;std::filesystem::path checkpoint;
    bool resume=false,overwriteCheckpoint=false,retryFailed=false;
};
struct Result {
    std::vector<double> parameters,costs,predictions,baseParameters,basePredictions,parameterMean,parameterStd,correlation,correlationDefined,fittedLower,fittedUpper;
    std::vector<uint32_t> status;std::vector<uint64_t> iterations,evaluations,ranks;std::vector<char> messages;
    penta_inference_result_v1 report{};runtime::ParallelStatistics parallel{};std::size_t threadsGranted=1,restored=0;
    bool checkpointOkay=true;std::string checkpointMessage,fingerprint;
};
class Module {
public:
    explicit Module(std::filesystem::path directory={});
    Result run(const penta_inference_request_v1&,const Options&)const;
private:
    std::filesystem::path path_,fitDirectory_;const penta_inference_api_v1& api()const;
};
}
