#pragma once
#include "dae_module.hpp"
#include "modules/linear_dae_api.h"
namespace penta::dae {
struct LinearResult {std::vector<double>times,values,derivatives,initial,initialDerivative,freeBasis,constraintProjector,reducedMatrix;penta_linear_dae_result_v2 report{};};
class LinearModule {
public:
    explicit LinearModule(std::filesystem::path directory={});
    penta_dae_analyze_result_v2 analyze(const penta_dae_analyze_request_v2&,const std::shared_ptr<std::atomic_bool>& cancellation={},const std::function<void(std::size_t)>& charge={})const;
    LinearResult solve(const penta_linear_dae_request_v2&,const std::shared_ptr<std::atomic_bool>& cancellation={},const std::function<void(std::size_t)>& charge={})const;
private:std::filesystem::path path_;
};
}
