#pragma once
#include "modules/petsc_api.h"
#include "runtime/thread_budget.hpp"
#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <stdexcept>
#include <vector>
namespace penta::petsc {
struct Error:std::runtime_error{int status;Error(int code,const std::string& text):std::runtime_error(text),status(code){}};
struct Result {std::vector<double> solution,residual;penta_petsc_result_v1 report{};runtime::ParallelStatistics statistics{};std::size_t threadsGranted=1;};
class Module {
public:
    explicit Module(std::filesystem::path directory={});
    Result solve(const penta_petsc_request_v1&,std::size_t threads,const std::shared_ptr<std::atomic_bool>& cancellation,const std::function<void(std::size_t)>& charge,const std::function<int(const penta_iteration_snapshot_v1&,std::string&)>& observer={})const;
    static bool isLoaded()noexcept;
private:
    std::filesystem::path path_;const penta_petsc_api_v1& api()const;
};
}
