#pragma once
#include "graph_module.hpp"
#include "modules/network_api.h"
#include "ode/native_ode.hpp"
#include "runtime/thread_budget.hpp"
namespace penta::networks {
using Error=graph::Error;
struct Graph {std::size_t nodes=0;bool directed=false;std::vector<uint64_t> endpoints;std::vector<double> weights;};
struct Options {
    uint32_t method=1; // 1 explicit RK4; 2 CVODES BDF/SPGMR.
    double t0=0,t1=10,step=.001,sampleStep=.01,maximumSeconds=60;
    double rtol=1e-7,atol=1e-9;std::size_t krylovDimension=20;
    std::size_t maximumSteps=1000000,maximumSamples=100000,maximumCoordinates=8000000,maximumBytes=256*1024*1024,maximumWork=1000000000,threads=0;
    bool generalCoupling=false;
    std::vector<double> diffusion{1};
    std::vector<double> edgeOperators; // physical-edge-major n-by-n ordered real operators
};
struct Result {
    std::vector<double> times,values,means,synchronization;
    std::vector<std::string> stateNames;std::vector<std::size_t> stateWidths;
    penta_network_result_v1 report{};
    runtime::ParallelStatistics parallel;
    std::size_t nodes=0,coordinates=0;double gamma=1;
    std::string key;bool cacheHit=false;
};
class Module final {
public:
    explicit Module(std::filesystem::path directory={});
    Result solve(const language::OdeDefinition&,const Graph&,double gamma,const std::vector<double>& initial,const std::vector<double>& parameters,const Options& = {},const ode::NativeOptions& = {})const;
    static bool isLoaded()noexcept;
private:
    const penta_network_api_v1& api()const;
    std::filesystem::path path_;
};
}
