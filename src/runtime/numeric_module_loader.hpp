#pragma once
#include "thread_budget.hpp"
#include "modules/numeric_api.h"
#include "modules/calculus_api.h"
#include <filesystem>
#include <stdexcept>
namespace penta::runtime {
namespace detail {
struct ParallelState {ParallelSettings settings;ParallelStatistics statistics;};
}
class NumericModuleError final : public std::runtime_error {
public:
    NumericModuleError(int status,const std::string& message):std::runtime_error(message),status(status){}
    int status;
};
class NumericModule final {
public:
    explicit NumericModule(std::filesystem::path directory={});
    [[nodiscard]] bool loaded() const;
    [[nodiscard]] const std::filesystem::path& path() const noexcept{return path_;}
    void vector(const penta_numeric_vector_request_v1&,const ParallelSettings&,
        const std::shared_ptr<std::atomic_bool>&,ParallelStatistics&) const;
    void reduce(const penta_numeric_reduce_request_v1&,const ParallelSettings&,
        const std::shared_ptr<std::atomic_bool>&,ParallelStatistics&) const;
    void calculus(const penta_numeric_calculus_request_v1&,const ParallelSettings&,
        const std::shared_ptr<std::atomic_bool>&,ParallelStatistics&) const;
private:
    const penta_numeric_api_v1& api() const;
    std::filesystem::path path_;
};
}
