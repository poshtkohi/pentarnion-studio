#pragma once
#include "dae_module.hpp"
#include "modules/structural_dae_api.h"
namespace penta::dae {
struct StructuralResult {std::vector<int32_t>equationOffsets,variableOffsets,matching;std::vector<uint8_t>algebraic;penta_dae_structure_result_v3 report{};};
struct InitialResult {std::vector<double>initial,derivative,differentialAcceleration;penta_dae_initialize_result_v3 report{};};
class StructuralModule {
public:
    explicit StructuralModule(std::filesystem::path directory={});
    StructuralResult structure(const penta_dae_structure_request_v3&,const std::shared_ptr<std::atomic_bool>& cancellation={},const std::function<void(std::size_t)>& charge={})const;
    InitialResult initialize(const penta_dae_initialize_request_v3&,const std::shared_ptr<std::atomic_bool>& cancellation={},const std::function<void(std::size_t)>& charge={})const;
private:std::filesystem::path path_;
};
}
