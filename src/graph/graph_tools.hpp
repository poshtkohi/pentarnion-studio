#pragma once
#include "graph_module.hpp"
#include "modules/graph_tools_api.h"
namespace penta::graph_tools {
using Error=graph::Error;
struct Result {
    penta_graph_tools_result_v1 report{};
    std::vector<uint64_t> indices,pointers,endpoints;
    std::vector<double> values;
};
class Module final {
public:
    explicit Module(std::filesystem::path directory={});
    Result execute(penta_graph_tools_request_v1,std::size_t entries,std::size_t pointers,std::size_t endpoints,
        const std::shared_ptr<std::atomic_bool>& cancellation={},const std::function<void(std::size_t)>& charge={})const;
    static bool isLoaded()noexcept;
private:
    const penta_graph_tools_api_v1& api()const;
    std::filesystem::path path_;
};
}
