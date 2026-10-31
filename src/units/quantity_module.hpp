#pragma once
#include "modules/units_api.h"
#include "runtime/thread_budget.hpp"
#include <filesystem>
#include <stdexcept>
#include <vector>
namespace penta::units {
struct Error:std::runtime_error{int status;Error(int code,const std::string& text):std::runtime_error(text),status(code){}};
struct Result{penta_si_unit_v1 unit{};uint64_t rows=0,columns=0,work=0;std::vector<double> values;runtime::ParallelStatistics parallel;};
struct ExtendedResult{penta_si_unit_v2 unit{};uint64_t rows=0,columns=0,work=0;std::vector<double> values;runtime::ParallelStatistics parallel;};
class Module{
public:
 explicit Module(std::filesystem::path directory={});
 penta_si_unit_v1 parse(const std::string&)const;
 penta_si_unit_v2 parseExtended(const std::string&)const;
 penta_si_unit_v2 derive(penta_units_request_v2)const;
 ExtendedResult execute(penta_units_request_v2,const runtime::ParallelSettings& = {},const std::shared_ptr<std::atomic_bool>& = {},const std::function<void(std::size_t)>& = {})const;
 Result execute(penta_units_request_v1,const runtime::ParallelSettings& = {},const std::shared_ptr<std::atomic_bool>& = {},const std::function<void(std::size_t)>& = {})const;
private:
 std::filesystem::path path_;const penta_units_api_v1& api()const;const penta_units_api_v2& extendedApi()const;
};
}
