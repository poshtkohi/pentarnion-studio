#pragma once
#include "native_function.hpp"
#include <memory>
#include <string>
#include <vector>
namespace penta::native_functions {
struct ThirdRequest {
    // Empty selects every flattened real input coordinate. Otherwise unique,
    // zero-based indices; the three directions have active.size() entries.
    std::vector<std::size_t> active;
    std::vector<double> u,v,w;
};
struct ThirdResult {
    std::vector<double> values,third;
    std::size_t columns=0,nativeEvaluations=1,threadsUsed=1;
    bool cacheHit=false;
    std::string key;
};
// One native trilinear evaluation. Neither a dense tensor nor finite differences
// are formed. The caller reserves Studio's shared CPU budget for this serial
// scalar kernel; construction performs compilation before that reservation.
class ThirdCallback {
public:
    ThirdCallback()=default;
    ThirdCallback(const language::FunctionDefinition&,const Options&);
    [[nodiscard]] std::size_t inputCount()const;
    [[nodiscard]] std::size_t outputCount()const;
    [[nodiscard]] const std::vector<language::Type>& outputTypes()const;
    [[nodiscard]] const std::vector<std::size_t>& inputWidths()const;
    [[nodiscard]] const std::vector<std::size_t>& outputWidths()const;
    [[nodiscard]] const std::string& key()const;
    [[nodiscard]] bool cacheHit()const;
    [[nodiscard]] ThirdResult evaluate(double gamma,const std::vector<double>& input,const ThirdRequest&)const;
private:
    struct State;std::shared_ptr<State> state_;
};
void validateThirdCallback(const language::FunctionDefinition&);
[[nodiscard]] std::string thirdFingerprint(const language::FunctionDefinition&);
}
