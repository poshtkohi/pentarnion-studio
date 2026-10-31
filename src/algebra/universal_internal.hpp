#pragma once
#include "universal.hpp"
#include <nlohmann/json.hpp>

namespace penta::algebra::universal {
struct Definition::Data {
    std::string name,id,hash,sourceHash,sourceJson,builtinKind,convention,coefficientDomain;
    std::size_t dimension=0;
    std::vector<std::string> basis;
    std::vector<Term> terms;
    std::vector<double> table,unit,conjugation,metric;
    std::vector<std::string> unitExact,conjugationExact,metricExact;
    Properties properties;
    std::string canonical;
};
namespace detail {
[[nodiscard]] std::string sha256(std::string_view);
[[nodiscard]] std::shared_ptr<Definition::Data> makeBuiltin(std::string_view,const BuiltinOptions&);
[[nodiscard]] double finite(long double);
[[nodiscard]] std::size_t slot(std::size_t n,std::size_t i,std::size_t j,std::size_t k);
void finishDefinition(Definition::Data&);
void builtinProduct(std::string_view,std::span<const double>,std::span<const double>,std::span<double>);
}
}
