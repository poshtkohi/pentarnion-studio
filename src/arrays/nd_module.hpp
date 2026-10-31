#pragma once
#include "modules/nd_api.h"
#include "runtime/thread_budget.hpp"
#include <array>
#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
namespace penta::arrays {
class Error:public std::runtime_error{public:Error(int code,const std::string& message):std::runtime_error(message),status(code){}int status;};
struct Result {penta_nd_report_v1 report{};std::vector<double> values;runtime::ParallelStatistics parallel;};
using MapCallback=std::function<void(const double*,uint32_t,double*,uint32_t)>;
class Module {
public:
 explicit Module(std::filesystem::path directory={});
 Result execute(penta_nd_request_v1,std::size_t capacity,const runtime::ParallelSettings& = {},
  const std::shared_ptr<std::atomic_bool>& cancellation={},const std::function<void(std::size_t)>& charge={},const MapCallback& map={})const;
private:std::filesystem::path path_;const penta_nd_api_v1& api()const;
};
}
