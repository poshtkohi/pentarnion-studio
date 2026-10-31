#pragma once
#include "language/frontend.hpp"
#include "ode/native_ode.hpp"
#include <array>
#include <functional>
#include <vector>
#include <utility>
namespace penta::native_functions {
struct Value {language::Type type=language::Type::Real;std::array<double,5> coefficients{};};
struct Options {
    ode::NativeOptions build;
    // Invoked synchronously on the callback evaluator thread. Concurrent hosts
    // must provide a thread-safe quota adapter; interpreter-owned counters must
    // not be touched from pool workers.
    std::function<void(std::size_t)> chargeOperations;
};
struct Result {Value value;bool cacheHit=false;std::string key;};
// Pure named Real/Pentarnion functions with locals, assignment, if and return.
// Unsupported constructs are diagnosed, with no interpreted execution fallback.
void validate(const language::FunctionDefinition&);
[[nodiscard]] std::string functionFingerprint(const language::FunctionDefinition&);
// Compile/cache only; never evaluate a callback at a guessed input point.
std::pair<std::string,bool> prepare(const language::FunctionDefinition&,const Options&);
Result evaluate(const language::FunctionDefinition&,double gamma,
                const std::vector<Value>& arguments,const Options&);
}
