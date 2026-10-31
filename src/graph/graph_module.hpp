#pragma once
#include "modules/graph_api.h"
#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace penta::graph {
class Error final : public std::runtime_error {
public:
    Error(int statusCode,const std::string& message):std::runtime_error(message),status(statusCode){}
    int status;
};
struct Result {
    penta_graph_result_v1 report{};
    std::vector<double> values;
};
class Module final {
public:
    explicit Module(std::filesystem::path directory={});
    Result execute(penta_graph_request_v1 request,std::size_t outputCapacity,
        const std::shared_ptr<std::atomic_bool>& cancellation={},
        const std::function<void(std::size_t)>& charge={}) const;
    static bool isLoaded() noexcept;
private:
    const penta_graph_api_v1& api() const;
    std::filesystem::path path_;
};
}
