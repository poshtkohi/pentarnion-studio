#pragma once
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>

namespace penta::discovery {
enum class FailureReason : std::uint32_t {
    None=0,Cancelled=1,WallTime=2,InvalidInput=3,StepLimit=4,
    NonlinearConvergence=5,LinearSolver=6,NonfiniteDomain=7,
    SingularRhs=8,OtherNumerical=9,NativeWorkLimit=10
};
inline const char* failureReasonName(FailureReason reason) noexcept {
    switch(reason){
    case FailureReason::None:return "none";
    case FailureReason::Cancelled:return "cancelled";
    case FailureReason::WallTime:return "wall_time_budget";
    case FailureReason::InvalidInput:return "invalid_input";
    case FailureReason::StepLimit:return "step_limit_or_possible_stiffness";
    case FailureReason::NonlinearConvergence:return "nonlinear_convergence";
    case FailureReason::LinearSolver:return "linear_solver_failure";
    case FailureReason::NonfiniteDomain:return "nonfinite_or_domain";
    case FailureReason::SingularRhs:return "singular_rhs";
    case FailureReason::OtherNumerical:return "other_numerical_failure";
    case FailureReason::NativeWorkLimit:return "native_work_budget";
    }
    return "unknown";
}
inline FailureReason classifyFailure(std::string_view code,std::string message,bool cancelled=false){
    std::transform(message.begin(),message.end(),message.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
    auto has=[&](std::string_view term){return message.find(term)!=std::string::npos;};
    if(cancelled||code=="O1006"||has("cancelled"))return FailureReason::Cancelled;
    if(has("operation budget")||has("native work budget"))return FailureReason::NativeWorkLimit;
    if(code=="O1003"||has("wall-time")||has("wall time"))return FailureReason::WallTime;
    if(code=="O1001")return FailureReason::InvalidInput;
    if(has("division by zero")||has("divide by zero")||has("zero denominator"))return FailureReason::SingularRhs;
    if(has("nonfinite")||has("non-finite")||has("domain")||has("nan")||has("infinity"))return FailureReason::NonfiniteDomain;
    if(has("linear solver")||has("singular matrix")||has("factorization")||has("factorisation"))return FailureReason::LinearSolver;
    if(has("nonlinear")||has("convergence failure")||has("convergence test"))return FailureReason::NonlinearConvergence;
    if(has("max_steps")||has("step limit")||has("too much work")||has("mxstep")||has("maximum number of steps"))return FailureReason::StepLimit;
    return FailureReason::OtherNumerical;
}
}
