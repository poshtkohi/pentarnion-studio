#pragma once
#include "science_module.hpp"
#include "modules/science_extended_api.h"
#include "runtime/thread_budget.hpp"
#include <nlohmann/json.hpp>
namespace penta::science {
struct ExtendedResult {std::vector<double> values;nlohmann::json metadata;runtime::ParallelStatistics parallel;};
class ExtendedModule {
public:
    explicit ExtendedModule(std::filesystem::path directory={});
    ExtendedResult execute(penta_science_request_v2,nlohmann::json options,std::size_t outputCapacity,std::size_t threads=0,
        const std::shared_ptr<std::atomic_bool>& cancellation={},const std::function<void(std::size_t)>& charge={})const;
private:std::filesystem::path path_;
};
}
