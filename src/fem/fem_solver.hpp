#pragma once
#include "modules/fem_api.h"
#include "modules/fem_mean_api.h"
#include "modules/fem_parallel_api.h"
#include "modules/fem_system_api.h"
#include "modules/fem_mixed_api.h"
#include "runtime/thread_budget.hpp"
#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <stdexcept>
#include <vector>
#include <span>
namespace penta::fem {
class Error:public std::runtime_error {public:int code;Error(int c,const std::string& s):std::runtime_error(s),code(c){}};
struct Result {
    penta_fem_result_v1 report{};
    std::vector<double> coordinates,values,visualizationCoordinates,visualizationValues;
    std::vector<uint64_t> visualizationFaces;
    bool meanConstraint=false;uint64_t nullspaceComponents=0;double compatibilityResidual=0,meanError=0;
    runtime::ParallelStatistics statistics;std::size_t threadsGranted=1;
};
struct SolverOptions {std::size_t threads=0;bool meanConstraint=false;double targetMean=0,compatibilityTolerance=1e-10;};
struct SystemResult {penta_fem_system_result_v1 report{};std::vector<double> coordinates,values,visualizationCoordinates,visualizationValues;std::vector<uint64_t> visualizationFaces,vertexDofs,elementDofs;runtime::ParallelStatistics statistics{};std::size_t threadsGranted=1;};
struct EvaluationResult {std::vector<double> values;uint64_t work=0;runtime::ParallelStatistics statistics{};};
struct TimeResult {SystemResult field;std::vector<double> times,values;penta_fem_time_result_v1 report{};};
struct MixedResult {penta_fem_mixed_result_v1 report{};std::vector<double> coordinates,values,visualizationCoordinates,visualizationValues;std::vector<uint64_t> componentOffsets,visualizationFaces;runtime::ParallelStatistics statistics;std::size_t threadsGranted=1;};
class Module final {
public:
    explicit Module(std::filesystem::path directory={});
    Result solve(penta_fem_request_v1,const std::shared_ptr<std::atomic_bool>& cancellation={},const std::function<void(std::size_t)>& charge={})const;
    Result solveMean(penta_fem_request_v1,double targetMean,double compatibilityTolerance=1e-10,const std::shared_ptr<std::atomic_bool>& cancellation={},const std::function<void(std::size_t)>& charge={})const;
    Result solveConfigured(penta_fem_request_v1,const SolverOptions&,const std::shared_ptr<std::atomic_bool>& cancellation={},const std::function<void(std::size_t)>& charge={})const;
    SystemResult solveSystem(penta_fem_system_request_v1,std::size_t threads=0,const std::shared_ptr<std::atomic_bool>& cancellation={},const std::function<void(std::size_t)>& charge={})const;
    EvaluationResult evaluate(penta_fem_system_evaluate_request_v1,std::size_t threads=0,const std::shared_ptr<std::atomic_bool>& cancellation={},const std::function<void(std::size_t)>& charge={})const;
    TimeResult solveTime(penta_fem_time_request_v1,std::size_t threads=0,const std::shared_ptr<std::atomic_bool>& cancellation={},const std::function<void(std::size_t)>& charge={},const std::function<bool(double,std::span<const double>,std::size_t,std::size_t)>& sample={})const;
    MixedResult solveMixed(penta_fem_mixed_request_v1,std::size_t threads=0,const std::shared_ptr<std::atomic_bool>& cancellation={},const std::function<void(std::size_t)>& charge={})const;
    static bool isLoaded()noexcept;
private:
    const penta_fem_api_v1& api()const;
    Result solveImpl(penta_fem_request_v1,const SolverOptions&,const std::shared_ptr<std::atomic_bool>&,const std::function<void(std::size_t)>&)const;
    std::filesystem::path path_;
};
}
