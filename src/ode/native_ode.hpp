#pragma once

#include "language/frontend.hpp"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace penta::ode {

enum class Method { Adams, Bdf, Erk, Esdirk };
[[nodiscard]] const char* backendIdentity() noexcept;
[[nodiscard]] inline const char* reverseTableIdentity(Method method) noexcept {
    return method==Method::Erk?"dormand_prince_7_4_5":method==Method::Esdirk?"ark436l2sa_dirk_6_3_4":method==Method::Bdf?"cvodes_bdf":"cvodes_adams";
}

struct SolveOptions {
    double t0 = 0.0, t1 = 10.0, sampleStep = 0.01;
    double rtol = 1e-7, atol = 1e-9;
    Method method = Method::Adams;
    std::string jacobian = "analytic";
    std::size_t maxSteps = 1000000, maxSamples = 1000000;
    double maximumWallSeconds = 60.0;
    // Optional arbitrary requested output times in integration order. Endpoints
    // are inserted if absent. They use the solver interpolant, never a sample fit.
    std::vector<double> outputTimes;
    double initialStep=0, maximumStep=0, minimumStep=0;
    int maximumOrder=0; // 0 solver default; BDF<=5, Adams<=12.
    bool denseOutput=true;
    std::size_t maxDenseCoordinates=8000000;
    std::size_t maxRetainedCoordinates=32000000; // Values+slopes+time+sensitivities+events+dense data.
    // Real constant coordinate mass M*y'=f. Empty is identity; singular DAEs
    // are rejected and are not silently treated as explicit ODEs.
    std::vector<double> massMatrix;
    std::size_t eventCount=0;
    std::vector<int> eventDirections, eventTerminal;
    std::function<void(double,const double*,double*)> eventFunction;
    // Each direction selects a zero-based flat parameter or -1 for an
    // initial-state-only direction. Seeds are direction-major, n entries each.
    std::vector<int> sensitivityParameters;
    std::vector<double> sensitivityInitial;
    std::function<void(double,const double*,double*)> parameterJacobian;
    std::string sensitivityMethod="simultaneous";
    bool sensitivityErrorControl=true;
    // Row-major output/state+parameter/state+parameter Hessian of the raw RHS.
    // Used only for analytic ESDIRK's augmented variational-system Jacobian.
    std::function<void(double,const double*,double*)> stateHessian;
};

struct NativeOptions {
    std::filesystem::path cacheDirectory;
    std::shared_ptr<std::atomic_bool> cancellation;
    double compileTimeoutSeconds = 120.0;
    // Called on the invoking worker once per actual compiler attempt, including
    // failure/cancellation. Cache hits do not report. Callback exceptions are
    // contained by the native layer and never change compilation/solver results.
    std::function<void(double)> compilationFinished;
    // A host may precompile several translation units in parallel. Preserve
    // first-use cache reporting even when the binary was prepared in that batch.
    std::function<bool(const std::string&,bool)> cacheStatus;
    // Optional native host quota. Called on the invoking numerical worker;
    // callers running ensembles must reserve atomically and commit on owner.
    std::function<void(std::size_t)> chargeOperations;
    // Optional host observation on the invoking worker after a retained sample.
    // Borrowed full state; observer exceptions are contained and never change a solve.
    std::function<void(double,std::span<const double>)> sampleObserver;
};

class OdeError final : public std::runtime_error {
public:
    OdeError(std::string code, const std::string& message, language::SourceSpan span = {});
    std::string code;
    language::SourceSpan span;
    std::vector<language::DiagnosticFrame> frames{};
};

struct Statistics {
    bool cacheHit = false;
    std::string key;
    std::string method;
    std::string jacobian;
    std::size_t steps = 0, rhsEvaluations = 0, jacobianEvaluations = 0;
    double elapsedSeconds = 0.0;
    std::size_t eventEvaluations=0, sensitivityRhsEvaluations=0;
    std::size_t errorTestFailures=0, nonlinearIterations=0;
    double lastStep=0;
    int lastOrder=0;
    bool terminalEvent=false;
    std::size_t denseRhsEvaluations = 0; // Endpoint slopes, separate from integrator RHS counts.
};

struct DenseSegment {
    double first=0,last=0,center=0;
    unsigned degree=0;
    // Taylor coefficients about center, power-major then state coordinates.
    std::vector<double> coefficients;
};
struct Solution {
    std::vector<double> times;
    std::vector<std::string> stateNames;
    std::vector<std::size_t> stateWidths;
    std::vector<double> values; // sample-major, then states in declaration order; widths 1/5
    Statistics stats;
    // Compiled RHS at each stored sample, same layout as values. These define
    // a cubic Hermite dense interpolant whose accuracy depends on sampleStep;
    // they are not CVODE/ARKODE's internal interpolation polynomial. Older saved
    // solutions may omit the slopes and remain usable as sampled data.
    std::vector<double> derivatives;
    std::vector<DenseSegment> denseSegments;
    std::size_t denseCoordinateCount=0;
    std::vector<double> eventTimes,eventValues;
    std::vector<int> eventIndices,eventDirections;
    std::size_t sensitivityCount=0;
    std::vector<int> sensitivityParameterIndices;
    std::vector<double> sensitivities; // sample-major, direction-major, coordinate
    [[nodiscard]] std::size_t coordinateCount() const noexcept;
    [[nodiscard]] std::size_t retainedCoordinateCount() const noexcept;
};

// Both native RHS and the automatic full real-coordinate Jacobian are always
// compiled. Integrator/Jacobian selection, values, gamma and tolerances are not
// part of the structural cache identity. No interpreted execution fallback.
[[nodiscard]] std::shared_ptr<const Solution> solve(
    const language::OdeDefinition&, double gamma,
    const std::vector<double>& initial, const std::vector<double>& parameters,
    const SolveOptions& = {}, const NativeOptions& = {});

struct NativeEvaluation {
    std::vector<double> rhs;
    std::vector<double> jacobian; // row-major d(rhs coordinate)/d(state coordinate)
    bool cacheHit = false;
    std::string key;
};

// A prepared immutable native kernel can be shared by independent integrations.
// Each solve owns its SUNDIALS state, workspace, events and cancellation options.
// Preparation generates/checks/loads once; changing run values never rebuilds.
class PreparedModel final {
public:
    explicit PreparedModel(const language::OdeDefinition&,const NativeOptions& = {});
    [[nodiscard]] std::size_t coordinateCount() const noexcept;
    [[nodiscard]] std::size_t parameterCount() const noexcept;
    [[nodiscard]] const std::string& key() const noexcept;
    [[nodiscard]] bool cacheHit() const noexcept;
    [[nodiscard]] NativeEvaluation evaluate(double gamma,double time,
        const std::vector<double>& state,const std::vector<double>& parameters,
        const NativeOptions& = {}) const;
    // Allocation-free calls through the identical loaded RHS/Jacobian entry.
    // Caller-owned output buffers may be partial if the callback throws; solvers
    // must evaluate candidates separately from their accepted scientific state.
    void evaluateValues(double gamma,double time,std::span<const double> state,
        std::span<const double> parameters,std::span<double> rhs,const NativeOptions& = {})const;
    void evaluateJacobian(double gamma,double time,std::span<const double> state,
        std::span<const double> parameters,std::span<double> rhs,std::span<double> jacobian,
        const NativeOptions& = {})const;
    [[nodiscard]] std::shared_ptr<const Solution> solve(double gamma,
        const std::vector<double>& initial,const std::vector<double>& parameters,
        const SolveOptions& = {},const NativeOptions& = {}) const;
private:
    struct State;
    std::shared_ptr<const State> state_;
};

// The same compiled kernel used by CVODE/ARKODE; exposed for independent numerical QA.
[[nodiscard]] NativeEvaluation evaluateNative(
    const language::OdeDefinition&, double gamma, double time,
    const std::vector<double>& state, const std::vector<double>& parameters,
    const NativeOptions& = {});

// Evaluate retained native integrator interpolation polynomials. A restored
// legacy solution without segments can continue using its documented Hermite path.
[[nodiscard]] std::vector<double> denseEvaluate(const Solution&,double time);
struct AdjointResult {
    std::shared_ptr<const Solution> solution;
    double objective=0;
    std::vector<double> parameterGradient,initialGradient;
    std::size_t backwardSteps=0,checkpoints=0;
    double runningCost=0;
};
using TerminalObjective=std::function<double(double,const double*,const double*,double*,double*)>;
// Running evaluators return L(t,y,p), filling L_y/L_p only when their output
// pointer is non-null. The forward integral and reverse derivatives use native
// callbacks; no finite-difference or VM evaluator is substituted.
using RunningObjective=TerminalObjective;
[[nodiscard]] AdjointResult solveAdjoint(const language::OdeDefinition&,double gamma,
    const std::vector<double>& initial,const std::vector<double>& parameters,
    const TerminalObjective&,const SolveOptions& = {},const NativeOptions& = {},
    const RunningObjective& = {});
} // namespace penta::ode
