#pragma once
#include "modules/sensitivity_api.h"
#include "runtime/thread_budget.hpp"
#include <atomic>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <vector>
namespace penta::sensitivity {
struct Error:std::runtime_error{int status;Error(int code,const std::string& message):std::runtime_error(message),status(code){}};
struct Result {std::vector<double> mu,muStar,sigma,first,total,firstLower,firstUpper,totalLower,totalUpper;penta_sensitivity_result_v1 report{};runtime::ParallelStatistics statistics{};std::size_t threadsGranted=1;};
class Module {
public:
    explicit Module(std::filesystem::path directory={});
    Result analyze(const penta_sensitivity_request_v1&,const std::shared_ptr<std::atomic_bool>&,std::size_t threads=0)const;
private:
    std::filesystem::path path_;const penta_sensitivity_api_v1& api()const;
};
}
