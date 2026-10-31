#pragma once
#include "modules/mechanics_api.h"
#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <stdexcept>
#include <vector>
namespace penta::mechanics {
struct Error:std::runtime_error{int status;Error(int code,const std::string& message):std::runtime_error(message),status(code){}};
struct Result {std::vector<double> times,q,velocity,momentum,energy,modelValue,kinetic;penta_mechanics_result_v1 report{};};
class Module {
public:
    explicit Module(std::filesystem::path directory={});
    Result solve(const penta_mechanics_request_v1&,const std::shared_ptr<std::atomic_bool>&,const std::function<void(std::size_t)>&)const;
private:
    std::filesystem::path path_;const penta_mechanics_api_v1& api()const;
};
}
