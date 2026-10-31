#pragma once
#include "modules/science_api.h"
#include <atomic>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
namespace penta::science {
class Error:public std::runtime_error{public:Error(int code,const std::string& text):std::runtime_error(text),status(code){}int status;};
class Module {
public:
 explicit Module(std::filesystem::path directory={});
 std::size_t numeric(penta_science_request_v1&,const std::shared_ptr<std::atomic_bool>&)const;
 std::string symbolic(uint32_t,const std::string&,const std::string&,const std::string&,double&,const std::shared_ptr<std::atomic_bool>&)const;
private:
 std::filesystem::path path_;
 const penta_science_api_v1& api()const;
};
}
