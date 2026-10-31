#include "runtime/interpreter.hpp"

#include <cmath>
#include <iostream>
#include <string>
#include <vector>

using namespace penta;
namespace {
int checks = 0, failures = 0;
void require(bool condition, const std::string& message) {
    ++checks;
    if (!condition) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
bool hasCode(const runtime::ExecutionResult& result, const std::string& code) {
    for (const auto& diagnostic : result.analysis.diagnostics)
        if (diagnostic.code == code) return true;
    for (const auto& diagnostic : result.runtimeDiagnostics)
        if (diagnostic.code == code) return true;
    return false;
}
void output(const std::string& source, const std::vector<std::string>& expected) {
    const auto result = runtime::execute(source);
    require(result.ok(), "run succeeds: " + source);
    require(result.output == expected, "output matches: " + source);
}
}

int main() {
    output("display((-2)*4611686018427387904); display((-9223372036854775808)*0);",
           {"-9223372036854775808", "0"});
    output("display((-9223372036854775808)-(-1)); display(9223372036854775807-1);",
           {"-9223372036854775807", "9223372036854775806"});
    for (const auto& source : {"display((-1)*(-9223372036854775808));",
                               "display(9223372036854775807-(-1));",
                               "display((-4611686018427387905)*2);"})
        require(hasCode(runtime::execute(source), "R1002"), "integer boundary overflow reports R1002");
    output("display(inv(pent([1e-308,0,0,0,0])));", {"1e+308"});
    output("display(pent([0,0,0,0,1e-300]));", {"1e-300*ell"});
    output("real d=1e-320; pentarnion p=pent([1e-320,0,0,0,0]); display(d\\p);", {"1"});
    output("display((i*j)*j-i*(j*j));", {"i + 0.5*k - 0.5*ell"});
    {
        const auto result = runtime::execute("pentarnion a=j-k-ell; display(a\\i);");
        require(result.status == runtime::ExecutionStatus::RuntimeError, "inconsistent equation fails at runtime");
        require(hasCode(result,"R1003"), "equation division has its dedicated runtime diagnostic");
        require(result.output.empty(), "rejected equation solve produces no fabricated display");
        require(!result.runtimeDiagnostics.empty() && result.runtimeDiagnostics.front().span.offset > 0,
                "equation solve failure retains expression location");
    }
    {
        runtime::InterpreterSession session;
        require(session.submit("integer count=7").ok(), "seed integer variable");
        auto result = session.submit("count=8; real invalid=i;");
        require(result.status == runtime::ExecutionStatus::StaticError && result.output.empty(),
                "whole console command checks before earlier assignments execute");
        require(session.submit("count").output == std::vector<std::string>{"7"},
                "static failure preserves earlier workspace value");
        result = session.submit("count=1.5");
        require(result.status == runtime::ExecutionStatus::StaticError && hasCode(result,"E1204"),
                "seeded integer binding cannot silently change to real");
        require(session.submit("count").output == std::vector<std::string>{"7"},
                "type rejection preserves seeded value");
        result = session.submit("real divisor=0; count=9; real missing=1/divisor;");
        require(result.status == runtime::ExecutionStatus::RuntimeError,
                "runtime error occurs after earlier sequential statements");
        require(session.submit("count").output == std::vector<std::string>{"9"},
                "completed statements remain committed after ordinary runtime error");
        require(hasCode(session.submit("missing"),"E1201"),
                "failed declaration never creates a phantom workspace binding");
        result = session.submit("real divisor=1;");
        require(hasCode(result,"E1202"), "an earlier successful declaration remains present after later runtime error");
    }
    {
        runtime::InterpreterSession session;
        require(session.submit("use_algebra(\"A_gamma\",gamma=0); pentarnion q=i;").ok(),
                "select quaternion sector and create typed element");
        require(session.submit("q*j").output == std::vector<std::string>{"k"},
                "seeded element shares session context");
        const auto blocked = session.submit("use_algebra(\"A_gamma\",gamma=1); display(q*j);");
        require(hasCode(blocked,"R1009") && blocked.output.empty(),
                "context change is rejected before operating on existing values");
        require(session.submit("q*j").output == std::vector<std::string>{"k"},
                "rejected context change leaves session context intact");
        const auto badFile = session.runFile("use_algebra(\"A_gamma\",gamma=1); real wrong=i;");
        require(badFile.status == runtime::ExecutionStatus::StaticError,
                "invalid file remains unexecuted");
        require(session.submit("q*j").output == std::vector<std::string>{"k"},
                "invalid file preserves both prior values and algebra context");
    }
    {
        runtime::InterpreterSession session;
        require(session.submit("real x=2").ok(), "initialize workspace for bounded echo");
        runtime::ExecutionOptions bounded;
        bounded.maxOutputBytes = 1;
        const auto result = session.submit("real y=3", {}, bounded);
        require(hasCode(result,"R1005") && result.output.empty(), "automatic echo obeys output budget");
        require(session.submit("y").output == std::vector<std::string>{"3"},
                "echo failure preserves an already completed declaration");
    }
    std::cout << "Independent interpreter review: " << checks << " checks, " << failures << " failures.\n";
    return failures == 0 ? 0 : 1;
}
