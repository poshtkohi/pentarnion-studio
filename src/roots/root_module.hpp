#pragma once
#include "modules/roots_api.h"
#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
namespace penta::roots {
class Error final:public std::runtime_error {
public:int status;Error(int code,const std::string& text):std::runtime_error(text),status(code){}
};
struct Result {std::vector<double> solution,residual;penta_roots_result_v1 report{};};
class Module {
public:
    explicit Module(std::filesystem::path directory={});
    [[nodiscard]] bool loaded()const;
    [[nodiscard]] Result solve(const penta_roots_request_v1&,
        const std::shared_ptr<std::atomic_bool>& cancellation={},
        const std::function<void(std::size_t)>& charge={},const std::function<bool(const penta_iteration_snapshot_v1&)>& observer={})const;
private:std::filesystem::path path_;const penta_roots_api_v1& api()const;
};
}
