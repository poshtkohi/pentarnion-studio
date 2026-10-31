#pragma once
#include "native_function.hpp"
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace penta::native_functions {
enum class DerivativeMethod { Automatic, FiniteDifference };
struct DerivativeRequest {
    // Zero-based flattened real input coordinates; empty selects every input.
    std::vector<std::size_t> active;
    unsigned order=1; // 0 values, 1 first derivatives, 2 mixed second derivatives.
    DerivativeMethod method=DerivativeMethod::Automatic;
    // An optional direction has active.size entries. At order1 compute J*v;
    // at order2 compute H*v per output without materializing the Hessian.
    std::vector<double> direction;
    double step=0; // 0 selects an order-appropriate finite-difference spacing.
    std::size_t requestedThreads=0; // Per-call maximum; current scalar AD is serial.
};
struct DerivativeResult {
    std::vector<double> values;
    std::vector<double> jacobian; // output-major, columns as below.
    std::vector<double> second; // output-major H rows/columns, or output-major H*v.
    std::size_t columns=0;
    std::size_t nativeEvaluations=0;
    bool cacheHit=false;
    std::size_t threadsUsed=1;
    std::string key;
};
class Callback {
public:
    Callback()=default;
    Callback(const language::FunctionDefinition&,const Options&);
    [[nodiscard]] std::size_t inputCount()const;
    [[nodiscard]] std::size_t outputCount()const;
    [[nodiscard]] const std::vector<language::Type>& outputTypes()const;
    // Per declared argument/result widths; all coordinates are retained in the
    // flattened C ABI, including generic elements and explicit complex values.
    [[nodiscard]] const std::vector<std::size_t>& inputWidths()const;
    [[nodiscard]] const std::vector<std::size_t>& outputWidths()const;
    [[nodiscard]] const std::string& key()const;
    [[nodiscard]] bool cacheHit()const;
    [[nodiscard]] DerivativeResult evaluate(double gamma,const std::vector<double>& input,
                                             const DerivativeRequest& request={})const;
    // Value-only evaluation through the same cached entry. Successful calls
    // allocate no host buffers, copy no cache identity and seed no derivatives.
    // output must contain exactly outputCount() entries; failures leave it intact.
    void evaluateValues(double gamma,const std::vector<double>& input,std::span<double> output)const;
private:
    struct State;
    std::shared_ptr<State> state_;
};
void validateCallback(const language::FunctionDefinition&);
// Pure generated-code identity, independent of AST allocation/source positions.
[[nodiscard]] std::string callbackFingerprint(const language::FunctionDefinition&);
}
