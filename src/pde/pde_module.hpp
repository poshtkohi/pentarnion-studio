#pragma once
#include "modules/pde_api.h"
#include "modules/pde_grid_api.h"
#include "modules/pde_stiff_api.h"
#include "runtime/thread_budget.hpp"
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <span>
#include <vector>
namespace penta::pde {
struct Error:std::runtime_error{int status;Error(int s,const std::string& m):std::runtime_error(m),status(s){}};
struct Result{std::vector<double> times,values,means,rms;penta_pde_result_v1 report{};
    runtime::ParallelStatistics statistics{};std::size_t threadsGranted=1;
    std::uint64_t linearSolves=0,factorizations=0;};
struct Mesh{std::vector<double> xyz,scalars;std::vector<std::uint32_t> faces,edges;};
struct StiffResult {std::vector<double> times,values,means,rms;penta_pde_stiff_result_v1 report{};runtime::ParallelStatistics statistics{};std::size_t threadsGranted=1;};
class Module{
public:
    explicit Module(std::filesystem::path directory={});
    Result solve(const penta_pde_request_v1&,const std::shared_ptr<std::atomic_bool>&,
                 const std::function<void(std::size_t)>&,std::size_t threads=1)const;
    Result solveGrid(const penta_pde_grid_request_v1&,const std::shared_ptr<std::atomic_bool>&,
                 const std::function<void(std::size_t)>&,std::size_t threads=1)const;
    StiffResult solveStiff(const penta_pde_stiff_request_v1&,const std::shared_ptr<std::atomic_bool>&,
                 const std::function<void(std::size_t)>&,std::size_t threads=1,
                 const std::function<bool(double,std::span<const double>,std::size_t,std::size_t)>& sample={})const;
    Mesh geometry(const penta_geometry_request_v1&,std::size_t maximumPoints,const std::shared_ptr<std::atomic_bool>&,const std::function<void(std::size_t)>&)const;
private:std::filesystem::path path_;const penta_pde_api_v1& api()const;
};
}
