#pragma once
#include "modules/chaos_api.h"
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>
namespace penta::chaos {
class Error final:public std::runtime_error {public:int status;Error(int code,const std::string& text):std::runtime_error(text),status(code){}};
struct QR {std::vector<double> basis,logarithms;double orthogonalityError=0;};
struct Recurrence {std::vector<double> matrix;uint64_t eligible=0,recurrent=0;};
class Module {
public:
    explicit Module(std::filesystem::path directory={});
    [[nodiscard]] bool loaded()const;
    [[nodiscard]] QR orthonormalize(const std::vector<double>& tangent,std::size_t dimension)const;
    [[nodiscard]] Recurrence recurrence(const std::vector<double>& states,std::size_t samples,std::size_t dimension,double radius,std::size_t theiler)const;
private:std::filesystem::path path_;const penta_chaos_api_v1& api()const;
};
}
