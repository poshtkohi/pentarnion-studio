#pragma once
#include "language/frontend.hpp"
#include "native_functions/build_plan.hpp"
#include "plot_style.hpp"
#include "graph_plot.hpp"
#include <atomic>
#include <array>
#include <cstddef>
#include <memory>
#include <filesystem>
#include <chrono>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <span>
#include <stdexcept>
#include <vector>

namespace penta::plotting {struct FieldAnimationData;struct PlotTemplate;}
namespace penta::runtime {
namespace detail {
// Internal timer bookkeeping. Compilation totals belong to the session and are
// never rolled back; restoring this state therefore excludes cancelled builds.
struct TimerState {
    using Clock = std::chrono::steady_clock;
    std::optional<Clock::time_point> start;
    double compilationAtStart = 0;
    void restart(Clock::time_point now,double compilationTotal) noexcept {
        start=now;compilationAtStart=compilationTotal;
    }
    void reset() noexcept {start.reset();compilationAtStart=0;}
    [[nodiscard]] double elapsed(Clock::time_point now,double compilationTotal) const {
        const double seconds=std::chrono::duration<double>(now-start.value()).count()-(compilationTotal-compilationAtStart);
        return seconds<0?0:seconds;
    }
};
}
enum class ExecutionStatus { Success, StaticError, RuntimeError, Cancelled };
enum class FigureActionKind {
    SelectNew, Select, Plot, Clear, Close, CloseAll, Dock, Undock,
    TileStudio, TileDesktop, SetTitle, SetXLabel, SetYLabel, SetZLabel, SetCaption, Subplot,
    ConfigurePlot, ExportPlot, LinkAxes, SetAnimation, SelectAnimationFrame, SetAnimationPlaying, SavePlotTemplate, ApplyPlotTemplate, LinkCameras
};
struct PlotVector {
    std::array<double, 5> coefficients{};
    std::string label;
    std::vector<double> originalCoefficients{};
    [[nodiscard]] std::span<const double> allCoefficients() const noexcept {
        if(!originalCoefficients.empty())return originalCoefficients;
        return coefficients;
    }
    [[nodiscard]] double coefficient(std::size_t index) const {
        const auto values=allCoefficients();
        if(index>=values.size())throw std::out_of_range("Plot coefficient index exceeds the original algebra dimension.");
        return values[index];
    }
};
struct PlotCurve { std::vector<std::array<double, 3>> points; std::string label; };
struct PlotMesh {
    std::vector<std::array<double,3>> points;
    std::vector<std::array<std::uint32_t,3>> triangles;
    std::vector<std::array<std::uint32_t,2>> segments;
    std::vector<double> scalars;
    std::string label;
};
struct FigureAction {
    FigureActionKind kind = FigureActionKind::SelectNew;
    int figure = 0;
    int dimensions = 0;
    std::array<int, 3> axes{{1, 2, 3}};
    std::vector<PlotVector> vectors;
    std::string algebraId;
    double gamma = 1.0;
    int rows = 0;
    int columns = 0;
    std::optional<std::string> name;
    std::optional<bool> numberTitle;
    std::string text;
    bool latex = true;
    int subplotIndex = 1;
    std::vector<PlotCurve> curves;
    std::optional<PlotMesh> mesh;
    std::optional<PlotGraph> graph;
    std::optional<PlotStylePatch> plotStyle;
    std::optional<FigureExportOptions> exportOptions;
    std::vector<int> linkedPanels;
    unsigned linkedAxesMask=3;
    std::shared_ptr<const plotting::FieldAnimationData> animation;
    std::size_t animationFrame=0;
    bool animationPlaying=false;
    std::vector<std::string> basisLabels{};
    std::shared_ptr<const plotting::PlotTemplate> plotTemplate;
};
struct ExecutionOptions {
    std::function<void(const std::vector<FigureAction>&)> figureProgress;
    std::shared_ptr<std::atomic_bool> cancellation;
    std::size_t maxOperations = 1000000;
    std::uint64_t maxNativeOperations = 1000000000; // Separately bounded PDE callback/assembly work.
    std::size_t maxOutputBytes = 1048576;
    std::size_t maxWorkspaceVariables = 50000;
    bool allowFigures = true;
    bool nativeFigureExport = false; // Headless PNG rendering on the invoking numerical worker.
    std::size_t maxFigureActions = 256;
    std::size_t maxPlotVectors = 256;
    std::size_t maxPlotPoints = 1000000; // Aggregate curve points emitted by one request; arrows excluded.
    std::filesystem::path odeCacheDirectory;
    std::filesystem::path numericModuleDirectory; // Optional explicit backend module directory.
    std::filesystem::path dataDirectory; // Relative data IO paths resolve here; empty uses current directory.
    std::size_t maxCallDepth = 32;
    std::size_t maxOdeSolves = 64;
    std::size_t nativeCompileJobs=0; // 0 automatic (up to4), explicit maximum0..64; at most8 compiler jobs.
    std::size_t maxSolutionCoordinates = 8000000; // Unique retained state samples; series aliases share storage.
    std::size_t maxRetainedSolutionCoordinates=32000000; // Separate aggregate cap including auxiliary ODE buffers.
    std::size_t maxMatrixElements = 1000000; // Per dense array; bounded before allocation.
    std::size_t maxMatrixCoordinates = 8000000; // Unique retained real/Pentarnion coordinates.
};
struct VariableSnapshot { std::string name, type, value; };
struct ExecutionResult {
    std::string sourceHash; // Invocation provenance; native structural keys remain independent of script edits.
    language::AnalysisResult analysis;
    std::vector<language::Diagnostic> runtimeDiagnostics;
    std::vector<std::string> output;
    std::vector<std::size_t> warningOutputLines; // Indices, never inferred from displayed text.
    std::vector<VariableSnapshot> variables;
    std::vector<FigureAction> figureActions;
    bool clearConsole = false; // clc: discard preceding transcript/output before these lines.
    ExecutionStatus status = ExecutionStatus::StaticError;
    std::size_t operations = 0;
    native_functions::BuildReport nativeCompilation;
    [[nodiscard]] bool ok() const noexcept { return status == ExecutionStatus::Success; }
};
// Analysis completes before any source statement is evaluated. Every call owns
// a fresh environment. Native ODE execution builds/reuses kernels in the supplied
// cache directory; static analysis never compiles or integrates a model.
[[nodiscard]] ExecutionResult execute(std::string_view source,
    const language::AnalysisOptions& options = {}, const ExecutionOptions& execution = {});

// A session is owned by one worker, never shared concurrently. Run File begins
// a fresh file scope; subsequent console submissions inspect/use its workspace.
// Console commands may omit their final semicolon for automatic result echo.
class InterpreterSession final {
public:
    InterpreterSession();
    ~InterpreterSession();
    InterpreterSession(InterpreterSession&&) noexcept;
    InterpreterSession& operator=(InterpreterSession&&) noexcept;
    InterpreterSession(const InterpreterSession&) = delete;
    InterpreterSession& operator=(const InterpreterSession&) = delete;
    [[nodiscard]] ExecutionResult runFile(std::string_view source,
        const language::AnalysisOptions& options = {}, const ExecutionOptions& execution = {});
    [[nodiscard]] ExecutionResult submit(std::string_view command,
        const language::AnalysisOptions& options = {}, const ExecutionOptions& execution = {});
    void reset();
    // Explicit experiment replay only. A loaded archive never calls this.
    // Accept the bounded numeric mt19937_64 snapshot before any submission.
    void restoreRandomState(std::string_view state);
private:
    struct State;
    std::unique_ptr<State> state_;
};
}
