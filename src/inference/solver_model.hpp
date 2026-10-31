#pragma once
#include "inference_module.hpp"
#include "sensitivity/solver_summary.hpp"
#include <map>
#include <mutex>
#include <thread>

namespace penta::inference {
struct SolverPrediction {std::vector<double> values,jacobian;std::size_t nativeSolves=1;};
// Native sampled ODE/PDE model for independent Ceres refits. The observation
// lookup coordinate is an integer index into a complete solver trajectory,
// never a surrogate interpolated model or an interpreted user callback.
struct SolverModel {
    sensitivity::solver::Model model;
    std::vector<double> times;
    std::vector<std::size_t> observationCoordinates;
    std::vector<int> selectors; // parameter>=0; initial coordinate encoded -1-index
    std::vector<double> lower,upper;
    std::string jacobian="analytic";std::function<void()> solveStarted;
    void validate()const;
    std::size_t observationCount()const;
    SolverPrediction evaluate(std::span<const double> point,bool derivatives)const;
    std::string fingerprint()const;
};
class SolverModelBridge {
public:
    explicit SolverModelBridge(SolverModel);
    static int evaluate(void*,const double*,uint64_t,double,double*,double*,char*,uint64_t)noexcept;
    std::uint64_t solves()const noexcept{return solves_.load(std::memory_order_relaxed);}
    const SolverModel& model()const{return model_;}
private:
    struct Cache {std::vector<double> point;SolverPrediction prediction;bool derivatives=false,ready=false;};
    SolverModel model_;std::mutex mutex_;std::map<std::thread::id,std::shared_ptr<Cache>> caches_;std::atomic_uint64_t solves_{0};
};
}
