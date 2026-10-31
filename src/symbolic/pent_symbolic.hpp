#pragma once
#include "modules/pent_symbolic_api.h"
#include <array>
#include <atomic>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
namespace penta::symbolic {
class Error:public std::runtime_error{public:Error(int code,const std::string& text):std::runtime_error(text),status(code){}int status;};
struct Result {std::string text;std::array<double,5> coefficients{};double gamma=1;std::size_t workUnits=0;};
class PentModule {
public:
 explicit PentModule(std::filesystem::path directory={});
 Result execute(uint32_t operation,const std::string& expression,const std::string& reals,
   const std::string& pentarnions,const std::string& gamma,const std::string& variable,
   const std::string& replacement,const std::map<std::string,double>& bindings,
   std::uint64_t maxWork,const std::shared_ptr<std::atomic_bool>& cancellation={},
   const std::function<void(std::size_t)>& charge={})const;
private:
 std::filesystem::path path_;
 const penta_pent_symbolic_api_v1& api()const;
};
}
