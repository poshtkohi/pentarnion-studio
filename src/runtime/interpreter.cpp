#include "interpreter.hpp"
#include "matrix_value.hpp"
#include "numeric_module_loader.hpp"
#include "module_inventory.hpp"
#include "research_capture.hpp"
#include "research/research_archive.hpp"
#include "science/extended_science.hpp"
#include "dae/linear_dae.hpp"
#include "dae/structural_dae.hpp"
#include "native_functions/dependency_pattern.hpp"
#include "arrays/typed_tensor.hpp"
#include "modules/calculus_api.h"
#include <complex>
#include <sstream>
#include "data_io.hpp"
#include <nlohmann/json.hpp>
#include "algebra/pentarnion.hpp"
#include "algebra/elementary.hpp"
#include "algebra/universal.hpp"
#include "ode/native_ode.hpp"
#include "native_functions/native_function.hpp"
#include "native_functions/native_callback.hpp"
#include "native_functions/universal_native.hpp"
#include "native_functions/third_callback.hpp"
#include "runtime/linalg_module_loader.hpp"
#include "roots/root_module.hpp"
#include "ode/advanced_ode_native.hpp"
#include "ode/hybrid_ode.hpp"
#include "fitting/fit_module.hpp"
#include "inference/inference_module.hpp"
#include "inference/solver_model.hpp"
#include "optimization/opt_module.hpp"
#include "maps/map_module.hpp"
#include "maps/ensemble.hpp"
#include "pde/pde_module.hpp"
#include "petsc/petsc_solver.hpp"
#include "symbolic/cas_module.hpp"
#include "pde/pde_experiment_module.hpp"
#include "chaos/chaos_module.hpp"
#include "discovery/discovery_module.hpp"
#include "discovery/verification.hpp"
#include "orbits/orbits_module.hpp"
#include "graph/graph_module.hpp"
#include "graph/graph_tools.hpp"
#include "graph/network_solver.hpp"
#include "geometry/geometry_module.hpp"
#include "geometry/extra_geometry.hpp"
#include "fem/fem_solver.hpp"
#include "orbits/bifurcation.hpp"
#include "research/experiment_bundle.hpp"
#include "sensitivity/sensitivity_module.hpp"
#include "sensitivity/solver_summary.hpp"
#include "dae/dae_module.hpp"
#include "mechanics/mechanics_module.hpp"
#include "mechanics/constrained_mechanics.hpp"
#include "units/quantity_module.hpp"
#include "arrays/nd_module.hpp"
#include "plotting/native_export.hpp"
#include "plotting/plot_template.hpp"
#include "plotting/field_animation.hpp"
#include "plotting/field_video.hpp"
#include "plotting/graph_view.hpp"
#include "symbolic/pent_symbolic.hpp"
#include "science/science_module.hpp"
#include "ode/native_internal.hpp"
#include "evolution/evolution_module.hpp"
#include "help_catalog.hpp"
#include "modules/iteration_api.h"
#include <random>
#include <iterator>

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <chrono>
#include <limits>
#include <map>
#include <set>
#include <numeric>
#include <stdexcept>
#include <exception>
#include <variant>
#include <unordered_set>
#include <mutex>

namespace penta::runtime {
namespace {
using I = std::int64_t;
using P = algebra::Pentarnion;
using UniversalDefinition=algebra::universal::DefinitionPtr;
using UniversalElement=algebra::universal::Element;
using language::Type;
using language::Expr;
using language::ExprKind;
using language::SourceSpan;
struct IntegerRange { I start, step, stop; };
struct RealRange { double start, step, stop; };
struct CapturedEnvironment;
struct FunctionValue {std::shared_ptr<const language::FunctionDefinition> definition;std::shared_ptr<const CapturedEnvironment> captures;std::string builtin;};
struct TupleData;struct RecordData;
using TupleValue=std::shared_ptr<const TupleData>;
using RecordValue=std::shared_ptr<const RecordData>;
struct ModelValue { std::shared_ptr<const language::OdeDefinition> definition; };
struct SolutionValue { std::shared_ptr<const ode::Solution> data; std::shared_ptr<const language::OdeDefinition> definition; };
struct RealSeries { std::shared_ptr<const ode::Solution> data; std::size_t offset=0; bool times=false; };
struct PentSeries { std::shared_ptr<const ode::Solution> data; std::size_t offset=0; };
struct UniversalSeries { std::shared_ptr<const ode::Solution> data; std::size_t offset=0; UniversalDefinition algebra; bool complex=false; };
struct UniversalMatrixData { UniversalDefinition algebra; std::size_t rows=0,columns=0; std::vector<double> values; std::vector<std::size_t> fixedShape{}; };
using UniversalMatrix=std::shared_ptr<const UniversalMatrixData>;
struct ArraySeries { std::shared_ptr<const ode::Solution> data; std::size_t offset=0; Type valueType=Type::RealMatrix; std::shared_ptr<const language::ValueSchema> schema; };
using Matrix = detail::DenseMatrix;
using MatrixValue = detail::MatrixValue;
using TensorValue = detail::TensorValue;
using Random = detail::RandomState;
using Value = std::variant<I, double, P, IntegerRange, RealRange, std::string,ModelValue,SolutionValue,RealSeries,PentSeries,MatrixValue,FunctionValue,std::complex<double>,TupleValue,RecordValue,UniversalDefinition,UniversalElement,UniversalSeries,UniversalMatrix,ArraySeries,TensorValue>;
using Environment = std::map<std::string, Value>;
struct CapturedEnvironment {Environment values;};
struct TupleData {std::vector<Value> values;};
struct RecordData {std::map<std::string,Value> fields;};
using Timer = detail::TimerState;
Type valueType(const Value& value) {
    if(std::holds_alternative<TensorValue>(value))return Type::Tensor;
    if(std::holds_alternative<UniversalMatrix>(value))return Type::ElementMatrix;
    if(std::holds_alternative<ArraySeries>(value))return Type::ArraySeries;
    if(std::holds_alternative<UniversalSeries>(value))return Type::ElementSeries;
    if(std::holds_alternative<UniversalDefinition>(value))return Type::Algebra;
    if(std::holds_alternative<UniversalElement>(value))return Type::Element;
    if(std::holds_alternative<FunctionValue>(value))return Type::Function;
    if(std::holds_alternative<std::complex<double>>(value))return Type::Complex;
    if(std::holds_alternative<TupleValue>(value))return Type::Tuple;
    if(std::holds_alternative<RecordValue>(value))return Type::Record;
    if(const auto* matrix=std::get_if<MatrixValue>(&value)) return (*matrix)->pentarnion?Type::PentarnionMatrix:(*matrix)->logical?Type::LogicalMatrix:Type::RealMatrix;
    constexpr Type types[]{Type::Integer, Type::Real, Type::Pentarnion,
                           Type::IntegerRange, Type::RealRange, Type::String,Type::OdeModel,Type::Solution,Type::RealSeries,Type::PentarnionSeries};
    return types[value.index()];
}

struct Failure final : std::runtime_error {
    std::string code;
    SourceSpan span;
    std::vector<language::DiagnosticFrame> frames;
    Failure(std::string c, std::string message, SourceSpan s)
        : std::runtime_error(std::move(message)), code(std::move(c)), span(s) {}
};
Failure functionFailure(std::string code,std::string message,SourceSpan callSpan,
    const language::FunctionDefinition& function,SourceSpan bodySpan,
    std::vector<language::DiagnosticFrame> frames={}) {
    Failure failure(std::move(code),std::move(message),callSpan);failure.frames=std::move(frames);
    const bool duplicate=!failure.frames.empty()&&function.origin&&failure.frames.back().function==function.name&&failure.frames.back().source==function.origin->source;
    if(!duplicate&&function.origin&&function.origin->source&&failure.frames.size()<32){
        const auto& text=function.origin->source;const auto offset=std::min(bodySpan.offset,text->size());
        bodySpan={offset,std::min(bodySpan.length,text->size()-offset)};
        failure.frames.push_back({function.name,function.origin->identity,ode::detail::sha256(*text),bodySpan,text});
    }
    return failure;
}
[[noreturn]] void fail(std::string code, std::string message, SourceSpan span) {
    Failure failure(std::move(code),std::move(message),span);
    if(const auto current=std::current_exception()){
        try{std::rethrow_exception(current);}
        catch(const ode::OdeError& cause){failure.frames=cause.frames;}
        catch(const Failure& cause){failure.frames=cause.frames;}
        catch(...){}
    }
    throw failure;
}
double finite(double value, SourceSpan span) {
    if (!std::isfinite(value)) fail("R1001", "The result is outside the finite real domain.", span);
    return value;
}
I integerAdd(I a, I b, SourceSpan span) {
    if ((b > 0 && a > std::numeric_limits<I>::max() - b) ||
        (b < 0 && a < std::numeric_limits<I>::min() - b))
        fail("R1002", "Signed 64-bit integer addition overflow.", span);
    return a + b;
}
I integerSubtract(I a, I b, SourceSpan span) {
    if ((b > 0 && a < std::numeric_limits<I>::min() + b) ||
        (b < 0 && a > std::numeric_limits<I>::max() + b))
        fail("R1002", "Signed 64-bit integer subtraction overflow.", span);
    return a - b;
}
I integerMultiply(I a, I b, SourceSpan span) {
    const I hi = std::numeric_limits<I>::max(), lo = std::numeric_limits<I>::min();
    const bool overflow = a > 0 ? (b > 0 ? a > hi / b : b < lo / a)
        : a < 0 ? (b > 0 ? a < lo / b : b < hi / a) : false;
    if (overflow) fail("R1002", "Signed 64-bit integer multiplication overflow.", span);
    return a * b;
}
std::string realText(double value) {
    if (value == 0.0) return "0";
    char buffer[128];
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value,
                                     std::chars_format::general, 15);
    if (result.ec != std::errc{}) throw std::runtime_error("Cannot format a real result.");
    return {buffer, result.ptr};
}
std::string integerText(I value) {
    char buffer[32];
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
    return {buffer, result.ptr};
}
std::string valueText(const Value& value) {
    if(const auto* x=std::get_if<TensorValue>(&value)){
        const auto& tensor=**x;std::string text="tensor<"+std::string(language::typeName(tensor.entryType))+"> shape=[";
        for(std::size_t n=0;n<tensor.shape.size();++n){if(n)text+=",";text+=std::to_string(tensor.shape[n]);}text+="] full_coefficients=[";
        const auto count=std::min<std::size_t>(tensor.values.size(),128);for(std::size_t n=0;n<count;++n){if(n)text+=", ";text+=realText(tensor.values[n]);}if(count<tensor.values.size())text+=" ...";return text+"]";
    }
    if(const auto* x=std::get_if<ArraySeries>(&value))return "array series: "+std::to_string(x->data->times.size())+" samples, "+std::to_string(language::coordinateWidth(x->valueType,x->schema))+" coordinates per sample";
    if(const auto* x=std::get_if<UniversalMatrix>(&value)){
        const auto& a=**x;const auto width=a.algebra->dimension();std::string text="[";
        const auto shown=std::min<std::size_t>(a.rows*a.columns,32);
        for(std::size_t cell=0;cell<shown;++cell){if(cell)text+=(cell%a.columns==0?"; ":", ");text+=a.algebra->name()+"[";for(std::size_t k=0;k<width;++k){if(k)text+=", ";text+=realText(a.values.at(cell*width+k));}text+="]";}
        if(shown<a.rows*a.columns)text+=" ...";
        return text+"] ("+std::to_string(a.rows)+"x"+std::to_string(a.columns)+")";
    }
    if(const auto* definition=std::get_if<UniversalDefinition>(&value))return "algebra "+(*definition)->name()+" ("+std::to_string((*definition)->dimension())+" real coordinates, hash="+(*definition)->hash()+")";
    if(const auto* item=std::get_if<UniversalElement>(&value)){
        std::string text=item->definition()->name()+"[";
        for(std::size_t n=0;n<item->coefficients().size();++n){if(n)text+=", ";text+=realText(item->coefficients()[n]);}
        return text+"]";
    }
    if(const auto* x=std::get_if<std::complex<double>>(&value))return "complex("+realText(x->real())+", "+realText(x->imag())+")";
    if(const auto* x=std::get_if<TupleValue>(&value)){std::string text="(";for(std::size_t n=0;n<(*x)->values.size();++n){if(n)text+=", ";text+=valueText((*x)->values[n]);if(text.size()>4096){text+=" ...";break;}}return text+")";}
    if(const auto* x=std::get_if<RecordValue>(&value)){std::string text="options(";bool first=true;for(const auto& [name,item]:(*x)->fields){if(!first)text+=", ";first=false;text+=name+"="+valueText(item);if(text.size()>4096){text+=" ...";break;}}return text+")";}
    if (const auto* x = std::get_if<I>(&value)) return integerText(*x);
    if (const auto* x = std::get_if<double>(&value)) return realText(*x);
    if (const auto* x = std::get_if<std::string>(&value)) return *x;
    if (const auto* x = std::get_if<IntegerRange>(&value))
        return integerText(x->start) + ":" + integerText(x->step) + ":" + integerText(x->stop);
    if (const auto* x = std::get_if<RealRange>(&value))
        return realText(x->start) + ":" + realText(x->step) + ":" + realText(x->stop);
    if(const auto* x=std::get_if<FunctionValue>(&value))return "user function "+x->definition->name;
    if(const auto* x=std::get_if<ModelValue>(&value)) return "native ODE model "+x->definition->name;
    if(const auto* x=std::get_if<SolutionValue>(&value)) {
        const auto& s=*x->data;
        return "solution: "+std::to_string(s.times.size())+" samples, "+std::to_string(s.coordinateCount())+
            " coordinates, native cache "+(s.stats.cacheHit?"hit":"miss")+", key="+s.stats.key+
            ", method="+s.stats.method+", jacobian="+s.stats.jacobian+", steps="+std::to_string(s.stats.steps);
    }
    if(const auto* x=std::get_if<RealSeries>(&value)) return "real series: "+std::to_string(x->data->times.size())+" samples";
    if(const auto* x=std::get_if<PentSeries>(&value)) return "pentarnion series: "+std::to_string(x->data->times.size())+" samples";
    if(const auto* x=std::get_if<UniversalSeries>(&value)) return x->algebra->name()+" series: "+std::to_string(x->data->times.size())+" samples, "+std::to_string(x->algebra->dimension())+" coordinates";
    if(const auto* array=std::get_if<MatrixValue>(&value)) {
        const auto& matrix=**array;
        std::string text="[";
        const bool preview=matrix.values.size()>1024;
        const auto rows=preview?std::min<std::size_t>(matrix.rows,6):matrix.rows;
        const auto columns=preview?std::min<std::size_t>(matrix.columns,6):matrix.columns;
        for(std::size_t row=0;row<rows;++row) {
            if(row) text+="; ";
            for(std::size_t column=0;column<columns;++column) {
                if(column) text+=", ";
                const auto& entry=matrix.values[row*matrix.columns+column];
                text+=matrix.pentarnion?"("+valueText(Value(entry))+")":realText(entry.coefficients()[0]);
            }
            if(columns<matrix.columns)text+=", ...";
        }
        if(rows<matrix.rows)text+="; ...";
        text+="]";
        if(preview || matrix.values.empty())text+=" ("+std::to_string(matrix.rows)+"x"+std::to_string(matrix.columns)+")";
        return text;
    }
    const auto& coefficients = std::get<P>(value).coefficients();
    constexpr const char* bases[] = {"", "i", "j", "k", "ell"};
    std::string text;
    for (std::size_t n = 0; n < coefficients.size(); ++n) {
        const double c = coefficients[n];
        if (c == 0.0) continue; // Exact zero only; never erase tiny nonzero values.
        const bool negative = c < 0;
        if (text.empty()) { if (negative) text += '-'; }
        else text += negative ? " - " : " + ";
        const double magnitude = std::abs(c);
        if (n == 0 || magnitude != 1.0) {
            text += realText(magnitude);
            if (n != 0) text += '*';
        }
        text += bases[n];
    }
    return text.empty() ? "0" : text;
}

#include "foundation_value.inc"
class Evaluator {
public:
    Evaluator(ExecutionResult& result, const ExecutionOptions& options, Environment& variables,Timer& timer,double& compilationSeconds,Random& random,detail::ParallelState& parallel,detail::ResearchCapture& researchCapture,std::string_view executedSource)
        : result_(result), options_(options), executedSource_(executedSource), context_(result.analysis.program.gamma), variables_(variables),timer_(timer),compilationSeconds_(compilationSeconds),random_(random),parallel_(parallel),researchCapture_(researchCapture) {}
    void run(bool automaticEcho = false) {
        prepareNativeUses();
        runStatements(result_.analysis.program.statements);
        if (automaticEcho && lastValue_)
            emitLine((lastName_.empty() ? std::string() : lastName_ + " = ") + valueText(*lastValue_), currentSpan_);
    }
    void runStatements(const std::vector<language::Statement>& statements) {
        for (const auto& statement : statements) {
            tick(statement.span);
            lastValue_.reset(); lastName_.clear();
            currentTarget_.clear();
            switch (statement.kind) {
            case language::StatementKind::Directive: break;
            case language::StatementKind::Destructure:runDestructure(statement);break;
            case language::StatementKind::Import:runImport(statement);break;
            case language::StatementKind::FunctionDefinition:
                if(variables_.size()>=options_.maxWorkspaceVariables)fail("R1005","Workspace variable limit reached.",statement.span);
                checkFoundationWorkspace(statement.name,FunctionValue{statement.function,{},{}},statement.span);
                variables_.insert_or_assign(statement.name,FunctionValue{statement.function,{},{}});break;
            case language::StatementKind::If:
                runBlock(truth(evaluate(*statement.expression),statement.span)?statement.body:statement.alternative);
                lastValue_.reset();lastName_.clear();break;
            case language::StatementKind::While:
                while(truth(evaluate(*statement.expression),statement.span)) {
                    tick(statement.span);
                    try{runBlock(statement.body);}catch(const LoopFlow& flow){if(!flow.continuing)break;}
                }
                lastValue_.reset();break;
            case language::StatementKind::For:runFor(statement);lastValue_.reset();break;
            case language::StatementKind::Break:throw LoopFlow{false};
            case language::StatementKind::Continue:throw LoopFlow{true};
            case language::StatementKind::Return:
                throw ReturnFlow{statement.expression?std::optional<Value>(evaluate(*statement.expression)):std::nullopt,statement.span};
            case language::StatementKind::Tic:
                timer_.restart(std::chrono::steady_clock::now(),compilationSeconds_);break;
            case language::StatementKind::Toc: {
                const double elapsed=statement.expression?real(evaluate(*statement.expression),statement.span):elapsedSeconds(statement.span);
                emitLine("Elapsed time is "+realText(elapsed)+" seconds.",statement.span);break;
            }
            case language::StatementKind::OdeDefinition:
                if(variables_.size()>=options_.maxWorkspaceVariables) fail("R1005","The workspace variable limit was reached.",statement.span);
                checkFoundationWorkspace(statement.name,ModelValue{statement.ode},statement.span);
                variables_.insert_or_assign(statement.name,ModelValue{statement.ode});
                break;
            case language::StatementKind::Clear:
                if (statement.names.empty()) variables_.clear();
                else for (const auto& name : statement.names) variables_.erase(name);
                break;
            case language::StatementKind::ClearConsole:
                result_.clearConsole = true;
                result_.output.clear();
                result_.warningOutputLines.clear();
                break;
            case language::StatementKind::Figure: {
                auto action = figureAction(statement.expression ? FigureActionKind::Select : FigureActionKind::SelectNew,
                                           statement.span);
                if (statement.expression) action.figure = figureIdentifier(*statement.expression);
                action.name = statement.figureName;
                action.numberTitle = statement.numberTitle;
                result_.figureActions.push_back(std::move(action));
                break;
            }
            case language::StatementKind::DockFigure:
            case language::StatementKind::UndockFigure: {
                auto action = figureAction(statement.kind == language::StatementKind::DockFigure
                    ? FigureActionKind::Dock : FigureActionKind::Undock, statement.span);
                action.figure = figureIdentifier(*statement.expression);
                result_.figureActions.push_back(std::move(action));
                break;
            }
            case language::StatementKind::TileFigures: {
                auto action = figureAction(statement.text == "desktop" ? FigureActionKind::TileDesktop
                    : FigureActionKind::TileStudio, statement.span);
                if (!statement.arguments.empty()) {
                    const I rows = integer(evaluate(*statement.arguments.at(0)), statement.arguments[0]->span);
                    const I columns = integer(evaluate(*statement.arguments.at(1)), statement.arguments[1]->span);
                    if (rows < 1 || rows > 64 || columns < 1 || columns > 64 || rows * columns > 64)
                        fail("R1012", "Tiling dimensions must be integers in 1..64 with at most 64 cells.", statement.span);
                    action.rows = static_cast<int>(rows);
                    action.columns = static_cast<int>(columns);
                }
                result_.figureActions.push_back(std::move(action));
                break;
            }
            case language::StatementKind::SetTitle:
            case language::StatementKind::SetXLabel:
            case language::StatementKind::SetYLabel:
            case language::StatementKind::SetZLabel:
            case language::StatementKind::SetCaption: {
                const auto kind = statement.kind == language::StatementKind::SetTitle ? FigureActionKind::SetTitle :
                    statement.kind == language::StatementKind::SetXLabel ? FigureActionKind::SetXLabel :
                    statement.kind == language::StatementKind::SetYLabel ? FigureActionKind::SetYLabel :
                    statement.kind == language::StatementKind::SetZLabel ? FigureActionKind::SetZLabel : FigureActionKind::SetCaption;
                auto action = figureAction(kind, statement.span);
                action.text = statement.text;
                action.latex = statement.latex;
                result_.figureActions.push_back(std::move(action));
                break;
            }
            case language::StatementKind::Plot: {
                auto action = figureAction(FigureActionKind::Plot, statement.span);
                if (statement.arguments.size() > std::min<std::size_t>(256, options_.maxPlotVectors))
                    fail("R1005", "Plot vector count exceeds the configured per-plot budget.", statement.span);
                action.dimensions = statement.dimensions;
                action.axes = statement.axes;
                const auto plotAlgebra=statement.declaredSchema?statement.declaredSchema->algebra:UniversalDefinition{};
                if(plotAlgebra){action.basisLabels=plotAlgebra->basis();action.algebraId=plotAlgebra->id();}
                if(statement.name=="plot" || statement.name=="plot3_coords") {
                    std::vector<Value> coordinates;
                    for(const auto& argument:statement.arguments)coordinates.push_back(evaluate(*argument));
                    const auto count=[&](const Value& value) {
                        if(const auto* series=std::get_if<RealSeries>(&value))return series->data->times.size();
                        const auto& matrix=*std::get<MatrixValue>(value);
                        if(matrix.rows>1 && matrix.columns>1)fail("R1901","Plot coordinates must be row or column vectors.",statement.span);
                        return matrix.values.size();
                    };
                    const auto samples=count(coordinates[0]);
                    for(const auto& coordinate:coordinates)if(count(coordinate)!=samples)
                        fail("R1810","Curve coordinates must have matching sample counts.",statement.span);
                    reservePlotPoints(samples,statement.span);
                    PlotCurve curve;curve.label=statement.name=="plot"?"time curve":"coordinate curve";curve.points.reserve(samples);
                    for(std::size_t sample=0;sample<samples;++sample) {
                        tick(statement.span);std::array<double,3> point{};
                        for(std::size_t axis=0;axis<coordinates.size();++axis) {
                            if(const auto* series=std::get_if<RealSeries>(&coordinates[axis]))point[axis]=seriesValue(*series,sample,statement.span);
                            else point[axis]=std::get<MatrixValue>(coordinates[axis])->values[sample].coefficients()[0];
                        }
                        curve.points.push_back(point);
                    }
                    action.curves.push_back(std::move(curve));result_.figureActions.push_back(std::move(action));break;
                }
                action.vectors.reserve(statement.arguments.size());
                for (std::size_t index = 0; index < statement.arguments.size(); ++index) {
                    const auto& argument = statement.arguments[index];
                    const Value value = evaluate(*argument);
                    if(const auto* series=std::get_if<UniversalSeries>(&value)) {
                        if(!plotAlgebra||!series->algebra||series->algebra->hash()!=plotAlgebra->hash())fail("R2530","Plot trajectory algebra differs from its checked immutable definition.",argument->span);
                        const auto samples=series->data->times.size();reservePlotPoints(samples,statement.span);
                        PlotCurve curve;curve.label="trajectory "+std::to_string(index+1);curve.points.reserve(samples);
                        for(std::size_t sample=0;sample<samples;++sample){
                            tick(statement.span);std::array<double,3> point{};
                            for(int axis=0;axis<statement.dimensions;++axis)point[static_cast<std::size_t>(axis)]=seriesValue({series->data,series->offset+static_cast<std::size_t>(statement.axes[static_cast<std::size_t>(axis)]),false},sample,statement.span);
                            curve.points.push_back(point);
                        }
                        action.curves.push_back(std::move(curve));continue;
                    }
                    if(const auto* series=std::get_if<PentSeries>(&value)) {
                        const auto samples=series->data->times.size();
                        reservePlotPoints(samples,statement.span);
                        PlotCurve curve;curve.label="trajectory "+std::to_string(index+1);curve.points.reserve(samples);
                        for(std::size_t sample=0;sample<samples;++sample) {
                            tick(statement.span);std::array<double,3> point{};
                            for(int axis=0;axis<statement.dimensions;++axis)
                                point[static_cast<std::size_t>(axis)]=seriesValue({series->data,series->offset+static_cast<std::size_t>(statement.axes[static_cast<std::size_t>(axis)]),false},sample,statement.span);
                            curve.points.push_back(point);
                        }
                        action.curves.push_back(std::move(curve));continue;
                    }
                    const Expr* label = argument.get();
                    while (label->kind == ExprKind::Group) label = label->children.at(0).get();
                    const auto name=label->kind == ExprKind::Reference?label->text:"vector "+std::to_string(index+1);
                    if(const auto* vector=std::get_if<UniversalElement>(&value)){
                        if(!plotAlgebra||vector->definition()->hash()!=plotAlgebra->hash())fail("R2530","Plot vector algebra differs from its checked immutable definition.",argument->span);
                        PlotVector arrow;arrow.label=name;arrow.originalCoefficients=vector->coefficients();
                        std::copy_n(arrow.originalCoefficients.begin(),std::min<std::size_t>(5,arrow.originalCoefficients.size()),arrow.coefficients.begin());
                        action.vectors.push_back(std::move(arrow));
                    }else{
                        const auto* legacyVector=std::get_if<P>(&value);if(!legacyVector)fail("R1999","A checked plot vector is not an algebra element.",argument->span);
                        action.vectors.push_back({legacyVector->coefficients(),name});
                    }
                }
                result_.figureActions.push_back(std::move(action));
                break;
            }
            case language::StatementKind::Subplot: {
                auto action=figureAction(FigureActionKind::Subplot,statement.span);
                const I rows=integer(evaluate(*statement.arguments.at(0)),statement.span);
                const I columns=integer(evaluate(*statement.arguments.at(1)),statement.span);
                const I index=integer(evaluate(*statement.arguments.at(2)),statement.span);
                if(rows<1||rows>64||columns<1||columns>64||rows*columns>64||index<1||index>rows*columns)
                    fail("R1012","subplot requires positive dimensions, at most64 cells and an index within its grid.",statement.span);
                action.rows=static_cast<int>(rows);action.columns=static_cast<int>(columns);action.subplotIndex=static_cast<int>(index);
                result_.figureActions.push_back(std::move(action));break;
            }
            case language::StatementKind::ClearFigure:
                result_.figureActions.push_back(figureAction(FigureActionKind::Clear, statement.span));
                break;
            case language::StatementKind::CloseFigure: {
                auto action = figureAction(FigureActionKind::Close, statement.span);
                action.figure = figureIdentifier(*statement.expression);
                result_.figureActions.push_back(std::move(action));
                break;
            }
            case language::StatementKind::CloseAllFigures:
                result_.figureActions.push_back(figureAction(FigureActionKind::CloseAll, statement.span));
                break;
            case language::StatementKind::Declaration:
                if (variables_.size() >= options_.maxWorkspaceVariables)
                    fail("R1005", "The command workspace variable limit was reached.", statement.span);
                currentTarget_=statement.name;
                {
                    Value value=convert(evaluate(*statement.expression),statement.declaredType,statement.span,statement.declaredSchema);
                    if(statement.text=="vector") {
                        const auto rows=std::holds_alternative<UniversalMatrix>(value)?std::get<UniversalMatrix>(value)->rows:std::get<MatrixValue>(value)->rows;
                        const auto columns=std::holds_alternative<UniversalMatrix>(value)?std::get<UniversalMatrix>(value)->columns:std::get<MatrixValue>(value)->columns;
                        if(rows>1 && columns>1)fail("R1901","A vector declaration requires a row or column vector.",statement.span);
                    }
                    checkMatrixWorkspace(statement.name,value,statement.span);checkFoundationWorkspace(statement.name,value,statement.span);
                    variables_.insert_or_assign(statement.name,std::move(value));
                }
                lastValue_ = variables_.at(statement.name); lastName_ = statement.name;
                break;
            case language::StatementKind::Assignment: {
                const auto found = variables_.find(statement.name);
                if (found == variables_.end()) fail("R1999", "Missing checked assignment target.", statement.span);
                currentTarget_=statement.name;
                Value replacement=convert(evaluate(*statement.expression),valueType(found->second),statement.span,runtimeSchema(found->second));
                checkMatrixWorkspace(statement.name,replacement,statement.span);checkFoundationWorkspace(statement.name,replacement,statement.span);
                found->second=std::move(replacement);
                lastValue_ = found->second; lastName_ = statement.name;
                break;
            }
            case language::StatementKind::IndexedAssignment: {
                const auto found=variables_.find(statement.name);
                if(found==variables_.end() || (!std::holds_alternative<MatrixValue>(found->second)&&!std::holds_alternative<UniversalMatrix>(found->second)&&!std::holds_alternative<TensorValue>(found->second)))fail("R1999","Missing checked array target.",statement.span);
                Value candidate=std::holds_alternative<TensorValue>(found->second)?tensorWriteIndex(std::get<TensorValue>(found->second),statement.arguments,*statement.expression,statement.span,0):std::holds_alternative<UniversalMatrix>(found->second)?universalArrayWriteIndex(std::get<UniversalMatrix>(found->second),statement.arguments,*statement.expression,statement.span,0):Value(writeIndex(std::get<MatrixValue>(found->second),statement.arguments,*statement.expression,statement.span,0));
                checkMatrixWorkspace(statement.name,candidate,statement.span);checkFoundationWorkspace(statement.name,candidate,statement.span);
                found->second=std::move(candidate);
                lastValue_=found->second;lastName_=statement.name;
                break;
            }
            case language::StatementKind::Expression:
                lastValue_ = evaluate(*statement.expression);
                if(statement.expression->type==Type::Void)lastValue_.reset();
                break;
            case language::StatementKind::Display: {
                std::string line;
                bool first = true;
                for (const auto& argument : statement.arguments) {
                    const auto text = valueText(evaluate(*argument));
                    if (text.size() > options_.maxOutputBytes ||
                        line.size() > options_.maxOutputBytes - text.size())
                        fail("R1005", "Display output exceeds the configured byte budget.", statement.span);
                    if (!first) line += ' ';
                    first = false;
                    line += text;
                }
                emitLine(std::move(line), statement.span);
                break;
            }
            }
        }
    }
    void snapshots() {
        for (const auto& [name, value] : variables_)
            result_.variables.push_back({name, std::string(language::typeName(valueType(value))), valueText(value)});
    }
private:
#include "matrix_operations.inc"
#include "workspace_operations.inc"
#include "research_capture_helpers.inc"
#include "foundation_runtime.inc"
#include "numeric_helpers.inc"
#include "universal_helpers.inc"
#include "universal_array_helpers.inc"
#include "tensor_helpers.inc"
#include "coordinate_values.inc"
#include "developer_tools_helpers.inc"
#include "parallel_helpers.inc"
#include "vector_threading_helpers.inc"
#include "linalg_helpers.inc"
#include "derivative_helpers.inc"
#include "third_derivative_helpers.inc"
#include "chart_geometry_helpers.inc"
#include "sensitivity_helpers.inc"
#include "model_sensitivity_helpers.inc"
#include "dae_helpers.inc"
#include "mechanics_helpers.inc"
#include "constrained_mechanics_helpers.inc"
#include "quantity_helpers.inc"
#include "nd_helpers.inc"
#include "derivative_model_helpers.inc"
#include "roots_helpers.inc"
#include "scientific_live_helpers.inc"
#include "advanced_ode_helpers.inc"
#include "fitting_helpers.inc"
#include "inference_helpers.inc"
#include "ode_fitting_helpers.inc"
#include "curve_fitting_helpers.inc"
#include "optimization_helpers.inc"
#include "iteration_helpers.inc"
#include "maps_helpers.inc"
#include "pde_helpers.inc"
#include "pde_ensemble_helpers.inc"
#include "geometry_helpers.inc"
#include "extra_geometry_helpers.inc"
#include "fem_helpers.inc"
#include "bifurcation_helpers.inc"
#include "experiment_bundle_helpers.inc"
#include "chaos_helpers.inc"
#include "publication_helpers.inc"
#include "animation_helpers.inc"
#include "video_helpers.inc"
#include "discovery_helpers.inc"
#include "hybrid_ode_helpers.inc"
#include "verification_helpers.inc"
#include "orbits_helpers.inc"
#include "graph_helpers.inc"
#include "graph_tools_helpers.inc"
#include "network_helpers.inc"
#include "graph_view_helpers.inc"
#include "science_data_helpers.inc"
#include "pent_symbolic_helpers.inc"
#include "evolution_helpers.inc"
    struct PreparedOrigins {std::mutex mutex;std::set<std::string> keys,usedKeys;};
    std::shared_ptr<PreparedOrigins> preparedOrigins_=std::make_shared<PreparedOrigins>();
#include "provenance_helpers.inc"
    std::function<bool(const std::string&,bool)> nativeCacheStatus() {
        return [origins=preparedOrigins_](const std::string& key,bool hit){
            std::lock_guard lock(origins->mutex);origins->usedKeys.insert(key);if(origins->keys.erase(key))return false;return hit;
        };
    }
    void recordCompilation(double seconds) {
        if(!std::isfinite(seconds)||seconds<0)return;
        std::lock_guard lock(preparedOrigins_->mutex);
        compilationSeconds_+=seconds;
        ++result_.nativeCompilation.compilerAttempts;
        result_.nativeCompilation.compilationWallSeconds+=seconds;
        result_.nativeCompilation.peakCompilerJobs=std::max<std::size_t>(1,result_.nativeCompilation.peakCompilerJobs);
    }
    void prepareNativeUses() {
        if(options_.odeCacheDirectory.empty()&&options_.dataDirectory.empty())return;
        native_functions::Options callbacks;
        callbacks.build.cacheDirectory=options_.odeCacheDirectory.empty()?options_.dataDirectory/".penta-cache"/"callbacks":options_.odeCacheDirectory.parent_path()/"callbacks";
        callbacks.build.cancellation=options_.cancellation;
        ode::NativeOptions models;models.cacheDirectory=options_.odeCacheDirectory;models.cancellation=options_.cancellation;
        std::vector<std::shared_ptr<const language::FunctionDefinition>> availableFunctions;
        std::unordered_set<const void*> visited;
        std::function<void(const Value&,std::size_t)> collect=[&](const Value& value,std::size_t depth){
            if(depth>32)return;
            if(const auto* function=std::get_if<FunctionValue>(&value)){if(function->definition&&visited.insert(function->definition.get()).second)availableFunctions.push_back(function->definition);}
            else if(const auto* record=std::get_if<RecordValue>(&value)){if(visited.insert(record->get()).second)for(const auto& [name,field]:(*record)->fields){(void)name;collect(field,depth+1);}}
            else if(const auto* tuple=std::get_if<TupleValue>(&value)){if(visited.insert(tuple->get()).second)for(const auto& item:(*tuple)->values)collect(item,depth+1);}
        };
        for(const auto& [name,value]:variables_){(void)name;collect(value,0);}
        try{result_.nativeCompilation=native_functions::prepareProgram(result_.analysis.program,callbacks,models,options_.nativeCompileJobs,availableFunctions);}
        catch(const ode::OdeError& e){fail(options_.cancellation&&options_.cancellation->load()?"R1006":e.code,e.what(),e.span);}
        compilationSeconds_+=result_.nativeCompilation.compilationWallSeconds;
        for(const auto& key:result_.nativeCompilation.builtKeys)preparedOrigins_->keys.insert(key);
        if(result_.nativeCompilation.cancelled)fail("R1006","Native compile batch cancelled.",{});
    }
    std::optional<Value> dataFileCall(const Expr& expression,const std::vector<Value>& arguments) {
        const auto& name=expression.text;const auto span=expression.span;
        if(name=="save"||name=="load")return workspaceFileCall(expression,arguments);
        const bool reading=name=="readmatrix"||name=="csvread"||name=="xlsread";
        const bool writing=name=="writematrix"||name=="csvwrite"||name=="xlswrite";
        if(!reading&&!writing)return {};
        const std::size_t pathIndex=name=="writematrix"?1:0;
        const auto path=dataPath(std::get<std::string>(arguments.at(pathIndex)),span);
        const std::string sheet=arguments.size()>(reading?1u:2u)?std::get<std::string>(arguments.back()):std::string();
        try {
            if(reading){const auto table=dataio::readMatrix(path,sheet,dataOptions());auto matrix=newMatrix(table.rows,table.columns,false,span);
                for(const auto value:table.values){tick(span);matrix->values.push_back(P::scalar(context_,finite(value,span)));}return Value(MatrixValue(std::move(matrix)));}
            const auto& value=arguments.at(name=="writematrix"?0:1);dataio::NumericTable table;
            if(const auto* matrix=std::get_if<MatrixValue>(&value)){
                if((*matrix)->pentarnion)fail("R2001","CSV/XLSX tables require real entries; export coeff(P,k) for a Pentarnion array.",span);
                table.rows=(*matrix)->rows;table.columns=(*matrix)->columns;table.values.reserve((*matrix)->values.size());
                for(const auto& entry:(*matrix)->values){tick(span);table.values.push_back(entry.coefficients()[0]);}
            }else if(const auto* series=std::get_if<RealSeries>(&value)){
                table.rows=series->data->times.size();table.columns=1;table.values.reserve(table.rows);
                for(std::size_t n=0;n<table.rows;++n){tick(span);table.values.push_back(seriesValue(*series,n,span));}
            }else{table.rows=table.columns=1;table.values.push_back(real(value,span));}
            dataio::writeMatrix(path,table,sheet,dataOptions());return Value(I{0});
        }catch(const dataio::Error& error){fail(options_.cancellation&&options_.cancellation->load()?"R1006":"R2001",error.what(),span);}
    }
    struct LoopFlow {bool continuing=false;};
    struct ReturnFlow {std::optional<Value> value;SourceSpan span{};};
    std::size_t callDepth_=0;
    std::vector<const Environment*> outerFrames_;
    bool truth(const Value& value,SourceSpan span) const {return real(value,span)!=0;}
    bool equalValues(const Value& left,const Value& right,SourceSpan span) {
        tick(span);
        if(const auto* a=std::get_if<TensorValue>(&left)){const auto* b=std::get_if<TensorValue>(&right);if(!b||!detail::sameTensorDescriptor(**a,**b))return false;chargeNumericWork((*a)->values.size(),span);return (*a)->values==(*b)->values;}
        if(std::holds_alternative<TensorValue>(right))return false;
        if(const auto* a=std::get_if<UniversalMatrix>(&left)){const auto* b=std::get_if<UniversalMatrix>(&right);if(!b||(*a)->algebra->hash()!=(*b)->algebra->hash()||(*a)->rows!=(*b)->rows||(*a)->columns!=(*b)->columns)return false;chargeNumericWork((*a)->values.size(),span);return (*a)->values==(*b)->values;}
        if(std::holds_alternative<UniversalMatrix>(right))return false;
        if(const auto* a=std::get_if<UniversalDefinition>(&left)){const auto* b=std::get_if<UniversalDefinition>(&right);return b&&(*a)->hash()==(*b)->hash();}
        if(const auto* a=std::get_if<UniversalElement>(&left)){const auto* b=std::get_if<UniversalElement>(&right);return b&&a->definition()->hash()==b->definition()->hash()&&a->coefficients()==b->coefficients();}
        if(std::holds_alternative<UniversalDefinition>(right)||std::holds_alternative<UniversalElement>(right))return false;
        if(const auto* a=std::get_if<RecordValue>(&left)){const auto* b=std::get_if<RecordValue>(&right);if(!b||(*a)->fields.size()!=(*b)->fields.size())return false;for(const auto& [key,value]:(*a)->fields){const auto found=(*b)->fields.find(key);if(found==(*b)->fields.end()||!equalValues(value,found->second,span))return false;}return true;}
        if(const auto* a=std::get_if<TupleValue>(&left)){const auto* b=std::get_if<TupleValue>(&right);if(!b||(*a)->values.size()!=(*b)->values.size())return false;for(std::size_t n=0;n<(*a)->values.size();++n)if(!equalValues((*a)->values[n],(*b)->values[n],span))return false;return true;}
        if(const auto* a=std::get_if<std::string>(&left)) {
            const auto* b=std::get_if<std::string>(&right);return b&&*a==*b;
        }
        if(const auto* a=std::get_if<MatrixValue>(&left)) {
            const auto* b=std::get_if<MatrixValue>(&right);
            if(!b||(*a)->rows!=(*b)->rows||(*a)->columns!=(*b)->columns)return false;
            for(std::size_t n=0;n<(*a)->values.size();++n){tick(span);if((*a)->values[n].coefficients()!=(*b)->values[n].coefficients())return false;}
            return true;
        }
        if(std::holds_alternative<P>(left)||std::holds_alternative<P>(right))return element(left,span).coefficients()==element(right,span).coefficients();
        if(const auto* a=std::get_if<I>(&left))if(const auto* b=std::get_if<I>(&right))return *a==*b;
        // Comparing a large integer through double would erase its low bits.
        if(std::holds_alternative<I>(left)||std::holds_alternative<I>(right)) {
            const auto integerValue=std::holds_alternative<I>(left)?std::get<I>(left):std::get<I>(right);
            const double realValue=std::holds_alternative<I>(left)?real(right,span):real(left,span);
            return static_cast<long double>(integerValue)==static_cast<long double>(realValue);
        }
        return real(left,span)==real(right,span);
    }
    void runBlock(const std::vector<language::Statement>& statements) {
        std::unordered_set<std::string> existing;
        for(const auto& [name,value]:variables_){(void)value;tick(currentSpan_);existing.insert(name);}
        struct Scope {
            Environment& variables;std::unordered_set<std::string>& existing;
            ~Scope(){for(auto it=variables.begin();it!=variables.end();)if(!existing.contains(it->first))it=variables.erase(it);else++it;}
        } scope{variables_,existing};
        runStatements(statements);
    }
    void runFor(const language::Statement& statement) {
        const auto values=evaluate(*statement.expression);
        const auto found=variables_.find(statement.name);
        if(found==variables_.end()&&variables_.size()>=options_.maxWorkspaceVariables)
            fail("R1005","The loop iterator exceeds the workspace variable limit.",statement.span);
        const std::optional<Value> old=found==variables_.end()?std::nullopt:std::optional<Value>(found->second);
        struct Scope{Environment& variables;const std::string& name;std::optional<Value> old;
            ~Scope(){if(old)variables.insert_or_assign(name,std::move(*old));else variables.erase(name);}
        } scope{variables_,statement.name,old};
        auto iteration=[&](Value value){
            tick(statement.span);variables_.insert_or_assign(statement.name,std::move(value));
            try{runBlock(statement.body);}catch(const LoopFlow& flow){return flow.continuing;}
            return true;
        };
        if(const auto* range=std::get_if<IntegerRange>(&values)) {
            I current=range->start;
            while(range->step>0?current<=range->stop:current>=range->stop) {
                if(!iteration(current))break;
                // Every signed endpoint is finite. Once an increment would
                // leave int64 there cannot be another representable member;
                // use exact integer guards even where long double is binary64.
                if(range->step>0&&current>std::numeric_limits<I>::max()-range->step)break;
                if(range->step<0&&current<std::numeric_limits<I>::min()-range->step)break;
                current+=range->step;
            }
            return;
        }
        Value materialized=rangeMatrix(values,statement.span);
        if(!std::holds_alternative<MatrixValue>(materialized))fail("R1902","A for loop requires a real range or vector.",statement.span);
        const auto& matrix=*std::get<MatrixValue>(materialized);
        if(matrix.pentarnion||(matrix.rows>1&&matrix.columns>1))fail("R1902","A for loop requires a real row or column vector.",statement.span);
        for(const auto& value:matrix.values)if(!iteration(value.coefficients()[0]))break;
    }
    Value callFunction(const FunctionValue& callable,const std::vector<Value>& arguments,SourceSpan span) {
        const auto function=callable.definition;
        if(!function||arguments.size()!=function->parameters.size())fail("R1920","User function call has an invalid signature.",span);
        if(function->native){
            native_functions::Options native;
            if(options_.odeCacheDirectory.empty()&&options_.dataDirectory.empty())
                fail("N1002","Native functions require a configured project/source cache directory.",span);
            native.build.cacheDirectory=options_.odeCacheDirectory.empty()
                ? options_.dataDirectory/".penta-cache"/"functions":options_.odeCacheDirectory.parent_path()/"functions";
            native.build.cancellation=options_.cancellation;
            native.build.cacheStatus=nativeCacheStatus();
            native.build.compilationFinished=[this](double seconds){recordCompilation(seconds);};
            native.chargeOperations=[this,span](std::size_t count){while(count--)tick(span);};
            if(native_functions::usesUniversalCoordinates(*function)){
                try{
                    std::vector<double> input;
                    for(std::size_t n=0;n<arguments.size();++n){
                        const auto schema=n<function->parameterSchemas.size()?function->parameterSchemas[n]:function->parameters[n].schema;
                        auto parameter=function->parameters[n];parameter.schema=schema;appendNativeCoordinates(arguments[n],parameter,input,span);
                    }
                    native_functions::Callback callback(*function,native);
                    std::vector<double> flat(callback.outputCount());callback.evaluateValues(context_.gamma(),input,flat);
                    const auto schemas=native_functions::callbackResultSchemas(*function);
                    std::vector<Value> output;std::size_t offset=0;
                    for(std::size_t n=0;n<callback.outputTypes().size();++n){
                        const auto type=callback.outputTypes()[n];const auto width=callback.outputWidths()[n];
                        output.push_back(nativeCoordinateValue(std::vector<double>(flat.begin()+offset,flat.begin()+offset+width),type,n<schemas.size()?schemas[n]:nullptr,span));
                        offset+=width;
                    }
                    if(function->result==Type::Tuple)return makeTuple(std::move(output),span);
                    if(output.size()!=1)fail("N2401","Native result shape differs from its checked signature.",span);
                    return output.front();
                }catch(const ode::OdeError& error){throw functionFailure(options_.cancellation&&options_.cancellation->load()?"R1006":error.code,std::string("In native function ")+function->name+": "+error.what(),span,*function,error.span.length?error.span:function->span,error.frames);}
            }
            std::vector<native_functions::Value> converted;converted.reserve(arguments.size());
            for(std::size_t n=0;n<arguments.size();++n){
                Value value=convert(arguments[n],function->parameters[n].type,span);native_functions::Value input;input.type=valueType(value);
                if(const auto* p=std::get_if<P>(&value))input.coefficients=p->coefficients();else input.coefficients[0]=real(value,span);
                converted.push_back(input);
            }
            try{
                if(!function->results.empty()){
                    std::vector<double> input;
                    for(const auto& item:converted)for(std::size_t c=0;c<(item.type==Type::Pentarnion?5u:1u);++c)input.push_back(item.coefficients[c]);
                    native_functions::Callback callback(*function,native);
                    native_functions::DerivativeRequest request;request.order=0;
                    const auto result=callback.evaluate(context_.gamma(),input,request);
                    std::vector<Value> output;std::size_t offset=0;
                    for(const auto type:callback.outputTypes()){
                        if(type==Type::Pentarnion){algebra::Coefficients coefficients{};for(auto& c:coefficients)c=result.values.at(offset++);output.emplace_back(P(context_,coefficients));}
                        else output.emplace_back(result.values.at(offset++));
                    }
                    return makeTuple(std::move(output),span);
                }
                const auto result=native_functions::evaluate(*function,context_.gamma(),converted,native);
                return result.value.type==Type::Pentarnion?Value(P(context_,result.value.coefficients)):Value(result.value.coefficients[0]);}
            catch(const ode::OdeError& error){throw functionFailure(options_.cancellation&&options_.cancellation->load()?"R1006":error.code,std::string("In native function ")+function->name+": "+error.what(),span,*function,error.span.length?error.span:function->span,error.frames);}
        }
        if(callDepth_>=std::min<std::size_t>(32,options_.maxCallDepth))fail("R1005","User function call depth limit reached.",span);
        ++callDepth_;
        Environment outer=std::move(variables_);variables_.clear();
        struct Scope {Evaluator& owner;Environment outer;std::optional<Value> last;std::string name,target;SourceSpan span;bool registered=false;
            ~Scope(){if(registered)owner.outerFrames_.pop_back();owner.variables_=std::move(outer);owner.lastValue_=std::move(last);owner.lastName_=std::move(name);owner.currentTarget_=std::move(target);owner.currentSpan_=span;--owner.callDepth_;}
        } scope{*this,std::move(outer),lastValue_,lastName_,currentTarget_,currentSpan_};
        outerFrames_.push_back(&scope.outer);
        scope.registered=true;
        try {
        for(const auto& [name,value]:scope.outer){tick(span);if(std::holds_alternative<FunctionValue>(value))variables_.emplace(name,value);}
        if(callable.captures)for(const auto& [name,value]:callable.captures->values){tick(span);variables_.insert_or_assign(name,value);}
        for(std::size_t n=0;n<arguments.size();++n) {
            tick(span);
            if(!variables_.contains(function->parameters[n].name)&&variables_.size()>=options_.maxWorkspaceVariables)
                fail("R1005","Function parameters exceed the workspace variable limit.",span);
            Value converted=convert(arguments[n],function->parameters[n].type,span,n<function->parameterSchemas.size()?function->parameterSchemas[n]:function->parameters[n].schema);
            checkMatrixWorkspace(function->parameters[n].name,converted,span);checkFoundationWorkspace(function->parameters[n].name,converted,span);
            variables_.insert_or_assign(function->parameters[n].name,std::move(converted));
        }
        try{runBlock(function->body);}catch(const ReturnFlow& returned) {
            if(function->result==Type::Void)return I(0);
            if(!returned.value)fail("R1921","This function did not return its declared result.",span);
            if(function->result==Type::Tuple){if(!std::holds_alternative<TupleValue>(*returned.value))fail("R1921","Function must return its declared outputs.",span);const auto& values=std::get<TupleValue>(*returned.value)->values;if(values.size()!=function->results.size())fail("R1921","Incorrect returned output count.",span);std::vector<Value> result;for(std::size_t n=0;n<values.size();++n)result.push_back(convert(values[n],function->results[n],span,function->resultSchema&&n<function->resultSchema->elements.size()?function->resultSchema->elements[n].schema:nullptr));return makeTuple(std::move(result),span);}
                return convert(*returned.value,function->result,returned.span,function->resultSchema);
        }
        if(function->result==Type::Void)return I(0);
        fail("R1921","Function reached its end without returning a value: "+function->name,span);
        }catch(const Failure& error){
            // Function bodies may have been defined by an earlier request.
            // Their byte spans cannot be applied to this request's document.
            throw functionFailure(error.code,"In function "+function->name+": "+error.what(),span,*function,error.span,error.frames);
        }
    }
    std::optional<Value> utilityCall(const Expr& expression,const std::vector<Value>& arguments) {
        const auto& name=expression.text;const auto span=expression.span;
        if(name=="assert") {
            if(!truth(arguments.at(0),span))fail("R1922",arguments.size()==2?std::get<std::string>(arguments[1]):"Assertion failed.",span);
            return Value(I(0));
        }
        if(name=="error")fail("R1923",std::get<std::string>(arguments.at(0)),span);
        if(name=="warning"){emitLine("Warning: "+std::get<std::string>(arguments.at(0)),span);result_.warningOutputLines.push_back(result_.output.size()-1);return Value(I(0));}
        if(name=="who"||name=="whos") {
            if(variables_.empty())emitLine("Workspace is empty.",span);
            else for(const auto& [variable,value]:variables_) {
                std::string line=variable;
                if(name=="whos") {
                    line+="  "+std::string(language::typeName(valueType(value)));
                    if(const auto* matrix=std::get_if<MatrixValue>(&value))line+="  "+std::to_string((*matrix)->rows)+"x"+std::to_string((*matrix)->columns);
                    else if(const auto* series=std::get_if<RealSeries>(&value))line+="  "+std::to_string(series->data->times.size())+"x1";
                    else if(const auto* series=std::get_if<PentSeries>(&value))line+="  "+std::to_string(series->data->times.size())+"x1";
                    else line+="  1x1";
                }
                emitLine(std::move(line),span);
            }
            return Value(I(0));
        }
        if(name=="help"||name=="lookfor") {
            const std::string topic=arguments.empty()?"":std::get<std::string>(arguments[0]);
            if(name=="lookfor"){emitLine(help::search(topic),span);return Value(I(0));}
            if(const auto entry=help::lookup(topic)){emitLine(*entry,span);return Value(I(0));}
            if(topic.empty())emitLine("Pentarnion Studio language: types integer/real/pentarnion/complex/string/matrix/vector/callback/options/tuple; indexing A(:,2), A(1:end); ranges a:b or a:step:b; if/else, for, while, functions; help(\"foundations\") lists the new scientific APIs.",span);
            else if(topic=="foundations"){
                emitLine("Foundations: callback f=@square or @(real x)=>x*x (snapshot captures); typed multiple outputs [real a,real b]=f(x); options defaults, named arguments and import \"helpers.p\" as science; native function real f(real x){return x*x;} compiles and caches the supported pure scalar subset.",span);
                emitLine("Arrays: reshape, diag, logical masks, find, meshgrid/ndgrid, dot and norm. Calculus: interp1, deval, trapz/cumtrapz, diff, gradient, divergence, laplacian and integral(callback,a,b[,options]). CPU: threads(0) selects automatic, threads(n) sets the shared budget, parallel_info() reports execution. See examples/README.txt for language foundations, array shapes, numerical calculus and parallel vectors.",span);
            }
            else if(topic=="for")emitLine("for(n=1:10){ ... } iterates a real range/vector; break and continue are supported. The iterator and body declarations have local scope.",span);
            else if(topic=="while")emitLine("while(condition){ ... } repeats while a real scalar is nonzero; break and continue are supported.",span);
            else if(topic=="if")emitLine("if(condition){ ... }else{ ... }; conditions are real scalar nonzero/zero; && and || short-circuit.",span);
            else if(topic=="function")emitLine("function real square(real x){return x*x;} Typed parameters and local variables; define functions before calling them. Functions persist in the command workspace; recursion is bounded.",span);
            else if(topic=="indexing"||topic=="end")emitLine("One-based column-major A(index), A(row,column); colon selects an entire dimension; end is its last index. Range/index-vector selections retain array shape; writes cannot resize arrays.",span);
            else if(topic=="save"||topic=="load")emitLine("save(\"workspace.pws\"[,\"name\",...]); load(\"workspace.pws\"); loaded names become available to subsequent console submissions. Scripts predeclare typed targets before load.",span);
            else if(topic=="readmatrix"||topic=="writematrix"||topic=="csvread"||topic=="csvwrite"||topic=="xlsread"||topic=="xlswrite")emitLine("matrix A=readmatrix(\"data.csv\"); writematrix(A,\"data.xlsx\"); optional sheet name for XLSX. csvread/xlsread and csvwrite/xlswrite aliases are supported.",span);
            else if(topic=="assert"||topic=="warning"||topic=="error")emitLine("assert(condition[,\"message\"]); warning(\"message\"); error(\"message\"); error and failed assertions stop the current request.",span);
            else if(topic=="who"||topic=="whos")emitLine("who; lists workspace names; whos; also shows their types and shapes.",span);
            else if(topic=="sum"||topic=="mean"||topic=="min"||topic=="max")emitLine("sum/mean(A[,dimension]); min/max(A), min/max(A,B), min/max(A,[],dimension). Array reductions use the first nonsingleton dimension by default; min/max require real entries.",span);
            else if(topic=="linspace"||topic=="range")emitLine("linspace(start,stop[,count]); a:b or a:step:b create finite real vectors when used as arrays. Bounds and finite-result checks apply.",span);
            else if(topic=="sin"||topic=="cos"||topic=="exp"||topic=="sinh"||topic=="cosh")emitLine(topic+"(x) accepts real/Pentarnion scalars and arrays; Pentarnion values use the one-element calculus.",span);
            else if(topic=="odesolve")emitLine("odesolve(model,state=initial,...,t0=0,t1=10,method=\"bdf\",jacobian=\"analytic\"); methods bdf/adams/erk/esdirk. The compiler emits analytic Jacobians; finite differences are optional.",span);
            else if(const auto found=variables_.find(topic);found!=variables_.end()&&std::holds_alternative<FunctionValue>(found->second)) {
                const auto& function=*std::get<FunctionValue>(found->second).definition;
                std::string usage="function "+std::string(language::typeName(function.result))+" "+function.name+"(";
                for(std::size_t n=0;n<function.parameters.size();++n){if(n)usage+=", ";usage+=std::string(language::typeName(function.parameters[n].type))+" "+function.parameters[n].name;}
                emitLine(usage+")",span);
            }else emitLine("No help entry for: "+topic+". Use lookfor \"words\" or help topics; see language.txt for the complete specification.",span);
            return Value(I(0));
        }
        return {};
    }

    double elapsedSeconds(SourceSpan span) {
        if(!timer_.start)fail("R1811","toc requires an earlier tic in this execution session.",span);
        return finite(timer_.elapsed(std::chrono::steady_clock::now(),compilationSeconds_),span);
    }
    void reservePlotPoints(std::size_t points,SourceSpan span) {
        const auto limit=std::min<std::size_t>(1000000,options_.maxPlotPoints);
        if(points>limit-plotPoints_)fail("R1005","Sampled plot points exceed the aggregate request budget.",span);
        plotPoints_+=points;
    }
    double seriesValue(const RealSeries& series,std::size_t sample,SourceSpan span) {
        if(sample>=series.data->times.size()) fail("R1810","Sample index is outside its solution.",span);
        if(series.times) return finite(series.data->times[sample],span);
        const auto width=series.data->coordinateCount();
        if(series.offset>=width || sample>std::numeric_limits<std::size_t>::max()/width ||
            sample*width+series.offset>=series.data->values.size()) fail("R1810","Invalid solution series layout.",span);
        return finite(series.data->values[sample*width+series.offset],span);
    }
    FigureAction figureAction(FigureActionKind kind, SourceSpan span) {
        if (!options_.allowFigures)
            fail("R1010", "Plotting and figure commands require Pentarnion Studio; they are disabled in this execution mode.", span);
        if (result_.figureActions.size() >= std::min<std::size_t>(256, options_.maxFigureActions))
            fail("R1005", "Figure actions exceed the configured request budget.", span);
        FigureAction action;
        action.kind = kind;
        action.algebraId = result_.analysis.program.algebraId;
        action.gamma = result_.analysis.program.gamma;
        return action;
    }
    int figureIdentifier(const Expr& expression) {
        const I identifier = integer(evaluate(expression), expression.span);
        if (identifier < 1 || identifier > 64)
            fail("R1011", "A figure identifier must be an integer in 1..64.", expression.span);
        return static_cast<int>(identifier);
    }
    void emitLine(std::string line, SourceSpan span) {
        if (line.size() >= options_.maxOutputBytes ||
            outputBytes_ > options_.maxOutputBytes - line.size() - 1)
            fail("R1005", "Display output exceeds the configured byte budget.", span);
        outputBytes_ += line.size() + 1;
        result_.output.push_back(std::move(line));
    }
    void tick(SourceSpan span) {
        currentSpan_ = span;
        if (options_.cancellation && options_.cancellation->load(std::memory_order_relaxed))
            fail("R1006", "Execution cancelled.", span);
        if (result_.operations >= options_.maxOperations)
            fail("R1005", "Execution exceeds the configured operation budget.", span);
        ++result_.operations;
    }
    double real(const Value& value, SourceSpan span) const {
        if (const auto* x = std::get_if<I>(&value)) return static_cast<double>(*x);
        if (const auto* x = std::get_if<double>(&value)) return *x;
        fail("R1999", "A checked scalar value was expected.", span);
    }
    I integer(const Value& value, SourceSpan span) const {
        if (const auto* x = std::get_if<I>(&value)) return *x;
        fail("R1999", "A checked integer value was expected.", span);
    }
    P element(const Value& value, SourceSpan span) const {
        if (const auto* x = std::get_if<P>(&value)) return *x;
        return P::scalar(context_, real(value, span));
    }
    Value convert(const Value& source, Type type, SourceSpan span,const std::shared_ptr<const language::ValueSchema>& schema={}) {
        Value value=source;
        if(type!=Type::Tuple)if(const auto* tuple=std::get_if<TupleValue>(&value)){if((*tuple)->values.empty())fail("R1951","A returned tuple has no first output.",span);value=(*tuple)->values.front();}
        if(type==Type::Tensor)return convertTensor(value,schema,span);
        const std::vector<std::size_t> fixedShape=schema?schema->shape:std::vector<std::size_t>{};
        if((type==Type::RealMatrix||type==Type::PentarnionMatrix||type==Type::ElementMatrix)&&(std::holds_alternative<IntegerRange>(value)||std::holds_alternative<RealRange>(value)))value=rangeMatrix(value,span);
        if(schema&&!schema->shape.empty()&&(type==Type::RealMatrix||type==Type::PentarnionMatrix||type==Type::ElementMatrix)){
            std::size_t rows=0,columns=0;
            if(const auto* matrix=std::get_if<MatrixValue>(&value)){rows=(*matrix)->rows;columns=(*matrix)->columns;}
            else if(const auto* matrix=std::get_if<UniversalMatrix>(&value)){rows=(*matrix)->rows;columns=(*matrix)->columns;}
            else fail("R2530","A fixed array parameter/result requires an array value.",span);
            if(schema->shape.size()!=2||rows!=schema->shape[0]||columns!=schema->shape[1])fail("R2530","Array rows/columns differ from the declared fixed shape.",span);
        }
        if(type==Type::ElementMatrix){
            if(const auto* matrix=std::get_if<UniversalMatrix>(&value)){
                if(schema&&schema->algebra&&schema->algebra->hash()!=(*matrix)->algebra->hash())fail("R2530","Array conversion cannot change its algebra definition.",span);
                if((*matrix)->fixedShape!=fixedShape){auto copy=std::make_shared<UniversalMatrixData>(**matrix);copy->fixedShape=fixedShape;return UniversalMatrix(copy);}
                return *matrix;
            }
            if(!schema||!schema->algebra||!std::holds_alternative<MatrixValue>(value))fail("R2530","Algebra arrays require a qualified element definition.",span);
            const auto& source=**std::get_if<MatrixValue>(&value);if(source.pentarnion)fail("R2530","Implicit conversions between Pentarnion and other algebra arrays are forbidden.",span);
            auto result=std::make_shared<UniversalMatrixData>();result->algebra=schema->algebra;result->rows=source.rows;result->columns=source.columns;
            result->fixedShape=fixedShape;
            const auto width=result->algebra->dimension();if(source.values.size()>options_.maxMatrixCoordinates/width)fail("R1005","Promoted algebra array exceeds the coordinate allowance.",span);result->values.reserve(source.values.size()*width);
            try{for(const auto& cell:source.values){tick(span);const auto item=UniversalElement::scalar(result->algebra,cell.coefficients()[0]);result->values.insert(result->values.end(),item.coefficients().begin(),item.coefficients().end());}}
            catch(const algebra::universal::Error& error){fail(error.code,error.what(),span);}return UniversalMatrix(result);
        }
        if(type==Type::Element){
            if(const auto* item=std::get_if<UniversalElement>(&value)){
                if(schema&&schema->algebra&&schema->algebra->hash()!=item->definition()->hash())fail("R2530","Conversion cannot change an element's algebra definition.",span);
                return *item;
            }
            if(!schema||!schema->algebra)fail("R2530","Scalar promotion requires a qualified algebra element type.",span);
            try{return UniversalElement::scalar(schema->algebra,real(value,span));}catch(const algebra::universal::Error& error){fail(error.code,error.what(),span);}
        }
        if(type==Type::Algebra){
            const auto* definition=std::get_if<UniversalDefinition>(&value);
            if(!definition||!*definition||(schema&&schema->algebra&&schema->algebra->hash()!=(*definition)->hash()))fail("R2530","An immutable algebra binding cannot change definition.",span);
            return *definition;
        }
        if((type==Type::RealMatrix||type==Type::PentarnionMatrix)&&std::holds_alternative<MatrixValue>(value)){
            const auto& source=std::get<MatrixValue>(value);
            if(type==Type::RealMatrix&&source->pentarnion)fail("R2530","A real array cannot discard Pentarnion coefficients.",span);
            if(source->fixedShape!=fixedShape||(type==Type::PentarnionMatrix&&!source->pentarnion)||(type==Type::RealMatrix&&source->logical)){
                auto result=std::make_shared<Matrix>(*source);result->fixedShape=fixedShape;result->pentarnion=type==Type::PentarnionMatrix;if(type==Type::RealMatrix)result->logical=false;return MatrixValue(result);
            }
            return source;
        }
        if(type==Type::Complex){if(const auto* number=std::get_if<std::complex<double>>(&value))return *number;return std::complex<double>(real(value,span),0);}
        if(type==Type::RealMatrix&&std::holds_alternative<MatrixValue>(value)&&std::get<MatrixValue>(value)->logical){auto result=std::make_shared<Matrix>(*std::get<MatrixValue>(value));result->logical=false;return MatrixValue(result);}
        if((type==Type::RealMatrix||type==Type::PentarnionMatrix)&&(std::holds_alternative<IntegerRange>(value)||std::holds_alternative<RealRange>(value)))value=rangeMatrix(value,span);
        if (valueType(value) == type) return value;
        if (type == Type::Real) return real(value, span);
        if (type == Type::Pentarnion) return element(value, span);
        if(type==Type::PentarnionMatrix && std::holds_alternative<MatrixValue>(value)) {
            auto result=std::make_shared<Matrix>(*std::get<MatrixValue>(value));result->pentarnion=true;return MatrixValue(std::move(result));
        }
        if (type == Type::RealRange) {
            if (const auto* x = std::get_if<IntegerRange>(&value))
                return RealRange{static_cast<double>(x->start), static_cast<double>(x->step), static_cast<double>(x->stop)};
        }
        fail("R1999", "A checked value conversion was unavailable.", span);
    }
    P divideCoefficients(const P& value, double denominator, SourceSpan span) const {
        if (denominator == 0) fail("R1001", "Division by zero.", span);
        algebra::Coefficients result;
        for (std::size_t n = 0; n < result.size(); ++n) {
            const long double wide = static_cast<long double>(value.coefficients()[n]) /
                                     static_cast<long double>(denominator);
            result[n] = finite(static_cast<double>(wide), span);
            if (wide != 0 && result[n] == 0)
                fail("R1001", "A nonzero coefficient division result underflows binary64.", span);
        }
        return P(value.context(), result);
    }
    Value binary(const Expr& expression, const Value& originalLeft, const Value& originalRight) {
        if(auto tensor=tensorBinary(expression,originalLeft,originalRight))return *tensor;
        if(auto array=universalArrayBinary(expression,originalLeft,originalRight))return *array;
        Value left=originalLeft,right=originalRight;
        if(std::holds_alternative<IntegerRange>(left)||std::holds_alternative<RealRange>(left))left=rangeMatrix(left,expression.span);
        if(std::holds_alternative<IntegerRange>(right)||std::holds_alternative<RealRange>(right))right=rangeMatrix(right,expression.span);
        const std::string& op = expression.text;
        const auto span = expression.span;
        if(std::holds_alternative<UniversalElement>(left)||std::holds_alternative<UniversalElement>(right)){
            const auto* a=std::get_if<UniversalElement>(&left);const auto* b=std::get_if<UniversalElement>(&right);
            const auto definition=a?a->definition():b->definition();
            universalCharge(definition,span,((op=="/"&&b)||(op=="\\"&&a))?definition->dimension()*definition->dimension()*definition->dimension():0);
            try{
                if(a&&b&&a->definition()->hash()!=b->definition()->hash())fail("R2530","Arithmetic cannot mix different algebra definitions.",span);
                if(op=="*")return a&&b?Value(algebra::universal::multiply(*a,*b)):Value(algebra::universal::scale(a?*a:*b,real(a?right:left,span)));
                if(op=="/"&&!b){const auto d=real(right,span);if(d==0)fail("R1001","Division by zero.",span);return algebra::universal::divideByReal(*a,d);}
                if(op=="\\"&&!a){const auto d=real(left,span);if(d==0)fail("R1001","Division by zero.",span);return algebra::universal::divideByReal(*b,d);}
                if(op=="^"){
                    const auto exponent=integer(right,span);if(exponent < -4096||exponent>4096)fail("R2530","Generic powers require an exponent in[-4096,4096].",span);
                    universalCharge(definition,span,(definition->dimension()+definition->terms().size())*static_cast<std::size_t>(std::abs(exponent))+(exponent<0?definition->dimension()*definition->dimension()*definition->dimension():0));
                    Value answer=I(0);ThreadBudget::instance().withReservation([&]{answer=algebra::universal::power(*a,exponent);});return answer;
                }
                const auto x=a?*a:UniversalElement::scalar(definition,real(left,span));
                const auto y=b?*b:UniversalElement::scalar(definition,real(right,span));
                if(op=="=="||op=="!="||op=="~=")return I((op=="==")== (x.coefficients()==y.coefficients()));
                if(op=="+")return algebra::universal::add(x,y);
                if(op=="-")return algebra::universal::subtract(x,y);
                if(op=="/"||op=="\\"){
                    algebra::universal::SolveResult solved;ThreadBudget::instance().withReservation([&]{solved=op=="/"?algebra::universal::solveRight(y,x):algebra::universal::solveLeft(x,y);});
                    if(!solved.solution)fail("R2531","Algebra equation has no accepted unique solution: "+std::string(algebra::universal::solveStatusName(solved.status)),span);
                    return *solved.solution;
                }
                fail("R2530","Unsupported generic algebra operator.",span);
            }catch(const algebra::universal::Error& error){fail(error.code,error.what(),span);}
        }
        if(auto result=foundationBinary(expression,left,right))return *result;
        if(auto result=linalgBinary(expression,left,right))return *result;
        if(auto result=parallelBinary(expression,left,right))return *result;
        if(op=="=="||op=="!="||op=="~=")return I((op=="==")==equalValues(left,right,span));
        if(op=="<"||op==">"||op=="<="||op==">=") {
            const long double a=std::holds_alternative<I>(left)?static_cast<long double>(std::get<I>(left)):real(left,span);
            const long double b=std::holds_alternative<I>(right)?static_cast<long double>(std::get<I>(right)):real(right,span);
            return I(op=="<"?a<b:op==">"?a>b:op=="<="?a<=b:a>=b);
        }
        if(std::holds_alternative<MatrixValue>(left)||std::holds_alternative<MatrixValue>(right))return matrixBinary(expression,left,right);
        if(op==".*"||op=="./"||op==".^") {
            Expr scalar=expression;scalar.text=op.substr(1);return binary(scalar,left,right);
        }
        const bool lp = std::holds_alternative<P>(left), rp = std::holds_alternative<P>(right);
        if (op == "+" || op == "-" || op == "*") {
            if (lp || rp) {
                if (op == "+") return algebra::add(element(left, span), element(right, span));
                if (op == "-") return algebra::subtract(element(left, span), element(right, span));
                if (!lp) return algebra::scale(std::get<P>(right), real(left, span));
                if (!rp) return algebra::scale(std::get<P>(left), real(right, span));
                return algebra::multiply(std::get<P>(left), std::get<P>(right));
            }
            if (std::holds_alternative<I>(left) && std::holds_alternative<I>(right)) {
                const I a = std::get<I>(left), b = std::get<I>(right);
                if (op == "+") return integerAdd(a, b, span);
                if (op == "-") return integerSubtract(a, b, span);
                return integerMultiply(a, b, span);
            }
            const double a = real(left, span), b = real(right, span);
            return finite(op == "+" ? a + b : op == "-" ? a - b : a * b, span);
        }
        if (op == "/") {
            if (rp) return algebra::rightQuotient(element(left, span), std::get<P>(right));
            const double denominator = real(right, span);
            if (denominator == 0) fail("R1001", "Division by zero.", span);
            if (lp) return divideCoefficients(std::get<P>(left), denominator, span);
            return finite(real(left, span) / denominator, span);
        }
        if (op == "\\") {
            if (lp) return algebra::leftQuotient(std::get<P>(left), element(right, span));
            const double denominator = real(left, span);
            if (denominator == 0) fail("R1001", "Left division by zero.", span);
            if (rp) return divideCoefficients(std::get<P>(right), denominator, span);
            return finite(real(right, span) / denominator, span);
        }
        if (op == "^") {
            if (lp) return algebra::power(std::get<P>(left), integer(right, span));
            const double base = real(left, span), exponent = real(right, span);
            if (base == 0 && exponent < 0) fail("R1001", "Zero cannot have a negative power.", span);
            if (base < 0 && exponent != std::trunc(exponent))
                fail("R1001", "This power has no finite real result.", span);
            return finite(std::pow(base, exponent), span);
        }
        fail("R1999", "Unknown checked binary operator.", span);
    }
    Value call(const Expr& expression, const std::vector<Value>& supplied) {
        const auto span = expression.span;
        const auto& name = expression.text;
        if(const auto found=variables_.find(name);found!=variables_.end()&&std::holds_alternative<FunctionValue>(found->second))return invokeCallable(found->second,supplied,span);
        std::vector<Value> arguments=supplied;
        for(auto& argument:arguments)if(std::holds_alternative<IntegerRange>(argument)||std::holds_alternative<RealRange>(argument))argument=rangeMatrix(argument,span);
        if(auto tensor=tensorCall(expression,arguments))return *tensor;
        if(auto value=universalArrayCall(expression,arguments))return *value;
        if(auto value=universalCall(expression,arguments))return *value;
        if(auto value=developerToolsCall(expression,arguments))return *value;
        if(expression.text=="compile_info") {
            const auto& r=result_.nativeCompilation;
            return makeRecord({{"requested_jobs",I(r.requestedJobs)},{"effective_jobs",I(r.effectiveJobs)},
                {"translation_units",I(r.translationUnits)},{"compiler_attempts",I(r.compilerAttempts)},
                {"cache_hits",I(r.cacheHits)},{"failed_preparations",I(r.failedPreparations)},
                {"peak_preparations",I(r.peakJobs)},{"peak_compiler_jobs",I(r.peakCompilerJobs)},
                {"compilation_seconds",r.compilationWallSeconds}},expression.span);
        }
        if(auto value=advancedOdeCall(expression,arguments))return *value;
        if(auto value=curveFittingCall(expression,arguments))return *value;
        if(auto value=odeFittingCall(expression,arguments))return *value;
        if(auto value=fittingCall(expression,arguments))return *value;
        if(auto value=optimizationCall(expression,arguments))return *value;
        if(auto value=iterationCall(expression,arguments))return *value;
        if(auto value=mapsCall(expression,arguments))return *value;
        if(auto value=pdeEnsembleCall(expression,arguments))return *value;
        if(auto value=pdeCall(expression,arguments))return *value;
        if(auto value=femCall(expression,arguments))return *value;
        if(auto value=bifurcationCall(expression,arguments))return *value;
        if(auto value=experimentBundleCall(expression,arguments))return *value;
        if(auto value=extraGeometryCall(expression,arguments))return *value;
        if(auto value=geometryCall(expression,arguments))return *value;
        if(auto value=chaosCall(expression,arguments))return *value;
        if(auto value=chartGeometryCall(expression,arguments))return *value;
        if(auto value=modelSensitivityCall(expression,arguments))return *value;
        if(auto value=sensitivityCall(expression,arguments))return *value;
        if(auto value=daeCall(expression,arguments))return *value;
        if(auto value=quantityCall(expression,arguments))return *value;
        if(auto value=constrainedMechanicsCall(expression,arguments))return *value;
        if(auto value=mechanicsCall(expression,arguments))return *value;
        if(auto value=ndCall(expression,arguments))return *value;
        if(auto value=inferenceCall(expression,arguments))return *value;
        if(auto value=animationCall(expression,arguments))return *value;
        if(auto value=videoCall(expression,arguments))return *value;
        if(auto value=publicationCall(expression,arguments))return *value;
        if(auto value=verificationCall(expression,arguments))return *value;
        if(auto value=hybridOdeCall(expression,arguments))return *value;
        if(auto value=discoveryCall(expression,arguments))return *value;
        if(auto value=provenanceCall(expression,arguments))return *value;
        if(auto value=pentSymbolicCall(expression,arguments))return *value;
        if(auto value=orbitsCall(expression,arguments))return *value;
        if(auto value=graphViewCall(expression,arguments))return *value;
        if(auto value=graphToolsCall(expression,arguments))return *value;
        if(auto value=networkCall(expression,arguments))return *value;
        if(auto value=graphCall(expression,arguments))return *value;
        if(auto value=scienceDataCall(expression,arguments))return *value;
        if(auto value=evolutionCall(expression,arguments))return *value;
        if(auto value=derivativeModelCall(expression,arguments))return *value;
        if(auto value=vectorThreadingCall(expression,arguments))return *value;
        if(auto value=linalgCall(expression,arguments))return *value;
        if(auto value=derivativeCall(expression,arguments))return *value;
        if(auto value=thirdDerivativeCall(expression,arguments))return *value;
        if(auto value=rootsCall(expression,arguments))return *value;
        if(auto value=foundationCall(expression,arguments))return *value;
        if(auto value=parallelCall(expression,arguments))return *value;
        if(auto value=foundationNumericCall(expression,arguments))return *value;
        if(auto value=utilityCall(expression,arguments))return *value;
        if(auto value=dataFileCall(expression,arguments))return *value;
        if(auto value=matrixCall(expression,arguments))return *value;
        if(name=="toc")return elapsedSeconds(span);
        if(name=="coeff" && std::holds_alternative<PentSeries>(arguments.at(0))) {
            const auto& series=std::get<PentSeries>(arguments[0]);const I index=integer(arguments.at(1),span);
            if(index<0||index>4) fail("R1004","Coefficient index must be in0..4.",span);
            return RealSeries{series.data,series.offset+static_cast<std::size_t>(index),false};
        }
        if(name=="coeff" && std::holds_alternative<UniversalSeries>(arguments.at(0))) {
            const auto& series=std::get<UniversalSeries>(arguments[0]);const I index=integer(arguments.at(1),span);
            if(index<0||static_cast<std::size_t>(index)>=series.algebra->dimension())fail("R2530","Coefficient index lies outside the series algebra dimension.",span);
            return RealSeries{series.data,series.offset+static_cast<std::size_t>(index),false};
        }
        if (name == "sqrt") {
            const double x = real(arguments.at(0), span);
            if (x < 0) fail("R1001", "sqrt requires a nonnegative real value.", span);
            return finite(std::sqrt(x), span);
        }
        if (name == "sin" || name == "cos" || name == "exp" || name == "sinh" || name == "cosh") {
            const auto& argument = arguments.at(0);
            if (const auto* value = std::get_if<P>(&argument)) {
                if (name == "sin") return algebra::sine(*value);
                if (name == "cos") return algebra::cosine(*value);
                if (name == "exp") return algebra::exponential(*value);
                if (name == "sinh") return algebra::hyperbolicSine(*value);
                return algebra::hyperbolicCosine(*value);
            }
            const double value = real(argument, span);
            if (name == "sin") return finite(std::sin(value), span);
            if (name == "cos") return finite(std::cos(value), span);
            if (name == "exp") return finite(std::exp(value), span);
            if (name == "sinh") return finite(std::sinh(value), span);
            return finite(std::cosh(value), span);
        }
        if (name == "tan" || name == "asin" || name == "acos" || name == "atan" || name == "atan2" ||
            name == "tanh" || name == "asinh" || name == "acosh" || name == "atanh" || name == "log" ||
            name == "log10" || name == "log2" || name == "abs" || name == "floor" || name == "ceil" || name == "round") {
            const double value = real(arguments.at(0), span);
            if ((name == "asin" || name == "acos") && (value < -1 || value > 1))
                fail("R1001", name + " requires a real value in [-1,1].", span);
            if (name == "acosh" && value < 1) fail("R1001", "acosh requires a real value >=1.", span);
            if (name == "atanh" && (value <= -1 || value >= 1)) fail("R1001", "atanh requires a real value strictly between -1 and 1.", span);
            if ((name == "log" || name == "log10" || name == "log2") && value <= 0)
                fail("R1001", name + " requires a positive real value.", span);
            if (name == "tan") return finite(std::tan(value), span);
            if (name == "asin") return finite(std::asin(value), span);
            if (name == "acos") return finite(std::acos(value), span);
            if (name == "atan") return finite(std::atan(value), span);
            if (name == "atan2") return finite(std::atan2(value, real(arguments.at(1), span)), span);
            if (name == "tanh") return finite(std::tanh(value), span);
            if (name == "asinh") return finite(std::asinh(value), span);
            if (name == "acosh") return finite(std::acosh(value), span);
            if (name == "atanh") return finite(std::atanh(value), span);
            if (name == "log") return finite(std::log(value), span);
            if (name == "log10") return finite(std::log10(value), span);
            if (name == "log2") return finite(std::log2(value), span);
            if (name == "abs") return finite(std::abs(value), span);
            if (name == "floor") return finite(std::floor(value), span);
            if (name == "ceil") return finite(std::ceil(value), span);
            return finite(std::round(value), span);
        }
        const P first = element(arguments.at(0), span);
        if (name == "conj") return algebra::conjugate(first);
        if (name == "scalar") return algebra::scalarPart(first);
        if (name == "pure") return algebra::purePart(first);
        if (name == "norm2") return algebra::normSquared(first);
        if (name == "inv") return algebra::inverse(first);
        if (name == "coeff") {
            const I index = integer(arguments.at(1), span);
            if (index < 0 || index > 4) fail("R1004", "Coefficient index must be in 0..4.", span);
            return algebra::coefficient(first, static_cast<std::size_t>(index));
        }
        if (name == "commutator") return algebra::commutator(first, element(arguments.at(1), span));
        if (name == "associator") return algebra::associator(first, element(arguments.at(1), span),
                                                            element(arguments.at(2), span));
        fail("R1999", "Unknown checked builtin.", span);
    }
    Value solveOde(const Expr& expression,std::size_t depth) {
        if(!expression.ode) fail("R1999","Missing checked ODE model.",expression.span);
        if(odeSolves_>=std::min<std::size_t>(64,options_.maxOdeSolves))fail("R1005","Native ODE solve count exceeds the request budget.",expression.span);
        ++odeSolves_;
        std::map<std::string,Value> bindings;
        for(std::size_t index=0;index<expression.children.size();++index)
            bindings.insert_or_assign(expression.argumentNames[index],evaluate(*expression.children[index],depth+1));
        std::vector<double> initial,parameters;
        const auto append=[&](const language::OdeVariable& variable,std::vector<double>& flattened) {
            appendNativeCoordinates(bindings.at(variable.name),variable,flattened,expression.span);
        };
        for(const auto& state:expression.ode->states) append(state,initial);
        for(const auto& parameter:expression.ode->parameters) append(parameter,parameters);
        ode::SolveOptions settings;settings.method=ode::Method::Bdf;
        settings.maxRetainedCoordinates=std::min<std::size_t>(32000000,options_.maxRetainedSolutionCoordinates);
        const auto scalar=[&](const char* name,double& target){if(bindings.contains(name))target=real(bindings.at(name),expression.span);};
        scalar("t0",settings.t0);scalar("t1",settings.t1);scalar("sample_step",settings.sampleStep);
        scalar("rtol",settings.rtol);scalar("atol",settings.atol);scalar("max_seconds",settings.maximumWallSeconds);
        if(bindings.contains("method")) {
            const auto& method=std::get<std::string>(bindings.at("method"));
            if(method=="bdf")settings.method=ode::Method::Bdf;
            else if(method=="adams")settings.method=ode::Method::Adams;
            else if(method=="erk")settings.method=ode::Method::Erk;
            else if(method=="esdirk")settings.method=ode::Method::Esdirk;
            else fail("R1801","Unknown ODE method.",expression.span);
        }
        if(bindings.contains("jacobian"))settings.jacobian=std::get<std::string>(bindings.at("jacobian"));
        const auto count=[&](const char* name,std::size_t& target){if(bindings.contains(name)) {
            const auto value=integer(bindings.at(name),expression.span);
            if(value<=0)fail("R1801","ODE resource limits must be positive integers.",expression.span);
            target=static_cast<std::size_t>(value);
        }};
        count("max_steps",settings.maxSteps);count("max_samples",settings.maxSamples);
        ode::NativeOptions native;native.cacheDirectory=options_.odeCacheDirectory;native.cancellation=options_.cancellation;
        native.cacheStatus=nativeCacheStatus();
        native.compilationFinished=[this](double seconds){recordCompilation(seconds);};
        try{
            if(const auto units=bindings.find("units");units!=bindings.end())
                (void)equationUnitsCall(expression,{ModelValue{expression.ode},units->second});
            if(const auto live=bindings.find("live_plot");live!=bindings.end())native.sampleObserver=scientificLiveObserver(&live->second,"ODE",initial.size(),expression.span);
            configureAdvancedOde(expression,bindings,settings,native);
            auto solution=ode::solve(*expression.ode,context_.gamma(),initial,parameters,settings,native);
            if(!currentTarget_.empty()) {
                std::unordered_set<const ode::Solution*> unique;
                unique.insert(solution.get());
                for(const auto& [name,value]:variables_) {
                    if(name==currentTarget_)continue;
                    if(const auto* x=std::get_if<SolutionValue>(&value))unique.insert(x->data.get());
                    if(const auto* x=std::get_if<RealSeries>(&value))unique.insert(x->data.get());
                    if(const auto* x=std::get_if<PentSeries>(&value))unique.insert(x->data.get());
                }
                const auto limit=std::min<std::size_t>(8000000,options_.maxSolutionCoordinates);
                const auto retainedLimit=std::min<std::size_t>(32000000,options_.maxRetainedSolutionCoordinates);
                std::size_t total=0,retainedTotal=0;
                for(const auto* data:unique) {
                    if(data->values.size()>limit-total)fail("R1005","Retained solution states exceed the workspace coordinate budget.",expression.span);
                    if(data->retainedCoordinateCount()>retainedLimit-retainedTotal)fail("R1005","Retained solution data exceed the auxiliary storage budget.",expression.span);
                    total+=data->values.size();retainedTotal+=data->retainedCoordinateCount();
                }
            }
            return SolutionValue{std::move(solution),expression.ode};
        }
        catch(const ode::OdeError& error) {
            if(options_.cancellation && options_.cancellation->load(std::memory_order_relaxed))fail("R1006","Native ODE execution cancelled.",expression.span);
            fail(error.code,error.what(),error.frames.empty()&&error.span.length?error.span:expression.span);
        }
    }
    Value evaluate(const Expr& expression, std::size_t depth = 0) {
        tick(expression.span);
        if (depth > 256) fail("R1005", "Execution expression depth limit exceeded.", expression.span);
        try {
        switch (expression.kind) {
        case ExprKind::FunctionHandle:return evaluateHandle(expression);
        case ExprKind::Lambda:return evaluateLambda(expression);
        case ExprKind::Invoke:{Value callable=evaluate(*expression.children[0],depth+1);std::vector<Value> arguments;for(std::size_t n=1;n<expression.children.size();++n)arguments.push_back(evaluate(*expression.children[n],depth+1));return invokeCallable(callable,arguments,expression.span);}
        case ExprKind::TupleLiteral:{std::vector<Value> values;for(const auto& child:expression.children)values.push_back(evaluate(*child,depth+1));return makeTuple(std::move(values),expression.span);}
        case ExprKind::MatrixLiteral:return expression.type==Type::ElementMatrix?universalArrayLiteral(expression,depth):matrixLiteral(expression,depth);
        case ExprKind::AllIndices:fail("R1902","Colon is only valid as an index selector.",expression.span);
        case ExprKind::EndIndex:
            if(!indexEnd_)fail("R1902","end is only valid inside an index selector.",expression.span);
            return *indexEnd_;
        case ExprKind::Index: {
            const auto base=evaluate(*expression.children.at(0),depth+1);
            return readIndex(base,std::vector<language::ExprPtr>(expression.children.begin()+1,expression.children.end()),expression.span,depth);
        }
        case ExprKind::Transpose: {
            const auto base=evaluate(*expression.children.at(0),depth+1);
            return transposeValue(base,expression.text=="'",expression.span);
        }
        case ExprKind::OdeSolve:{auto value=solveOde(expression,depth);captureResearchCall(expression,{ModelValue{expression.ode}},value);return value;}
        case ExprKind::Member: {
            const Value base=evaluate(*expression.children.at(0),depth+1);
            if(const auto* definition=std::get_if<UniversalDefinition>(&base)){
                const auto& names=(*definition)->basis();const auto found=std::find(names.begin(),names.end(),expression.text);
                if(found==names.end())fail("R2530","Unknown basis member in algebra definition.",expression.span);
                return UniversalElement::basis(*definition,static_cast<std::size_t>(found-names.begin()));
            }
            if(const auto* record=std::get_if<RecordValue>(&base)){const auto found=(*record)->fields.find(expression.text);if(found==(*record)->fields.end())fail("R1952","Unknown record member: "+expression.text,expression.span);return found->second;}
            const auto solution=std::get<SolutionValue>(base);
            if(expression.text=="t")return RealSeries{solution.data,0,true};
            std::size_t offset=0;
            for(std::size_t state=0;state<solution.data->stateNames.size();++state) {
                const auto width=solution.data->stateWidths.at(state);
                if(solution.data->stateNames[state]==expression.text){
                    if(solution.definition&&state<solution.definition->states.size()){
                        const auto& variable=solution.definition->states[state];
                        if(variable.type==Type::Element)return UniversalSeries{solution.data,offset,variable.schema->algebra,false};
                        if(variable.type==Type::Complex)return UniversalSeries{solution.data,offset,algebra::universal::builtin("complex"),true};
                        if(variable.type==Type::RealMatrix||variable.type==Type::PentarnionMatrix||variable.type==Type::ElementMatrix||variable.type==Type::Tensor)return ArraySeries{solution.data,offset,variable.type,variable.schema};
                    }
                    return width==5?Value(PentSeries{solution.data,offset}):Value(RealSeries{solution.data,offset,false});
                }
                offset+=width;
            }
            fail("R1810","A checked solution member was not found.",expression.span);
        }
        case ExprKind::IntegerLiteral: {
            I value{};
            const auto parsed = std::from_chars(expression.text.data(), expression.text.data() + expression.text.size(), value);
            if (parsed.ec != std::errc{} || parsed.ptr != expression.text.data() + expression.text.size())
                fail("R1002", "Integer literal cannot be represented.", expression.span);
            return value;
        }
        case ExprKind::RealLiteral: {
            double value{};
            const auto parsed = std::from_chars(expression.text.data(), expression.text.data() + expression.text.size(), value,
                                                std::chars_format::general);
            if (parsed.ec != std::errc{}) fail("R1001", "Real literal cannot be represented.", expression.span);
            return finite(value, expression.span);
        }
        case ExprKind::StringLiteral: return expression.text;
        case ExprKind::Reference: {
            if(expression.type==Type::Algebra&&expression.schema&&expression.schema->algebra)return expression.schema->algebra;
            constexpr const char* basisNames[] = {"i", "j", "k", "ell"};
            for (std::size_t n = 0; n < 4; ++n)
                if (expression.text == basisNames[n]) return P::basis(context_, n + 1);
            const auto found = variables_.find(expression.text);
            if (found == variables_.end()) fail("R1999", "A checked reference has no value.", expression.span);
            return found->second;
        }
        case ExprKind::Group: return evaluate(*expression.children.at(0), depth + 1);
        case ExprKind::Unary: {
            const auto& child = *expression.children.at(0);
            // This exact directly-negated literal is valid even though its
            // positive spelling cannot itself be evaluated as signed int64.
            if (expression.text == "-" && child.kind == ExprKind::IntegerLiteral) {
                std::string_view magnitude(child.text);
                while (magnitude.size() > 1 && magnitude.front() == '0') magnitude.remove_prefix(1);
                if (magnitude == "9223372036854775808") return std::numeric_limits<I>::min();
            }
            Value value = evaluate(child, depth + 1);
            if(auto result=foundationUnary(expression,value))return *result;
            if(auto result=parallelUnary(expression,value))return *result;
            if(expression.text=="!"||expression.text=="~")return I(!truth(value,expression.span));
            if (expression.text == "+") return value;
            if(std::holds_alternative<TensorValue>(value)){Expr scale;scale.text="*";scale.span=expression.span;return *tensorBinary(scale,value,Value(I(-1)));}
            if(std::holds_alternative<UniversalMatrix>(value)){Expr scale;scale.text="*";scale.span=expression.span;return *universalArrayBinary(scale,value,Value(I(-1)));}
            if(std::holds_alternative<MatrixValue>(value)) {
                Expr scale;scale.text="*";scale.span=expression.span;
                return matrixBinary(scale,Value(I(-1)),value);
            }
            if (const auto* x = std::get_if<I>(&value)) {
                if (*x == std::numeric_limits<I>::min()) fail("R1002", "Integer negation overflow.", expression.span);
                return -*x;
            }
            if (const auto* x = std::get_if<double>(&value)) return -*x;
            if(const auto* item=std::get_if<UniversalElement>(&value))return algebra::universal::scale(*item,-1);
            return algebra::scale(std::get<P>(value), -1.0);
        }
        case ExprKind::Binary: {
            Value left = evaluate(*expression.children.at(0), depth + 1);
            if(expression.text=="&&"&&!truth(left,expression.span))return I(0);
            if(expression.text=="||"&&truth(left,expression.span))return I(1);
            Value right = evaluate(*expression.children.at(1), depth + 1);
            if(expression.text=="&&"||expression.text=="||")return I(truth(right,expression.span));
            return binary(expression, left, right);
        }
        case ExprKind::PentConstructor: {
            algebra::Coefficients coefficients{};
            for (std::size_t n = 0; n < coefficients.size(); ++n)
                coefficients[n] = real(evaluate(*expression.children.at(n), depth + 1), expression.children.at(n)->span);
            return P(context_, coefficients);
        }
        case ExprKind::Call: {
            std::vector<Value> arguments;
            for (const auto& child : expression.children) arguments.push_back(evaluate(*child, depth + 1));
            auto value=call(expression, arguments);captureResearchCall(expression,arguments,value);return value;
        }
        case ExprKind::Range: {
            Value start = evaluate(*expression.children.at(0), depth + 1);
            Value step = evaluate(*expression.children.at(1), depth + 1);
            Value stop = evaluate(*expression.children.at(2), depth + 1);
            if (expression.type == Type::IntegerRange) {
                const I increment = integer(step, expression.span);
                if (increment == 0) fail("R1008", "A finite range step cannot be zero.", expression.span);
                return IntegerRange{integer(start, expression.span), increment, integer(stop, expression.span)};
            }
            const double increment = real(step, expression.span);
            if (increment == 0) fail("R1008", "A finite range step cannot be zero.", expression.span);
            return RealRange{real(start, expression.span), increment, real(stop, expression.span)};
        }
        }
        fail("R1999", "Unknown checked expression.", expression.span);
        } catch (const ode::OdeError& error) {
            fail(error.code,error.what(),expression.span);
        } catch (const algebra::AlgebraError& error) {
            const bool divisionFailure = error.code == "R3005" || error.code == "R3006" ||
                error.code == "R3007" || error.code == "R3008";
            fail(divisionFailure ? "R1003" : "R1001",
                 error.code + ": " + error.what(), expression.span);
        }
    }
    ExecutionResult& result_;
    const ExecutionOptions& options_;
    // The caller retains these exact invocation bytes for this evaluator's
    // lifetime. Bundle export copies them as data only; loading never runs them.
    std::string_view executedSource_;
    algebra::Context context_;
    Environment& variables_;
    Timer& timer_;
    double& compilationSeconds_;
    Random& random_;
    detail::ParallelState& parallel_;
    detail::ResearchCapture& researchCapture_;
    std::optional<Value> lastValue_;
    std::string lastName_;
    std::string currentTarget_;
    std::size_t odeSolves_=0;
    std::size_t plotPoints_=0;
    std::size_t outputBytes_ = 0;
    SourceSpan currentSpan_;
};
ExecutionResult executeEnvironment(std::string_view source, const language::AnalysisOptions& options,
    const ExecutionOptions& execution, Environment& variables,Timer& timer,double& compilationSeconds,Random& random,detail::ParallelState& parallel,detail::ResearchCapture& researchCapture, bool reuse, bool automaticEcho) {
    ExecutionResult result;
    result.sourceHash=ode::detail::sha256(source);
    auto analysisOptions = options;
    if(analysisOptions.sourceIdentity.empty())analysisOptions.sourceIdentity=automaticEcho?"<command>":"<source>";
    analysisOptions.initialSymbols.clear();
    if (reuse)
        for (const auto& [name, value] : variables) {
            std::shared_ptr<const language::OdeDefinition> definition;
            if(const auto* model=std::get_if<ModelValue>(&value))definition=model->definition;
            if(const auto* solution=std::get_if<SolutionValue>(&value))definition=solution->definition;
            std::shared_ptr<const language::FunctionDefinition> function;
            if(const auto* callable=std::get_if<FunctionValue>(&value))function=callable->definition;
            analysisOptions.initialSymbols.push_back({name,valueType(value),{0,0},definition,function,runtimeSchema(value)});
        }
    if (execution.cancellation) analysisOptions.cancellation = execution.cancellation;
    if(analysisOptions.sourceDirectory.empty())analysisOptions.sourceDirectory=execution.dataDirectory;
    result.analysis = language::analyse(source, analysisOptions);
    if (result.analysis.cancelled) { result.status = ExecutionStatus::Cancelled; return result; }
    if (!result.analysis.ok()) return result;
    if (options.languageVersion != "0.2") {
        result.runtimeDiagnostics.push_back({language::Severity::Error, "R1007",
            "Execution requires language 0.2. Explicitly upgrade the project's editor profile first.", {0, 0}});
        result.status = ExecutionStatus::RuntimeError;
        return result;
    }
    const auto firstUse=std::find_if(result.analysis.program.statements.begin(),result.analysis.program.statements.end(),[](const auto& statement){return statement.kind!=language::StatementKind::Directive&&statement.kind!=language::StatementKind::ClearConsole;});
    const bool leadingWorkspaceReset=firstUse!=result.analysis.program.statements.end()&&firstUse->kind==language::StatementKind::Clear&&firstUse->names.empty();
    if (reuse && !variables.empty() && !leadingWorkspaceReset && (result.analysis.program.gamma != options.gamma ||
                                       result.analysis.program.algebraId != options.algebraId)) {
        result.runtimeDiagnostics.push_back({language::Severity::Error, "R1009",
            "Clear the command workspace before changing its algebra context.", {0, 0}});
        result.status = ExecutionStatus::RuntimeError;
        return result;
    }
    if (!reuse) {variables.clear();timer.reset();researchCapture={};}
    {std::ostringstream randomState;randomState.imbue(std::locale::classic());randomState<<random.engine;researchCapture.currentInitialRandomState=randomState.str();}
    auto executionOptions = execution;
    if (!executionOptions.cancellation) executionOptions.cancellation = analysisOptions.cancellation;
    Evaluator evaluator(result, executionOptions, variables,timer,compilationSeconds,random,parallel,researchCapture,source);
    try {
        evaluator.run(automaticEcho);
        if (executionOptions.cancellation && executionOptions.cancellation->load(std::memory_order_relaxed))
            fail("R1006", "Execution cancelled.", {0, 0});
        result.status = ExecutionStatus::Success;
    } catch (const Failure& error) {
        result.runtimeDiagnostics.push_back({language::Severity::Error, error.code, error.what(), error.span,error.frames});
        result.status = error.code == "R1006" ? ExecutionStatus::Cancelled : ExecutionStatus::RuntimeError;
    } catch (const algebra::AlgebraError& error) {
        result.runtimeDiagnostics.push_back({language::Severity::Error, "R1001",
            error.code + ": " + error.what(), {0, 0}});
        result.status = ExecutionStatus::RuntimeError;
    } catch (const std::exception& error) {
        result.runtimeDiagnostics.push_back({language::Severity::Error, "R1999", error.what(), {0, 0}});
        result.status = ExecutionStatus::RuntimeError;
    }
    if (result.status == ExecutionStatus::Cancelled) result.figureActions.clear();
    else evaluator.snapshots();
    return result;
}

void snapshotEnvironment(ExecutionResult& result, const Environment& variables) {
    result.variables.clear();
    for (const auto& [name, value] : variables)
        result.variables.push_back({name, std::string(language::typeName(valueType(value))), valueText(value)});
}

std::pair<std::string, bool> consoleSource(std::string_view command) {
    const auto tokens = language::lex(command).tokens;
    const language::Token* last = nullptr;
    for (const auto& token : tokens)
        if (token.kind != language::TokenKind::Comment && token.kind != language::TokenKind::EndOfFile) last = &token;
    if (!last) return {std::string(command), false};
    if (command.substr(last->span.offset, last->span.length) == ";") return {std::string(command), false};
    std::string source(command);
    source.insert(last->span.offset + last->span.length, ";");
    return {std::move(source), true};
}
} // namespace

struct InterpreterSession::State {
    Environment variables;
    Timer timer;
    double compilationSeconds=0;
    Random random;
    detail::ParallelState parallel;
    detail::ResearchCapture researchCapture;
    language::AnalysisOptions context;
    bool initialized = false;
};
InterpreterSession::InterpreterSession() : state_(std::make_unique<State>()) {}
InterpreterSession::~InterpreterSession() = default;
InterpreterSession::InterpreterSession(InterpreterSession&&) noexcept = default;
InterpreterSession& InterpreterSession::operator=(InterpreterSession&&) noexcept = default;
void InterpreterSession::reset() { state_ = std::make_unique<State>(); }
void InterpreterSession::restoreRandomState(std::string_view snapshot) {
    if(state_->initialized||!state_->variables.empty()||snapshot.empty()||snapshot.size()>32768)
        throw std::invalid_argument("Restore a bounded random state into a fresh replay session before submitting source.");
    std::size_t offset=0;bool nonzero=false;
    auto space=[](char c){return c==' '||c=='\t'||c=='\r'||c=='\n';};
    for(std::size_t index=0;index<313;++index){
        while(offset<snapshot.size()&&space(snapshot[offset]))++offset;
        const auto begin=offset;std::uint64_t value=0;
        while(offset<snapshot.size()&&!space(snapshot[offset])){
            const auto c=snapshot[offset++];if(c<'0'||c>'9'||value>(std::numeric_limits<std::uint64_t>::max()-static_cast<unsigned>(c-'0'))/10)
                throw std::invalid_argument("Random replay state requires exactly312 unsigned64-bit words and a valid engine index.");
            value=value*10+static_cast<unsigned>(c-'0');
        }
        if(offset==begin||(index==312&&value>312))throw std::invalid_argument("Random replay state has a missing word or invalid engine index.");
        if(index<312&&value)nonzero=true;
    }
    while(offset<snapshot.size()&&space(snapshot[offset]))++offset;
    if(offset!=snapshot.size()||!nonzero)throw std::invalid_argument("Random replay state is degenerate or has extra data.");
    std::istringstream input{std::string(snapshot)};input.imbue(std::locale::classic());std::mt19937_64 engine;
    if(!(input>>engine))throw std::invalid_argument("Invalid numeric random replay state.");
    input>>std::ws;if(!input.eof())throw std::invalid_argument("Random replay state has trailing data.");
    state_->random.engine=engine;
}

ExecutionResult InterpreterSession::runFile(std::string_view source,
    const language::AnalysisOptions& options, const ExecutionOptions& execution) {
    const auto previous = state_->variables;
    const auto previousTimer=state_->timer;
    const auto previousRandom=state_->random;
    const auto previousParallel=state_->parallel;
    const auto previousCapture=state_->researchCapture;
    auto result = executeEnvironment(source, options, execution, state_->variables,state_->timer,state_->compilationSeconds,state_->random,state_->parallel,state_->researchCapture, false, false);
    if (result.status == ExecutionStatus::Cancelled) {
        state_->variables = previous;
        state_->timer=previousTimer;
        state_->random=previousRandom;state_->parallel=previousParallel;state_->researchCapture=previousCapture;
        snapshotEnvironment(result, state_->variables);
    } else if (result.analysis.ok() && options.languageVersion == "0.2") {
        state_->context = options;
        state_->context.gamma = result.analysis.program.gamma;
        state_->context.algebraId = result.analysis.program.algebraId;
        state_->context.initialSymbols.clear(); state_->context.cancellation.reset();
        state_->initialized = true;
    } else snapshotEnvironment(result, state_->variables);
    if(result.ok())detail::appendResearchInvocation(state_->researchCapture,source,result.analysis,options.sourceIdentity,state_->researchCapture.currentInitialRandomState);
    else if(result.status==ExecutionStatus::RuntimeError)state_->researchCapture.historyComplete=false;
    return result;
}

ExecutionResult InterpreterSession::submit(std::string_view command,
    const language::AnalysisOptions& options, const ExecutionOptions& execution) {
    auto actual = options;
    actual.languageVersion = "0.2";
    if (state_->initialized) {
        actual.gamma = state_->context.gamma;
        actual.algebraId = state_->context.algebraId;
    }
    const auto previous = state_->variables;
    const auto previousTimer=state_->timer;
    const auto previousRandom=state_->random;
    const auto previousParallel=state_->parallel;
    const auto previousCapture=state_->researchCapture;
    if (command.size() > actual.maximumSourceBytes) {
        auto rejected = executeEnvironment(command, actual, execution, state_->variables,state_->timer,state_->compilationSeconds,state_->random,state_->parallel,state_->researchCapture, true, false);
        snapshotEnvironment(rejected, state_->variables);
        return rejected;
    }
    const auto [source, echo] = consoleSource(command);
    auto result = executeEnvironment(source, actual, execution, state_->variables,state_->timer,state_->compilationSeconds,state_->random,state_->parallel,state_->researchCapture, true, echo);
    const bool failedContextChange=result.status==ExecutionStatus::RuntimeError&&result.analysis.ok()&&
        (result.analysis.program.gamma!=actual.gamma||result.analysis.program.algebraId!=actual.algebraId);
    if (result.status == ExecutionStatus::Cancelled || failedContextChange) {
        state_->variables = previous;
        state_->timer=previousTimer;
        state_->random=previousRandom;state_->parallel=previousParallel;state_->researchCapture=previousCapture;
        snapshotEnvironment(result, state_->variables);
    } else if (result.analysis.ok() && (result.runtimeDiagnostics.empty() || result.runtimeDiagnostics.front().code != "R1009")) {
        state_->context = actual;
        state_->context.gamma = result.analysis.program.gamma;
        state_->context.algebraId = result.analysis.program.algebraId;
        state_->context.initialSymbols.clear(); state_->context.cancellation.reset();
        state_->initialized = true;
    } else snapshotEnvironment(result, state_->variables);
    if(result.ok())detail::appendResearchInvocation(state_->researchCapture,source,result.analysis,actual.sourceIdentity.empty()?"<command>":actual.sourceIdentity,state_->researchCapture.currentInitialRandomState);
    else if(result.status==ExecutionStatus::RuntimeError){state_->researchCapture=previousCapture;state_->researchCapture.historyComplete=false;}
    return result;
}

ExecutionResult execute(std::string_view source, const language::AnalysisOptions& options,
                         const ExecutionOptions& execution) {
    InterpreterSession session;
    return session.runFile(source, options, execution);
}
}
