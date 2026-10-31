#pragma once
#include "modules/cas_api.h"
#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
namespace penta::symbolic {
struct CasError:std::runtime_error {int status;CasError(int code,const std::string& text):std::runtime_error(text),status(code){}};
class CasModule {
public:
    explicit CasModule(std::filesystem::path directory={});
    std::string execute(const std::string& json,std::uint64_t maximumWork,const std::shared_ptr<std::atomic_bool>& cancellation={},const std::function<void(std::size_t)>& charge={})const;
private:
    std::filesystem::path path_;
    const penta_cas_api_v1& api()const;
};
}
