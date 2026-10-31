#pragma once
#include "modules/maps_api.h"
#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
namespace penta::maps {
class Error final:public std::runtime_error {
public:int status;Error(int code,const std::string& text):std::runtime_error(text),status(code){}
};
struct Result {std::vector<double> solution,states,indices,jacobian;penta_maps_result_v1 report{};};
class Module {
public:
    explicit Module(std::filesystem::path directory={});
    [[nodiscard]] bool loaded()const;
    [[nodiscard]] Result iterate(const penta_maps_request_v1&,
        const std::shared_ptr<std::atomic_bool>& cancellation={},
        const std::function<void(std::size_t)>& charge={})const;
private:std::filesystem::path path_;const penta_maps_api_v1& api()const;
};
}
