#include "frontend.hpp"
#include "universal_import_data.hpp"
#include "extension_names.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <limits>
#include <fstream>
#include <sstream>
#include <optional>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace penta::language {
namespace {
struct ParseError {};
struct StopAnalysis {};

bool scalarType(Type type) { return type == Type::Integer || type == Type::Real; }
bool numericType(Type type) { return scalarType(type) || type == Type::Pentarnion || type==Type::Complex; }
bool matrixType(Type type) { return type == Type::RealMatrix || type == Type::PentarnionMatrix || type==Type::LogicalMatrix; }
bool pentarnionType(Type type) { return type == Type::Pentarnion || type == Type::PentarnionMatrix; }
bool arrayFunction(std::string_view name) {
    constexpr std::array names{std::string_view("zeros"),std::string_view("ones"),std::string_view("eye"),
        std::string_view("rand"),std::string_view("randn"),std::string_view("randi"),std::string_view("rng"),
        std::string_view("size"),std::string_view("numel"),std::string_view("length"),std::string_view("transpose"),
        std::string_view("linspace"),std::string_view("sum"),std::string_view("min"),std::string_view("max"),std::string_view("mean")};
    return std::find(names.begin(),names.end(),name)!=names.end();
}
bool arrayDeclaration(std::string_view name) { return name=="matrix" || name=="vector" || name=="realmatrix" || name=="pentarnionmatrix"; }
bool rangeType(Type type) { return type == Type::IntegerRange || type == Type::RealRange; }
bool multiplicative(std::string_view op) { return op == "*" || op == "/" || op == "\\" || op==".*" || op=="./"; }
SourceSpan cover(SourceSpan first, SourceSpan last) {
    return {first.offset, last.offset + last.length - first.offset};
}

std::string decoded(std::string_view literal) {
    std::string result;
    const std::size_t end = literal.size() > 1 && literal.back() == '"' ? literal.size() - 1 : literal.size();
    for (std::size_t index = 1; index < end; ++index) {
        char current = literal[index];
        if (current == '\\' && index + 1 < end) {
            current = literal[++index];
            if (current == 'n') current = '\n';
            else if (current == 'r') current = '\r';
            else if (current == 't') current = '\t';
        }
        result.push_back(current);
    }
    return result;
}

std::optional<std::uint64_t> unsignedInteger(std::string_view text) {
    std::uint64_t value = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc() || parsed.ptr != text.data() + text.size()) return {};
    return value;
}

std::optional<double> realLiteral(std::string_view text) {
    double value = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value, std::chars_format::general);
    if (parsed.ec != std::errc() || parsed.ptr != text.data() + text.size() || !std::isfinite(value)) return {};
    return value;
}

// Recognise signs/groups on literals only. Never evaluate arithmetic, variables
// or calls in order to infer values during static analysis.
std::optional<double> signedLiteral(const ExprPtr& expression) {
    if (!expression) return {};
    if (expression->kind == ExprKind::IntegerLiteral || expression->kind == ExprKind::RealLiteral)
        return realLiteral(expression->text);
    if (expression->kind == ExprKind::Group && expression->children.size() == 1)
        return signedLiteral(expression->children.front());
    if (expression->kind == ExprKind::Unary && expression->children.size() == 1) {
        auto value = signedLiteral(expression->children.front());
        if (value) return expression->text == "-" ? -*value : *value;
    }
    return {};
}

bool forbiddenConstruct(std::string_view name) {
    constexpr std::array names{std::string_view("for"), std::string_view("function"),
        std::string_view("if"), std::string_view("else"), std::string_view("while"),
        std::string_view("import"), std::string_view("namespace"), std::string_view("plot"), std::string_view("plot3"),
        std::string_view("search"), std::string_view("simulate"), std::string_view("solveODE"),
        std::string_view("ODE"), std::string_view("native"), std::string_view("parallel"),
        std::string_view("shell"), std::string_view("system"), std::string_view("eval"),
        std::string_view("include"), std::string_view("disp"), std::string_view("print")};
    return std::find(names.begin(), names.end(), name) != names.end();
}

bool additiveCommand(std::string_view name) {
    constexpr std::array names{std::string_view("display"), std::string_view("clear"), std::string_view("clc"),
        std::string_view("plot2"), std::string_view("plot3"), std::string_view("figure"),
        std::string_view("clf"), std::string_view("close"), std::string_view("dock"),
        std::string_view("undock"), std::string_view("tilefigures"), std::string_view("title"),
        std::string_view("xlabel"), std::string_view("ylabel"), std::string_view("zlabel"), std::string_view("caption"),
        std::string_view("subplot"), std::string_view("plot"), std::string_view("tic")};
    return std::find(names.begin(), names.end(), name) != names.end();
}
bool elementaryFunction(std::string_view name) {
    constexpr std::array names{std::string_view("sin"), std::string_view("cos"), std::string_view("exp"),
        std::string_view("cosh"), std::string_view("sinh"), std::string_view("tan"), std::string_view("asin"),
        std::string_view("acos"), std::string_view("atan"), std::string_view("atan2"), std::string_view("tanh"),
        std::string_view("asinh"), std::string_view("acosh"), std::string_view("atanh"), std::string_view("log"),
        std::string_view("log10"), std::string_view("log2"), std::string_view("abs"), std::string_view("floor"),
        std::string_view("ceil"), std::string_view("round")};
    return std::find(names.begin(), names.end(), name) != names.end();
}
bool utilityFunction(std::string_view name) {
    constexpr std::array names{std::string_view("assert"),std::string_view("error"),std::string_view("warning"),
        std::string_view("help"),std::string_view("lookfor"),std::string_view("who"),std::string_view("whos"),std::string_view("save"),
        std::string_view("load"),std::string_view("readmatrix"),std::string_view("writematrix"),
        std::string_view("csvread"),std::string_view("csvwrite"),std::string_view("xlsread"),std::string_view("xlswrite")};
    return std::find(names.begin(),names.end(),name)!=names.end();
}
bool controlName(std::string_view name) {
    constexpr std::array names{std::string_view("if"),std::string_view("else"),std::string_view("for"),
        std::string_view("while"),std::string_view("break"),std::string_view("continue"),std::string_view("function"),
        std::string_view("return"),std::string_view("void"),std::string_view("string"),std::string_view("true"),
        std::string_view("false"),std::string_view("end")};
    return std::find(names.begin(),names.end(),name)!=names.end();
}
bool foundationBuiltin(std::string_view name) {
    constexpr std::array names{std::string_view("options"),std::string_view("complex"),std::string_view("realpart"),std::string_view("imagpart"),std::string_view("arg"),std::string_view("with_options"),std::string_view("get_option"),std::string_view("reshape"),std::string_view("diag"),std::string_view("meshgrid"),std::string_view("ndgrid"),std::string_view("find"),std::string_view("logical"),std::string_view("dot"),std::string_view("norm"),std::string_view("interp1"),std::string_view("trapz"),std::string_view("cumtrapz"),std::string_view("diff"),std::string_view("gradient"),std::string_view("divergence"),std::string_view("laplacian"),std::string_view("deval"),std::string_view("integral"),std::string_view("threads"),std::string_view("parallel_info"),std::string_view("expm1"),std::string_view("log1p"),std::string_view("erf"),std::string_view("erfc"),std::string_view("hypot"),std::string_view("sign"),std::string_view("axpy")};
    return std::find(names.begin(),names.end(),name)!=names.end();
}
bool foundationKeyword(std::string_view name) {return name=="callback"||name=="options"||name=="record"||name=="tuple"||name=="complex"||name=="logicalmatrix"||name=="native"||name=="import"||name=="as"||name=="algebra"||name=="element";}
bool advancedBuiltin(std::string_view name) {
    if(extensionBuiltin(name))return true;
    constexpr std::array algebraNames{std::string_view("builtin_algebra"),std::string_view("import_algebra"),
        std::string_view("algebra_info"),std::string_view("algebra_basis"),std::string_view("algebra_coefficients"),
        std::string_view("algebra_leftop"),std::string_view("algebra_rightop"),std::string_view("algebra_solveleft"),
        std::string_view("algebra_solveright"),std::string_view("algebra_norm"),std::string_view("algebra_json"),std::string_view("algebra_products"),std::string_view("statecoord")};
    if(std::find(algebraNames.begin(),algebraNames.end(),name)!=algebraNames.end())return true;
    constexpr std::array names{std::string_view("matmul"),std::string_view("batchmul"),std::string_view("det"),std::string_view("leftop"),std::string_view("rightop"),std::string_view("solveleft"),std::string_view("solveright"),std::string_view("linsolve"),std::string_view("solve_report"),std::string_view("lu"),std::string_view("qr"),std::string_view("chol"),std::string_view("svd"),std::string_view("eig"),std::string_view("schur"),std::string_view("rank"),std::string_view("rcond"),std::string_view("cond"),std::string_view("sparse"),std::string_view("full"),std::string_view("nnz"),std::string_view("sparsemul"),std::string_view("sparsetranspose"),std::string_view("sparsesolve"),std::string_view("sparse_solve_report"),std::string_view("jacobian"),std::string_view("hessian"),std::string_view("jvp"),std::string_view("vjp"),std::string_view("hvp"),std::string_view("derivative_check"),std::string_view("thirdderivative"),std::string_view("root"),std::string_view("rootsolve"),std::string_view("equilibrium"),std::string_view("fixedpoint"),std::string_view("curvefit"),std::string_view("lsqfit"),std::string_view("lsqnonlin"),std::string_view("odefit"),std::string_view("minimize"),std::string_view("odeevents"),std::string_view("odesens"),std::string_view("odestats"),std::string_view("odewindow"),std::string_view("odeadjoint"),std::string_view("mapiterate"),std::string_view("mapdiagram"),std::string_view("returnmap"),std::string_view("compile_info"),std::string_view("pdesolve"),std::string_view("pdecoeff"),std::string_view("lyapunov"),std::string_view("poincare"),std::string_view("recurrence"),std::string_view("stability"),std::string_view("ensemble"),std::string_view("surf"),std::string_view("imagesc"),std::string_view("contour"),std::string_view("isosurface"),std::string_view("curvegeometry"),std::string_view("evolve"),std::string_view("fft"),std::string_view("ifft"),std::string_view("pwelch"),std::string_view("autocorr"),std::string_view("findpeaks"),std::string_view("variance"),std::string_view("stddev"),std::string_view("covariance"),std::string_view("quantile"),std::string_view("histogram"),std::string_view("normpdf"),std::string_view("normcdf"),std::string_view("norminv"),std::string_view("bootstrap"),std::string_view("sym"),std::string_view("symdiff"),std::string_view("symsubs"),std::string_view("symsimplify"),std::string_view("symnumeric"),std::string_view("symtext"),std::string_view("analysisrecord"),std::string_view("saveanalysis"),std::string_view("loadanalysis"),std::string_view("iterationinfo"),std::string_view("pdeensemble"),std::string_view("pdeensemble2"),std::string_view("pderun"),std::string_view("pdeparams"),std::string_view("pdesettings"),std::string_view("pdereplay"),std::string_view("graphsparse"),std::string_view("graphgen"),std::string_view("networksolve"),std::string_view("networkcoeff"),std::string_view("meshplane"),std::string_view("meshsphere"),std::string_view("meshcylinder"),std::string_view("meshcircle"),std::string_view("paramesh"),std::string_view("trimesh"),std::string_view("meshinfo"),std::string_view("meshtransform"),std::string_view("meshrefine"),std::string_view("meshplot"),std::string_view("meshread"),std::string_view("meshwrite"),std::string_view("lineplane"),std::string_view("raysphere"),std::string_view("dform"),std::string_view("formfield"),std::string_view("wedge"),std::string_view("exteriord"),std::string_view("hodge"),std::string_view("laplacebeltrami"),std::string_view("surfaceheat"),std::string_view("surfacecoeff"),std::string_view("savegraphlayout"),std::string_view("loadgraphlayout"),std::string_view("exportvideo"),std::string_view("profilefit"),std::string_view("fitbootstrap"),std::string_view("multistartfit"),std::string_view("ndarray"),std::string_view("complexarray"),std::string_view("pentarray"),std::string_view("ndinfo"),std::string_view("ndreshape"),std::string_view("ndpermute"),std::string_view("ndslice"),std::string_view("ndadd"),std::string_view("ndsub"),std::string_view("ndmul"),std::string_view("nddiv"),std::string_view("ndmatmul"),std::string_view("ndconj"),std::string_view("ndabs"),std::string_view("ndreal"),std::string_view("ndimag"),std::string_view("ndcoeff"),std::string_view("ndfft"),std::string_view("ndifft"),std::string_view("ndvalues"),std::string_view("ndplot"),std::string_view("ndmap"),std::string_view("ndget"),std::string_view("ndset"),std::string_view("ndinterp"),std::string_view("quantity"),std::string_view("unitconvert"),std::string_view("unitadd"),std::string_view("unitsub"),std::string_view("unitmul"),std::string_view("unitdiv"),std::string_view("unitpow"),std::string_view("unitvalues"),std::string_view("unitcompatible"),std::string_view("unitinfo"),std::string_view("constrainedmechanics"),std::string_view("modelsensitivity"),std::string_view("mechanics"),std::string_view("symexpand"),std::string_view("symcoeff"),std::string_view("sympentdiff"),std::string_view("sympentsubs"),std::string_view("sympentnumeric"),std::string_view("sympenttext"),std::string_view("sympentcode"),std::string_view("daesolve"),std::string_view("animatefield"),std::string_view("animationframe"),std::string_view("playanimation"),std::string_view("exportframes"),std::string_view("provenance"),std::string_view("researchrecord"),std::string_view("sensitivity"),std::string_view("exportexperiment"),std::string_view("chartgeometry"),std::string_view("geodesic"),std::string_view("hold"),std::string_view("legend"),std::string_view("plotstyle"),std::string_view("xlim"),std::string_view("ylim"),std::string_view("zlim"),std::string_view("axis"),std::string_view("linkaxes"),std::string_view("exportgraphics"),std::string_view("searchset"),std::string_view("searchinterval"),std::string_view("pentdirections"),std::string_view("femsolve"),std::string_view("femplot"),std::string_view("refinebifurcation"),std::string_view("saveexperiment"),std::string_view("loadexperiment"),std::string_view("experimentinfo"),std::string_view("experimentsource"),std::string_view("hybridsolve"),std::string_view("hybridval"),std::string_view("hybridadjoint"),std::string_view("verifyexperiment"),std::string_view("verifyrun"),std::string_view("discoverplan"),std::string_view("discover"),std::string_view("odeensemble"),std::string_view("discoverstatus"),std::string_view("experimentrun"),std::string_view("periodicorbit"),std::string_view("continueequilibrium"),std::string_view("continueperiodic"),std::string_view("graph"),std::string_view("digraph"),std::string_view("graphinfo"),std::string_view("graphcomponents"),std::string_view("components"),std::string_view("shortestpath"),std::string_view("spanningtree"),std::string_view("toposort"),std::string_view("centrality"),std::string_view("graphlaplacian"),std::string_view("graphadjacency"),std::string_view("graphlayout"),std::string_view("graphcycle"),std::string_view("plotgraph"),std::string_view("pdesolve2"),std::string_view("pdeslice")};
    return std::find(names.begin(),names.end(),name)!=names.end();
}
bool additiveName(std::string_view name) { return advancedBuiltin(name)||foundationBuiltin(name)||foundationKeyword(name)|| utilityFunction(name) || controlName(name) || additiveCommand(name) || elementaryFunction(name) || arrayFunction(name) || arrayDeclaration(name) || name == "ode" || name == "solution" || name == "odesolve" || name == "toc"; }
std::string asciiLower(std::string text) {
    for (char& character : text) if (character >= 'A' && character <= 'Z') character += 'a' - 'A';
    return text;
}

enum class Parameter { Scalar, Pentarnion, Integer, Numeric };
struct Signature {
    std::string_view name;
    std::size_t arity;
    Type result;
    std::array<Parameter, 5> parameters;
};
constexpr Parameter S = Parameter::Scalar;
constexpr Parameter P = Parameter::Pentarnion;
constexpr Parameter I = Parameter::Integer;
constexpr Parameter N = Parameter::Numeric;
constexpr std::array signatures{
    Signature{"sqrt", 1, Type::Real, {S}}, Signature{"pent", 5, Type::Pentarnion, {S,S,S,S,S}},
    Signature{"conj", 1, Type::Pentarnion, {P}}, Signature{"scalar", 1, Type::Real, {P}},
    Signature{"pure", 1, Type::Pentarnion, {P}}, Signature{"norm2", 1, Type::Real, {P}},
    Signature{"inv", 1, Type::Pentarnion, {P}}, Signature{"coeff", 2, Type::Real, {P,I}},
    Signature{"commutator", 2, Type::Pentarnion, {P,P}},
    Signature{"associator", 3, Type::Pentarnion, {P,P,P}},
    Signature{"sin", 1, Type::Real, {N}}, Signature{"cos", 1, Type::Real, {N}},
    Signature{"exp", 1, Type::Real, {N}}, Signature{"cosh", 1, Type::Real, {N}},
    Signature{"sinh", 1, Type::Real, {N}},
    Signature{"tan", 1, Type::Real, {S}}, Signature{"asin", 1, Type::Real, {S}},
    Signature{"acos", 1, Type::Real, {S}}, Signature{"atan", 1, Type::Real, {S}},
    Signature{"atan2", 2, Type::Real, {S,S}}, Signature{"tanh", 1, Type::Real, {S}},
    Signature{"asinh", 1, Type::Real, {S}}, Signature{"acosh", 1, Type::Real, {S}},
    Signature{"atanh", 1, Type::Real, {S}}, Signature{"log", 1, Type::Real, {S}},
    Signature{"log10", 1, Type::Real, {S}}, Signature{"log2", 1, Type::Real, {S}},
    Signature{"abs", 1, Type::Real, {S}}, Signature{"floor", 1, Type::Real, {S}},
    Signature{"ceil", 1, Type::Real, {S}}, Signature{"round", 1, Type::Real, {S}}
};

class Frontend {
public:
    Frontend(std::string_view source, const AnalysisOptions& options) : m_source(source), m_options(options) {
        m_result.program.algebraId = options.algebraId;
        m_result.program.gamma = options.gamma;
        m_result.program.languageVersion = options.languageVersion;
        m_diagnosticLimit = std::clamp<std::size_t>(options.maximumDiagnostics, 1, 512);
        m_depthLimit = std::min<std::size_t>(options.maximumNesting, 256);
    }

    AnalysisResult run() {
        try {
            cancelled();
            if (m_source.size() > m_options.maximumSourceBytes)
                resource("Source exceeds the configured byte limit.", {0, std::min<std::size_t>(1, m_source.size())});
            if (m_options.algebraId != "A_gamma_v1") add("E1502", "Unsupported algebra identifier/version.", {});
            if (!std::isfinite(m_options.gamma) || m_options.gamma < 0 || m_options.gamma > 1)
                add("E1503", "Gamma must be finite and lie in [0,1].", {});
            if (m_options.languageVersion != "0.1" && m_options.languageVersion != "0.2")
                add("E1601", "Unsupported language version; use 0.1 or 0.2.", {});
            seedSymbols();
            tokenize();
            while (!atEnd()) {
                cancelled();
                if (match(";")) continue;
                if (m_result.program.statements.size() >= m_options.maximumStatements)
                    resource("Statement count exceeds the configured limit.", current().span);
                const std::size_t before = m_index;
                try {
                    m_result.program.statements.push_back(statement());
                } catch (const ParseError&) {
                    synchronize();
                }
                m_seenNonempty = true;
                if (m_index == before && !atEnd()) ++m_index;
            }
        } catch (const StopAnalysis&) {}
        return std::move(m_result);
    }

private:
    struct Binding { Type type; SourceSpan span; std::shared_ptr<const OdeDefinition> ode{}; std::shared_ptr<const FunctionDefinition> function{}; std::shared_ptr<const ValueSchema> schema{}; };
    std::string_view m_source;
    const AnalysisOptions& m_options;
    AnalysisResult m_result;
    std::vector<Token> m_tokens;
    std::unordered_map<std::string, Binding> m_bindings;
    std::unordered_map<const Expr*, std::size_t> m_depths;
    std::size_t m_index = 0;
    std::size_t m_nodes = 0;
    std::size_t m_recursion = 0;
    std::size_t m_errors = 0;
    std::size_t m_diagnosticLimit = 128;
    std::size_t m_depthLimit = 128;
    bool m_seenNonempty = false;
    bool m_seenDirective = false;
    std::size_t m_blockDepth=0,m_loopDepth=0,m_functionDepth=0,m_indexDepth=0,m_statementCount=0;
    Type m_returnType=Type::Invalid;
    std::vector<Type> m_returnTypes;
    std::shared_ptr<const ValueSchema> m_lastDeclaredSchema,m_returnSchema;
    std::shared_ptr<const SourceOrigin> m_sourceOrigin;
    std::shared_ptr<const SourceOrigin> sourceOrigin(){if(!m_sourceOrigin){auto origin=std::make_shared<SourceOrigin>();origin->identity=m_options.sourceIdentity.empty()?"<source>":m_options.sourceIdentity;origin->source=std::make_shared<const std::string>(m_source);m_sourceOrigin=origin;}return m_sourceOrigin;}
    std::unordered_set<std::string> m_importedIdentities;
    std::shared_ptr<ImportBudget> m_importBudget=m_options.importBudget?m_options.importBudget:std::make_shared<ImportBudget>();
#include "iteration_frontend.inc"
#include "foundation_frontend.inc"
#include "universal_frontend.inc"
#include "developer_tools_frontend.inc"
#include "universal_array_frontend.inc"
#include "tensor_frontend.inc"
#include "vector_threading_frontend.inc"
#include "linalg_frontend.inc"
#include "derivative_frontend.inc"
#include "third_derivative_frontend.inc"
#include "chart_geometry_frontend.inc"
#include "sensitivity_frontend.inc"
#include "model_sensitivity_frontend.inc"
#include "dae_frontend.inc"
#include "mechanics_frontend.inc"
#include "constrained_mechanics_frontend.inc"
#include "quantity_frontend.inc"
#include "nd_frontend.inc"
#include "derivative_model_frontend.inc"
#include "roots_frontend.inc"
#include "advanced_ode_frontend.inc"
#include "fitting_frontend.inc"
#include "inference_frontend.inc"
#include "optimization_frontend.inc"
#include "maps_frontend.inc"
#include "pde_frontend.inc"
#include "pde_ensemble_frontend.inc"
#include "geometry_frontend.inc"
#include "extra_geometry_frontend.inc"
#include "fem_frontend.inc"
#include "bifurcation_frontend.inc"
#include "experiment_bundle_frontend.inc"
#include "chaos_frontend.inc"
#include "publication_frontend.inc"
#include "animation_frontend.inc"
#include "video_frontend.inc"
#include "discovery_frontend.inc"
#include "hybrid_ode_frontend.inc"
#include "verification_frontend.inc"
#include "provenance_frontend.inc"
#include "orbits_frontend.inc"
#include "graph_view_frontend.inc"
#include "graph_frontend.inc"
#include "graph_tools_frontend.inc"
#include "network_frontend.inc"
#include "science_data_frontend.inc"
#include "pent_symbolic_frontend.inc"
#include "evolution_frontend.inc"

    void seedSymbols() {
        if (m_options.initialSymbols.size() > m_options.maximumNodes)
            resource("Initial workspace exceeds the configured symbol budget.", {});
        for (const auto& symbol : m_options.initialSymbols) {
            cancelled();
            if (!numericType(symbol.type) && !matrixType(symbol.type) && !rangeType(symbol.type) && symbol.type != Type::Solution && symbol.type != Type::OdeModel &&
                symbol.type != Type::RealSeries && symbol.type != Type::PentarnionSeries && symbol.type!=Type::ElementSeries && symbol.type!=Type::Algebra && symbol.type!=Type::Element && symbol.type!=Type::String && symbol.type!=Type::Function && symbol.type!=Type::Complex && symbol.type!=Type::Record && symbol.type!=Type::Tuple && symbol.type!=Type::ElementMatrix && symbol.type!=Type::ArraySeries && symbol.type!=Type::Tensor) {
                add("E1204", "A workspace symbol has an unsupported declared type.", {});
                continue;
            }
            if(symbol.type==Type::Tensor&&(!symbol.schema||!coordinateWidth(Type::Tensor,symbol.schema))){add("E2550","A tensor workspace symbol requires its complete fixed shape and entry descriptor.",{});continue;}
            const auto tokens = lex(symbol.name);
            const bool legacyDisplayName = additiveName(symbol.name) && m_options.languageVersion == "0.1";
            if (tokens.tokens.size() != 2 ||
                (tokens.tokens.front().kind != TokenKind::Identifier && !legacyDisplayName && symbol.name!="element" && symbol.name!="algebra" && symbol.name!="tensor") ||
                tokens.tokens.front().span.offset != 0 || tokens.tokens.front().span.length != symbol.name.size()) {
                add("E1203", "A workspace symbol has an invalid or protected identifier.", {});
                continue;
            }
            if (!m_bindings.emplace(symbol.name, Binding{symbol.type, symbol.declarationSpan, symbol.ode,symbol.function,symbol.schema}).second)
                add("E1202", "The initial workspace contains a duplicate identifier.", {});
            else m_result.symbols.push_back(symbol);
        }
    }

    void cancelled() {
        if (m_options.cancellation && m_options.cancellation->load(std::memory_order_relaxed)) {
            m_result.cancelled = true;
            throw StopAnalysis{};
        }
    }
    void add(std::string_view code, std::string message, SourceSpan span, Severity severity = Severity::Error) {
        if (severity == Severity::Error) ++m_errors;
        Diagnostic item{severity, std::string(code), std::move(message), span};
        if (m_result.diagnostics.size() < m_diagnosticLimit) m_result.diagnostics.push_back(std::move(item));
        else if (severity == Severity::Error) {
            const auto warning = std::find_if(m_result.diagnostics.rbegin(), m_result.diagnostics.rend(),
                [](const auto& old) { return old.severity == Severity::Warning; });
            if (warning != m_result.diagnostics.rend()) *warning = std::move(item);
            throw StopAnalysis{};
        }
    }
    [[noreturn]] void resource(std::string message, SourceSpan span) {
        add("E1103", std::move(message), span);
        throw StopAnalysis{};
    }
    [[noreturn]] void syntax(std::string_view code, std::string message, SourceSpan span) {
        add(code, std::move(message), span);
        throw ParseError{};
    }
    void tokenize() {
        LexerState state;
        for (std::size_t at = 0; at < m_source.size();) {
            cancelled();
            const std::size_t next = m_source.find('\n', at);
            const std::size_t end = next == std::string_view::npos ? m_source.size() : next + 1;
            auto line = lexLine(m_source.substr(at, end - at), state, at);
            state = line.state;
            for (auto& diagnostic : line.diagnostics)
                add(diagnostic.code, std::move(diagnostic.message), diagnostic.span);
            for (const auto& token : line.tokens) {
                if (token.kind == TokenKind::Comment) continue;
                if (m_tokens.size() >= m_options.maximumTokens)
                    resource("Token count exceeds the configured limit.", token.span);
                m_tokens.push_back(token);
            }
            at = end;
        }
        if (state.inBlockComment) add("E1003", "Unterminated block comment.", {state.blockCommentStart, 2});
        m_tokens.push_back({TokenKind::EndOfFile, {m_source.size(), 0}});
    }
    const Token& current() const { return m_tokens[std::min(m_index, m_tokens.size() - 1)]; }
    const Token& peek(std::size_t count = 1) const { return m_tokens[std::min(m_index + count, m_tokens.size() - 1)]; }
    std::string_view text(const Token& token) const { return m_source.substr(token.span.offset, token.span.length); }
    bool atEnd() const { return current().kind == TokenKind::EndOfFile; }
    bool is(std::string_view spelling) const { return text(current()) == spelling; }
    Token advance() { const Token result = current(); if (!atEnd()) ++m_index; return result; }
    bool match(std::string_view spelling) { if (!is(spelling)) return false; advance(); return true; }
    Token expect(std::string_view spelling) {
        if (!is(spelling)) syntax("E1102", "Expected '" + std::string(spelling) + "'.", current().span);
        return advance();
    }
    static bool nameToken(const Token& token) {
        return token.kind == TokenKind::Identifier || token.kind == TokenKind::Basis ||
               token.kind == TokenKind::Builtin || token.kind == TokenKind::Keyword;
    }
    bool protectedName(const Token& token) const {
        // These additive type words are contextual, preserving existing0.2
        // programs such as `pentarnion element=P(3);`.
        if(text(token)=="element"||text(token)=="algebra"||text(token)=="tensor")return false;
        // This optional graph alias was already a valid array identifier in
        // 0.2 sources. Keep declarations/indexing compatible; graphcomponents
        // remains the unambiguous public spelling when the alias is shadowed.
        if(text(token)=="components")return false;
        // Version 0.2 adds this protected builtin; old 0.1 identifiers retain
        // their meaning rather than being silently reinterpreted on reopen.
        if (additiveName(text(token)))
            return m_options.languageVersion != "0.1";
        return token.kind == TokenKind::Basis || token.kind == TokenKind::Builtin ||
               token.kind == TokenKind::Keyword;
    }
    Token name() {
        if (!nameToken(current())) syntax("E1102", "Expected an identifier.", current().span);
        return advance();
    }
    SourceSpan terminate(SourceSpan beginning, SourceSpan last) {
        if (is(";")) return cover(beginning, advance().span);
        add("E1101", "Expected ';' after the statement.", current().span);
        return cover(beginning, last);
    }
    void synchronize() {
        while (!atEnd()) { cancelled(); if (match(";")) break; advance(); }
    }
    ExprPtr node(ExprKind kind, SourceSpan span, std::string spelling = {}, std::vector<ExprPtr> children = {}) {
        cancelled();
        if (++m_nodes > m_options.maximumNodes) resource("AST node count exceeds the configured limit.", span);
        std::size_t depth = 1;
        for (const auto& child : children) depth = std::max(depth, m_depths.at(child.get()) + 1);
        if (depth > m_depthLimit) resource("AST depth exceeds the configured nesting limit.", span);
        auto result = std::make_shared<Expr>();
        result->kind = kind;
        result->span = span;
        result->text = std::move(spelling);
        result->children = std::move(children);
        m_depths[result.get()] = depth;
        return result;
    }
    bool gapBefore(const Token& token) const {
        return m_index>0 && token.span.offset>m_tokens[m_index-1].span.offset+m_tokens[m_index-1].span.length;
    }
    bool newlineBefore(const Token& token) const {
        if(!gapBefore(token))return false;
        const auto previous=m_tokens[m_index-1].span;
        return m_source.substr(previous.offset+previous.length,token.span.offset-previous.offset-previous.length).find_first_of("\r\n")!=std::string_view::npos;
    }
    ExprPtr expression(bool matrixEntry = false) { return logicalOr(matrixEntry); }
    ExprPtr logicalOr(bool matrixEntry) {
        auto left=logicalAnd(matrixEntry);
        while(is("||")||is("|")){auto op=advance();auto right=logicalAnd(matrixEntry);left=node(ExprKind::Binary,cover(left->span,right->span),std::string(text(op)),{left,right});}
        return left;
    }
    ExprPtr logicalAnd(bool matrixEntry) {
        auto left=comparison(matrixEntry);
        while(is("&&")||is("&")){auto op=advance();auto right=comparison(matrixEntry);left=node(ExprKind::Binary,cover(left->span,right->span),std::string(text(op)),{left,right});}
        return left;
    }
    ExprPtr comparison(bool matrixEntry) {
        auto left=rangeExpression(matrixEntry);
        while(is("==")||is("!=")||is("~=")||is("<")||is(">")||is("<=")||is(">=")) {
            auto op=advance();auto right=rangeExpression(matrixEntry);left=node(ExprKind::Binary,cover(left->span,right->span),std::string(text(op)),{left,right});
        }
        return left;
    }
    ExprPtr rangeExpression(bool matrixEntry) {
        auto first=addition(matrixEntry);
        if(!match(":"))return first;
        auto second=addition(matrixEntry);
        ExprPtr step,stop;
        if(match(":")){step=second;stop=addition(matrixEntry);}
        else{step=node(ExprKind::IntegerLiteral,first->span,"1");stop=second;}
        return node(ExprKind::Range,cover(first->span,stop->span),{}, {first,step,stop});
    }
    ExprPtr addition(bool matrixEntry = false) {
        auto left = product();
        while (is("+") || is("-")) {
            // MATLAB-style [1 -2] has two entries; [1 - 2] has one expression.
            if(matrixEntry && (newlineBefore(current()) || (gapBefore(current()) &&
                current().span.offset+current().span.length==peek().span.offset)))break;
            const auto op = advance();
            auto right = product();
            left = node(ExprKind::Binary, cover(left->span, right->span), std::string(text(op)), {left, right});
        }
        return left;
    }
    ExprPtr product() {
        auto left = unary();
        while (multiplicative(text(current()))) {
            const auto op = advance();
            auto right = unary();
            left = node(ExprKind::Binary, cover(left->span, right->span), std::string(text(op)), {left, right});
        }
        return left;
    }
    ExprPtr unary() {
        cancelled();
        if (m_recursion >= m_depthLimit) resource("Expression nesting exceeds the configured limit.", current().span);
        ++m_recursion;
        struct Guard { std::size_t& value; ~Guard() { --value; } } guard{m_recursion};
        if (is("+") || is("-") || is("!") || is("~")) {
            const auto op = advance();
            auto child = unary();
            return node(ExprKind::Unary, cover(op.span, child->span), std::string(text(op)), {child});
        }
        auto left = postfix();
        if (is("^") || is(".^")) {
            const auto op=advance();
            auto right = unary();
            left = node(ExprKind::Binary, cover(left->span, right->span), std::string(text(op)), {left, right});
        }
        return left;
    }
    ExprPtr postfix() {
        auto value = primary();
        while(true) {
            if(is("'") || is(".'")) {
                const auto op=advance();
                value=node(ExprKind::Transpose,cover(value->span,op.span),std::string(text(op)),{value});
            } else if(match(".")) {
                const auto member = name();
                value = node(ExprKind::Member, cover(value->span, member.span), std::string(text(member)), {value});
            } else if(match("(")) {
                std::vector<ExprPtr> arguments{value};std::vector<std::string> names;
                if(!is(")"))do{std::string key;if(nameToken(current())&&text(peek())=="="){key=std::string(text(advance()));expect("=");}names.push_back(key);arguments.push_back(indexArgument());}while(match(","));
                const auto close=expect(")");
                value=node(ExprKind::Index,cover(value->span,close.span),{},std::move(arguments));value->argumentNames=std::move(names);
            } else break;
        }
        return value;
    }
    ExprPtr matrixLiteral() {
        const auto beginning=expect("[");
        if(m_options.languageVersion!="0.2")add("E1601","Matrix literals require language 0.2.",beginning.span);
        std::vector<ExprPtr> values;
        std::size_t rows=0,columns=0,rowWidth=0;
        std::vector<std::size_t> rowWidths;
        if(match("]"))return node(ExprKind::MatrixLiteral,cover(beginning.span,m_tokens[m_index-1].span),{},{});
        while(!atEnd()) {
            values.push_back(expression(true));++rowWidth;
            if(is("]") || is(";") || newlineBefore(current())) {
                if(!columns)columns=rowWidth;
                rowWidths.push_back(rowWidth);
                ++rows;rowWidth=0;
                if(match("]"))break;
                if(match(";")) {
                    if(is("]"))syntax("E1901","A matrix literal cannot end with an empty row.",current().span);
                }
                continue;
            }
            if(match(","))continue;
            if(!gapBefore(current()))syntax("E1102","Separate matrix entries with commas or whitespace.",current().span);
        }
        if(atEnd() && text(m_tokens[m_index-1])!="]")expect("]");
        auto value=node(ExprKind::MatrixLiteral,cover(beginning.span,m_tokens[m_index-1].span),{},std::move(values));
        value->rows=rows;value->columns=columns;value->rowWidths=std::move(rowWidths);return value;
    }
    ExprPtr indexArgument() {
        ++m_indexDepth;
        struct Guard{std::size_t& value;~Guard(){--value;}} guard{m_indexDepth};
        if(is(":")){const auto token=advance();return node(ExprKind::AllIndices,token.span);}
        return expression();
    }
    ExprPtr primary() {
        const auto beginning = current();
        if(m_options.languageVersion=="0.2" && (is("true") || is("false"))){advance();return node(ExprKind::IntegerLiteral,beginning.span,text(beginning)=="true"?"1":"0");}
        if(m_options.languageVersion=="0.2" && is("@"))return functionHandle();
        if(is("end") && m_indexDepth){advance();return node(ExprKind::EndIndex,beginning.span);}
        if (match("(")) {
            auto inside = expression();
            if(match(",")){std::vector<ExprPtr> values{inside};do{values.push_back(expression());}while(match(","));const auto close=expect(")");return node(ExprKind::TupleLiteral,cover(beginning.span,close.span),{},std::move(values));}
            const auto close = expect(")");
            return node(ExprKind::Group, cover(beginning.span, close.span), {}, {inside});
        }
        if (beginning.kind == TokenKind::Integer || beginning.kind == TokenKind::Real) {
            advance();
            return node(beginning.kind == TokenKind::Integer ? ExprKind::IntegerLiteral : ExprKind::RealLiteral,
                        beginning.span, std::string(text(beginning)));
        }
        if (beginning.kind == TokenKind::String) {
            advance();
            return node(ExprKind::StringLiteral, beginning.span, decoded(text(beginning)));
        }
        if (is("[")) return matrixLiteral();
        if (nameToken(beginning)) {
            advance();
            const std::string callee(text(beginning));
            if (!match("(")) return node(ExprKind::Reference, beginning.span, callee);
            if (callee == "odesolve") return odeSolve(beginning);
            if(const auto found=m_bindings.find(callee);found!=m_bindings.end() && (matrixType(found->second.type) || found->second.type==Type::Tensor || found->second.type==Type::ElementMatrix || found->second.type==Type::ArraySeries || found->second.type==Type::RealSeries || found->second.type==Type::PentarnionSeries || found->second.type==Type::ElementSeries)) {
                std::vector<ExprPtr> indices{node(ExprKind::Reference,beginning.span,callee)};
                if(!is(")"))do{indices.push_back(indexArgument());}while(match(","));
                const auto close=expect(")");
                return node(ExprKind::Index,cover(beginning.span,close.span),{},std::move(indices));
            }
            const bool constructor = callee == "pent" && match("[");
            std::vector<ExprPtr> arguments;
            const std::string_view closing = constructor ? "]" : ")";
            std::vector<std::string> argumentNames;
            if (!is(closing)) {
                do {std::string key;if(nameToken(current())&&text(peek())=="="){key=std::string(text(advance()));expect("=");}argumentNames.push_back(key);arguments.push_back(expression());} while (match(","));
            }
            expect(closing);
            const auto close = constructor ? expect(")") : m_tokens[m_index - 1];
            auto result=node(constructor ? ExprKind::PentConstructor : ExprKind::Call,
                        cover(beginning.span, close.span), callee, std::move(arguments));result->argumentNames=std::move(argumentNames);return result;
        }
        syntax("E1102", "Expected an expression or operand.", beginning.span);
    }
    ExprPtr odeSolve(const Token& beginning) {
        const auto model = name();
        std::vector<ExprPtr> values;
        std::vector<std::string> names;
        while (match(",")) {
            const auto key = name();
            expect("=");
            names.emplace_back(text(key));
            values.push_back(expression());
        }
        const auto close = expect(")");
        auto result = node(ExprKind::OdeSolve, cover(beginning.span, close.span), std::string(text(model)), std::move(values));
        result->argumentNames = std::move(names);
        return result;
    }
    ExprPtr initializer() { return expression(); }

    Type declaredTypeName() {
        m_lastDeclaredSchema.reset();
        const auto token=advance();const auto name=text(token);
        if(name=="algebra")return Type::Algebra;
        if(name=="tensor")return tensorDeclaredType();
        if(name=="element"){
            if(match("<")){
                const auto definition=this->name();const auto found=m_bindings.find(std::string(text(definition)));
                if(found==m_bindings.end()||found->second.type!=Type::Algebra||!found->second.schema||!found->second.schema->algebra)add("E2530","An element type requires a previously declared immutable algebra definition.",definition.span);
                else m_lastDeclaredSchema=found->second.schema;
                expect(">");
            }
            return Type::Element;
        }
        if(name=="integer")return Type::Integer;
        if(name=="real")return Type::Real;
        if(name=="pentarnion")return Type::Pentarnion;
        if(name=="string")return Type::String;
        if(name=="matrix"||name=="realmatrix"||name=="vector"||name=="pentarnionmatrix"){
            Type type=name=="pentarnionmatrix"?Type::PentarnionMatrix:Type::RealMatrix;
            if(match("<")){
                auto schema=std::make_shared<ValueSchema>();
                if(current().kind!=TokenKind::Integer){
                    const auto definition=this->name();const auto found=m_bindings.find(std::string(text(definition)));
                    if(name!="matrix"&&name!="vector")add("E2530","An algebra-qualified array uses matrix<A,rows,columns>.",definition.span);
                    if(found==m_bindings.end()||found->second.type!=Type::Algebra||!found->second.schema||!found->second.schema->algebra)add("E2530","An array algebra qualifier requires a previously declared immutable definition.",definition.span);
                    else schema->algebra=found->second.schema->algebra;
                    type=Type::ElementMatrix;expect(",");
                }
                for(unsigned dimension=0;dimension<2;++dimension){
                    const auto size=current();if(size.kind!=TokenKind::Integer)syntax("E2530","Fixed array dimensions require positive integer literals.",size.span);
                    advance();const auto number=unsignedInteger(text(size));if(!number||!*number||*number>100000)add("E2530","Fixed array dimensions must lie in1..100000.",size.span);
                    schema->shape.push_back(number&&*number<=100000?static_cast<std::size_t>(*number):0);if(!dimension)expect(",");
                }
                expect(">");if(schema->shape[0]&&schema->shape[1]&&schema->shape[0]>1000000/schema->shape[1])add("E2530","A fixed array exceeds the million-cell shape allowance.",token.span);
                if(name=="vector"&&schema->shape[0]>1&&schema->shape[1]>1)add("E2530","A fixed vector requires one row or one column.",token.span);
                m_lastDeclaredSchema=schema;
            }
            return type;
        }
        if(name=="void")return Type::Void;
        if(name=="callback")return Type::Function;
        if(name=="options"||name=="record")return Type::Record;
        if(name=="tuple")return Type::Tuple;
        if(name=="complex")return Type::Complex;
        if(name=="logicalmatrix")return Type::LogicalMatrix;
        syntax("E1204","Expected a scalar, string, matrix or void type.",token.span);
    }
    std::vector<Statement> block() {
        const auto opening=expect("{");
        if(++m_blockDepth>m_depthLimit)resource("Block nesting exceeds the configured limit.",opening.span);
        auto saved=m_bindings;auto symbols=m_result.symbols;
        struct Guard { Frontend& owner;decltype(m_bindings) bindings;decltype(m_result.symbols) symbols;
            ~Guard(){owner.m_bindings=std::move(bindings);owner.m_result.symbols=std::move(symbols);--owner.m_blockDepth;}
        } guard{*this,std::move(saved),std::move(symbols)};
        std::vector<Statement> statements;
        while(!is("}")&&!atEnd()){cancelled();if(match(";"))continue;statements.push_back(statement());}
        expect("}");return statements;
    }
    void checkCondition(const ExprPtr& condition) {
        const auto type=check(condition);
        if(type!=Type::Invalid&&!scalarType(type))add("E1204","A condition must be a real scalar; use norm2(p) for a Pentarnion.",condition->span);
    }
    Statement conditional() {
        const auto beginning=advance();const bool loop=text(beginning)=="while";
        expect("(");auto condition=expression();expect(")");checkCondition(condition);
        if(loop)++m_loopDepth;
        struct LoopGuard{std::size_t& depth;bool active;~LoopGuard(){if(active)--depth;}} guard{m_loopDepth,loop};
        Statement result;result.kind=loop?StatementKind::While:StatementKind::If;result.expression=condition;
        result.body=block();
        if(!loop&&match("else")) {
            if(is("if"))result.alternative.push_back(conditional());
            else result.alternative=block();
        }
        result.span=cover(beginning.span,m_tokens[m_index-1].span);match(";");return result;
    }
    Statement forStatement() {
        const auto beginning=advance();expect("(");const auto iterator=name();
        if(protectedName(iterator))add("E1203","A loop iterator cannot be a keyword, basis or builtin.",iterator.span);
        expect("=");auto values=expression();expect(")");const auto type=check(values);
        Type iteratorType=type==Type::IntegerRange?Type::Integer:Type::Real;
        if(type!=Type::Invalid && !rangeType(type)&&type!=Type::RealMatrix)
            add("E1204","A for loop requires a real range or row/column vector.",values->span);
        const auto old=m_bindings;const auto symbols=m_result.symbols;
        m_bindings.insert_or_assign(std::string(text(iterator)),Binding{iteratorType,iterator.span});
        ++m_loopDepth;
        struct Guard{Frontend& owner;decltype(m_bindings) bindings;decltype(m_result.symbols) symbols;
            ~Guard(){owner.m_bindings=std::move(bindings);owner.m_result.symbols=std::move(symbols);--owner.m_loopDepth;}
        } guard{*this,old,symbols};
        Statement result;result.kind=StatementKind::For;result.name=std::string(text(iterator));result.nameSpan=iterator.span;
        result.expression=values;result.declaredType=iteratorType;result.body=block();
        result.span=cover(beginning.span,m_tokens[m_index-1].span);match(";");return result;
    }
    Statement flowStatement() {
        const auto beginning=advance();Statement result;
        if(text(beginning)=="return") {
            result.kind=StatementKind::Return;
            if(!m_functionDepth)add("E1601","return is only valid inside a user function.",beginning.span);
            if(!is(";")) {
                result.expression=expression();
                if(match(",")){std::vector<ExprPtr> outputs{result.expression};do{outputs.push_back(expression());}while(match(","));const auto span=cover(outputs.front()->span,outputs.back()->span);result.expression=node(ExprKind::TupleLiteral,span,{},std::move(outputs));}
                if(result.expression->kind==ExprKind::MatrixLiteral&&m_returnSchema&&!m_returnSchema->shape.empty())result.expression->schema=m_returnSchema;
                const auto actual=check(result.expression,true,false,false,m_returnType);
                if(m_returnType==Type::Tuple && result.expression->schema){if(result.expression->schema->elements.size()!=m_returnTypes.size())add("E1204","Incorrect number of returned values.",result.expression->span);else for(std::size_t n=0;n<m_returnTypes.size();++n)if(!typeAssignable(result.expression->schema->elements[n].type,m_returnTypes[n]))add("E1204","Returned tuple item has an incompatible type.",result.expression->span);}
                if(m_returnType==Type::Element&&actual==Type::Element&&!compatibleSchema(m_returnSchema,result.expression->schema))add("E2530","Return value has a different algebra definition.",result.expression->span);
                if(m_returnType==Type::Tuple&&m_returnSchema&&result.expression->schema){
                    for(std::size_t n=0;n<m_returnSchema->elements.size()&&n<result.expression->schema->elements.size();++n){const auto& expected=m_returnSchema->elements[n];const auto& actualItem=result.expression->schema->elements[n];if(!(expected.type==Type::Element&&scalarType(actualItem.type))&&!compatibleSchema(expected.schema,actualItem.schema))add("E2530","Returned tuple item belongs to a different algebra definition.",result.expression->span);}
                }
                if(m_returnType==Type::Void || !assignable(result.expression,actual,m_returnType))
                    add("E1204","Return value does not match the function result type.",result.expression->span);
            }else if(m_returnType!=Type::Void&&m_returnType!=Type::Invalid)
                add("E1204","This function must return a value.",beginning.span);
        }else {
            result.kind=text(beginning)=="break"?StatementKind::Break:StatementKind::Continue;
            if(!m_loopDepth)add("E1601","break/continue require an enclosing loop.",beginning.span);
        }
        result.span=terminate(beginning.span,result.expression?result.expression->span:beginning.span);return result;
    }
    Statement functionDefinition() {
        const auto beginning=advance();
        if(m_blockDepth||m_functionDepth)add("E1601","User functions must be defined at file/command scope.",beginning.span);
        auto function=std::make_shared<FunctionDefinition>();
        function->origin=sourceOrigin();
        if(match("[")){
            auto schema=std::make_shared<ValueSchema>();
            do{const auto type=declaredTypeName();const auto descriptor=m_lastDeclaredSchema;
                if(type==Type::Void)add("E1204","A tuple result cannot be void.",current().span);
                std::string outputName;if(!is(",")&&!is("]"))outputName=std::string(text(name()));
                function->results.push_back(type);schema->elements.push_back({outputName,type,descriptor});
            }while(match(","));expect("]");
            if(function->results.empty()||function->results.size()>16)syntax("E1204","Use 1..16 function results.",beginning.span);
            function->result=Type::Tuple;function->resultSchema=schema;
        }else{function->result=declaredTypeName();function->resultSchema=m_lastDeclaredSchema;}
        const auto identifier=name();function->name=std::string(text(identifier));
        if(protectedName(identifier))add("E1203","A user function cannot have a protected name.",identifier.span);
        if(m_bindings.contains(function->name))add("E1202","This function name is already declared.",identifier.span);
        expect("(");std::unordered_set<std::string> parameterNames;
        if(!is(")"))do{
            const auto type=declaredTypeName();const auto declaredSchema=m_lastDeclaredSchema;const auto parameter=name();const std::string nameText(text(parameter));
            if(type==Type::Void)add("E1204","A function parameter cannot have void type.",parameter.span);
            if(protectedName(parameter)||!parameterNames.insert(nameText).second||nameText==function->name)
                add("E1203","A parameter needs a distinct, unprotected identifier.",parameter.span);
            function->parameters.push_back({nameText,type,parameter.span,declaredSchema});
            ExprPtr defaultValue;if(match("=")){defaultValue=expression();const auto actual=check(defaultValue,true,false,false,type);if(!assignable(defaultValue,actual,type)||!constantDefault(defaultValue))add("E1204","Parameter defaults must be compatible constant expressions.",defaultValue->span);}
            function->parameterDefaults.push_back(defaultValue);function->parameterSchemas.push_back(declaredSchema?declaredSchema:(defaultValue?defaultValue->schema:nullptr));function->parameters.back().schema=function->parameterSchemas.back();
        }while(match(","));expect(")");
        m_bindings.insert_or_assign(function->name,Binding{Type::Function,identifier.span,{},function});
        const auto outer=m_bindings;const auto symbols=m_result.symbols;
        const auto oldReturn=m_returnType;const auto oldReturns=m_returnTypes;const auto oldReturnSchema=m_returnSchema;const auto oldLoop=m_loopDepth;
        m_bindings.clear();for(const auto& [name,binding]:outer)if(binding.type==Type::Function||binding.type==Type::Algebra)m_bindings.emplace(name,binding);
        for(std::size_t n=0;n<function->parameters.size();++n){const auto& parameter=function->parameters[n];m_bindings.insert_or_assign(parameter.name,Binding{parameter.type,parameter.span,{},function->parameterSchemas[n]?function->parameterSchemas[n]->function:nullptr,function->parameterSchemas[n]});}
        m_returnType=function->result;m_returnTypes=function->results;m_returnSchema=function->resultSchema;m_loopDepth=0;++m_functionDepth;
        struct Guard{Frontend& owner;decltype(m_bindings) bindings;decltype(m_result.symbols) symbols;Type result;std::vector<Type> returns;std::shared_ptr<const ValueSchema> returnSchema;std::size_t loops;
            ~Guard(){owner.m_bindings=std::move(bindings);owner.m_result.symbols=std::move(symbols);owner.m_returnType=result;owner.m_returnTypes=returns;owner.m_returnSchema=std::move(returnSchema);owner.m_loopDepth=loops;--owner.m_functionDepth;}
        } guard{*this,outer,symbols,oldReturn,oldReturns,oldReturnSchema,oldLoop};
        function->body=block();
        bool seenResultSchema=function->resultSchema&&((function->result==Type::Element&&function->resultSchema->algebra)||!function->resultSchema->shape.empty());
        const auto declaredResultSchema=function->resultSchema;
        std::function<void(const std::vector<Statement>&)> results=[&](const auto& body){for(const auto& item:body){
            if(item.kind==StatementKind::Return&&item.expression&&item.expression->schema){
                if(!seenResultSchema){
                    if(function->result==Type::Tuple){auto shape=std::make_shared<ValueSchema>(*item.expression->schema);for(std::size_t n=0;n<shape->elements.size()&&n<function->results.size();++n){shape->elements[n].type=function->results[n];if(declaredResultSchema&&n<declaredResultSchema->elements.size()&&declaredResultSchema->elements[n].schema)shape->elements[n].schema=declaredResultSchema->elements[n].schema;}function->resultSchema=shape;}
                    else function->resultSchema=item.expression->schema;
                    seenResultSchema=true;
                }else if(!compatibleSchema(function->resultSchema,item.expression->schema))add("E1204","All returned structured values must share one field/callback schema.",item.expression->span);
            }
            results(item.body);results(item.alternative);
        }};results(function->body);
        function->span=cover(beginning.span,m_tokens[m_index-1].span);match(";");
        // The outer declaration is published by the statement parser after the
        // temporary function parameter scope is restored.
        guard.symbols.push_back({function->name,Type::Function,identifier.span,{},function});
        Statement result;result.kind=StatementKind::FunctionDefinition;result.name=function->name;result.nameSpan=identifier.span;
        result.function=function;result.span=function->span;return result;
    }
    Type checkUtility(const ExprPtr& value) {
        if(m_options.languageVersion!="0.2")add("E1601","This utility requires language 0.2.",value->span);
        const auto& name=value->text;const auto count=value->children.size();bool valid=true;
        auto require=[&](bool condition,const char* message){if(!condition){add("E1205",message,value->span);valid=false;}};
        auto string=[&](std::size_t n){if(n<count&&value->children[n]->type!=Type::String&&value->children[n]->type!=Type::Invalid){add("E1204","This argument must be a string.",value->children[n]->span);valid=false;}};
        Type result=Type::Void;
        if(name=="assert") {
            require(count==1||count==2,"Use assert(condition[,message]).");
            if(count&&!scalarType(value->children[0]->type)&&value->children[0]->type!=Type::Invalid){add("E1204","assert requires a real scalar condition.",value->span);valid=false;}
            if(count==2)string(1);
        }else if(name=="error"||name=="warning") {require(count==1,"Use error(message) or warning(message).");string(0);}
        else if(name=="help"||name=="lookfor"){require(count<=1,"Use help topic, help(\"topic\"), or lookfor(\"words\").");if(count)string(0);}
        else if(name=="who"||name=="whos")require(count==0,"who/whos do not accept arguments.");
        else if(name=="save"||name=="load") {require(count>=1,"Use save(\"file\"[,\"name\",...]) or load(\"file\").");for(std::size_t n=0;n<count;++n)string(n);}
        else if(name=="readmatrix"||name=="csvread"||name=="xlsread") {
            require(count==1||count==2,"Use readmatrix(\"file\"[,\"sheet\"]).");string(0);if(count==2)string(1);result=Type::RealMatrix;
        }else {
            require(count==2||count==3,"Use writematrix(A,\"file\"[,\"sheet\"]) or csvwrite/xlswrite(\"file\",A[,\"sheet\"]).");
            const std::size_t data=name=="writematrix"?0:1,path=1-data;
            string(path);if(count==3)string(2);
            if(data<count){const auto type=value->children[data]->type;if(!numericType(type)&&!matrixType(type)&&!rangeType(type)&&type!=Type::Invalid){add("E1204","Writing a matrix requires numeric data.",value->span);valid=false;}}
        }
        for(const auto& child:value->children)if(child->type==Type::Invalid)valid=false;
        return valid?result:Type::Invalid;
    }

    Statement statement() {
        const auto beginning = current();
        if(++m_statementCount>m_options.maximumStatements)resource("Statement count exceeds the configured limit.",beginning.span);
        if(m_options.languageVersion=="0.2") {
            if(is("tensor")&&(text(peek())=="<"||(nameToken(peek())&&text(peek(2))=="=")))return tensorDeclaration();
            if((is("algebra")||is("element"))&&(text(peek())=="<"||(nameToken(peek())&&text(peek(2))=="=")))return universalDeclaration();
            if(arrayDeclaration(text(beginning))&&text(peek())=="<")return fixedArrayDeclaration();
            if(is("if")||is("while"))return conditional();
            if(is("for"))return forStatement();
            if(is("native")){advance();expect("function");--m_index;auto value=functionDefinition();value.function->native=true;checkNativeDefinition(*value.function);return value;}
            if(is("function"))return functionDefinition();
            if(is("import"))return importStatement();
            if(is("[") && destructureAhead())return destructureStatement();
            if(is("break")||is("continue")||is("return"))return flowStatement();
            if((is("who")||is("whos")||is("help")||is("lookfor"))&&text(peek())!="(") {
                advance();auto value=node(ExprKind::Call,beginning.span,std::string(text(beginning)));
                if((value->text=="help"||value->text=="lookfor")&&!is(";")&&!atEnd()){
                    const auto topic=advance();
                    value->children.push_back(node(ExprKind::StringLiteral,topic.span,
                        topic.kind==TokenKind::String?decoded(text(topic)):std::string(text(topic))));
                }
                check(value);
                Statement result;result.kind=StatementKind::Expression;result.expression=value;result.span=terminate(beginning.span,beginning.span);return result;
            }
        }
        if (nameToken(beginning) && text(peek()) == "=") return assignment();
        if (is("use_algebra")) {if(m_blockDepth)add("E1601","Algebra directives are only allowed at file scope.",beginning.span);return directive();}
        if (m_options.languageVersion=="0.2"&&(is("callback")||is("options")||is("record")||is("tuple")||is("complex")||is("logicalmatrix"))&&nameToken(peek())&&text(peek(2))=="=")return foundationDeclaration();
        if (is("integer") || is("real") || is("pentarnion") || (is("string") && m_options.languageVersion=="0.2") ||
            (arrayDeclaration(text(beginning)) && (m_options.languageVersion!="0.1" || (nameToken(peek()) && text(peek(2))=="="))) ||
            (is("solution") && (m_options.languageVersion != "0.1" || (nameToken(peek()) && text(peek(2))=="=")))) return declaration();
        if (is("ode") && !(m_options.languageVersion == "0.1" && m_bindings.contains("ode"))) return odeDefinition();
        if (is("display") && text(peek()) == "(") return display();
        if ((is("tic") || (is("toc") && text(peek())!="(")) &&
            (m_options.languageVersion!="0.1" || text(peek())=="(" || !m_bindings.contains(std::string(text(beginning))))) return timerCommand();
        if(is("clear") && m_blockDepth)add("E1601","clear is only supported at file/command scope; block and function locals are cleaned up automatically.",beginning.span);
        if ((is("clear") || is("clc")) && (m_options.languageVersion != "0.1" ||
            text(peek()) == "(" || !m_bindings.contains(std::string(text(beginning)))))
            return is("clear") ? clearCommand() : clearConsole();
        if ((is("plot2") || is("plot3")) && text(peek()) == "(") return plot();
        if (is("plot") && text(peek()) == "(") return timePlot();
        if (is("subplot") && text(peek()) == "(") return subplot();
        if ((is("figure") || is("close")) && text(peek()) == "(") return figureCommand();
        if ((is("dock") || is("undock")) && text(peek()) == "(") return dockCommand();
        if (is("tilefigures") && text(peek()) == "(") return tileFigures();
        if ((is("title") || is("xlabel") || is("ylabel") || is("zlabel") || is("caption")) &&
            text(peek()) == "(") return figureText();
        if (is("clf") && (m_options.languageVersion != "0.1" || text(peek()) == "(" ||
                          !m_bindings.contains(std::string(text(beginning))))) return clearFigure();
        if (nameToken(beginning) && forbiddenConstruct(text(beginning)) &&
            m_bindings.find(std::string(text(beginning))) == m_bindings.end())
            syntax("E1601", "This language construct is not supported.", beginning.span);
        auto value = expression();
        if(is("=") && value->kind==ExprKind::Index && value->children.front()->kind==ExprKind::Reference) {
            advance();
            const auto targetType=check(value);
            auto rhs=initializer();const auto actual=check(rhs,false,false,false,targetType);
            const auto tensorBase=value->children.front()->schema;const bool tensorBroadcast=value->children.front()->type==Type::Tensor&&targetType==Type::Tensor&&tensorBase&&
                (scalarType(actual)||(actual==tensorBase->tensorEntryType&&(actual!=Type::Element||compatibleSchema(tensorSchema(Type::Real,{},tensorBase->algebra),rhs->schema))));
            if(actual!=Type::Invalid && targetType!=Type::Invalid && !assignable(rhs,actual,targetType) && !(matrixType(targetType) && numericType(actual))&&!tensorBroadcast)
                add("E1204","Indexed assignment requires a compatible scalar value.",rhs->span);
            if(value->children.front()->type==Type::Tensor&&actual==Type::Element&&targetType==Type::Element&&!compatibleSchema(value->schema,rhs->schema))add("E2550","Tensor entry assignment cannot change its immutable algebra definition.",rhs->span);
            Statement result;result.kind=StatementKind::IndexedAssignment;
            result.name=value->children.front()->text;result.nameSpan=value->children.front()->span;
            result.declaredType=value->children.front()->type;result.expression=rhs;
            result.arguments.assign(value->children.begin()+1,value->children.end());
            result.span=terminate(beginning.span,rhs->span);return result;
        }
        check(value);
        Statement result;
        result.kind = value->kind==ExprKind::Call && value->text=="toc" ? StatementKind::Toc : StatementKind::Expression;
        result.expression = value;
        result.span = terminate(beginning.span, value->span);
        return result;
    }
    Statement timerCommand() {
        const auto beginning=advance();figureProfile(beginning);
        SourceSpan last=beginning.span;
        if(is("(")) {
            auto [arguments,close]=commandArguments();last=close;
            if(!arguments.empty())add("E1205","tic/toc do not accept timer handles or arguments.",beginning.span);
            for(const auto& argument:arguments)check(argument);
        }
        Statement result;result.kind=text(beginning)=="tic"?StatementKind::Tic:StatementKind::Toc;
        result.name=std::string(text(beginning));result.nameSpan=beginning.span;result.span=terminate(beginning.span,last);
        return result;
    }
    void nativeExpression(const ExprPtr& value,std::size_t depth=0) {
        cancelled();
        if(depth>=128) resource("Native ODE expression nesting exceeds128 levels.",value->span);
        std::function<bool(const ExprPtr&)> containsTensor=[&](const ExprPtr& e){if(e->type==Type::Tensor)return true;return std::any_of(e->children.begin(),e->children.end(),containsTensor);};
        if(containsTensor(value)){FunctionDefinition synthetic;synthetic.result=value->type;synthetic.resultSchema=value->schema;synthetic.span=value->span;Statement result;result.kind=StatementKind::Return;result.expression=value;synthetic.body.push_back(result);checkNativeDefinition(synthetic);return;}
        for (const auto& child : value->children) nativeExpression(child,depth+1);
        bool allowed = value->kind == ExprKind::IntegerLiteral || value->kind == ExprKind::RealLiteral ||
            value->kind == ExprKind::Reference || value->kind == ExprKind::Group || (value->kind == ExprKind::Unary && (value->text=="+"||value->text=="-")) ||
            value->kind == ExprKind::PentConstructor;
        if(value->kind==ExprKind::Member&&value->children.size()==1&&value->children.front()->type==Type::Algebra&&value->schema&&value->schema->algebra)allowed=true;
        if(value->kind==ExprKind::MatrixLiteral){allowed=value->children.size()<=64;for(const auto& child:value->children)allowed=allowed&&scalarType(child->type);}
        if (value->kind == ExprKind::Binary) {
            if((value->text==".*" || value->text=="./" || value->text==".^") &&
               numericType(value->children[0]->type) && numericType(value->children[1]->type))
                value->text.erase(0,1); // Same ordered scalar operation in native code.
            allowed = value->text == "+" || value->text == "-" || value->text == "*" ||
                (value->text == "/" && scalarType(value->children[1]->type));
            if (value->text == "^") {
                const auto& exponent = value->children[1];
                const auto number = exponent->kind == ExprKind::IntegerLiteral ? unsignedInteger(exponent->text) : std::nullopt;
                allowed = number && *number <= 64;
            }
        }
        if (value->kind == ExprKind::Call) {
            constexpr std::array names{std::string_view("sin"),std::string_view("cos"),std::string_view("tan"),
                std::string_view("exp"),std::string_view("log"),std::string_view("sqrt"),std::string_view("sinh"),
                std::string_view("cosh"),std::string_view("tanh"),std::string_view("conj"),std::string_view("scalar"),
                std::string_view("pure"),std::string_view("coeff"),std::string_view("norm2"),
                std::string_view("commutator"),std::string_view("associator")};
            allowed = std::find(names.begin(),names.end(),value->text) != names.end()||value->text=="element"||value->text=="algebra_basis"||value->text=="complex"||value->text=="realpart"||value->text=="imagpart";
            if (elementaryFunction(value->text) && value->children.size() == 1)
                allowed = allowed && (scalarType(value->children[0]->type)||value->children[0]->type==Type::Element||value->children[0]->type==Type::Complex);
            if (value->text == "coeff" && value->children.size() == 2) {
                const auto& index = value->children[1];
                const auto number = index->kind == ExprKind::IntegerLiteral ? unsignedInteger(index->text) : std::nullopt;
                const auto width=coordinateWidth(value->children[0]->type,value->children[0]->schema);
                allowed = number && width && *number < width;
            }
        }
        if (!allowed) add("E1804", "This expression is outside the supported native ODE/Jacobian subset.",value->span);
    }
    Statement odeDefinition() {
        const auto beginning = advance();
        figureProfile(beginning);
        const auto identifier = name();
        const std::string modelName(text(identifier));
        if (protectedName(identifier)) add("E1203","An ODE name cannot be protected.",identifier.span);
        if (m_bindings.contains(modelName)) add("E1202","This identifier is already declared.",identifier.span);
        auto definition = std::make_shared<OdeDefinition>();
        definition->origin=sourceOrigin();
        definition->name = modelName;
        expect("(");
        const auto time = name();
        definition->timeName = std::string(text(time));
        if (protectedName(time)) add("E1203","The time name cannot be protected.",time.span);
        expect(",");
        std::unordered_set<std::string> localNames{definition->timeName};
        constexpr std::array options{std::string_view("t0"),std::string_view("t1"),std::string_view("sample_step"),
            std::string_view("method"),std::string_view("rtol"),std::string_view("atol"),std::string_view("jacobian"),
            std::string_view("max_steps"),std::string_view("max_samples"),std::string_view("max_seconds")};
        const auto variables = [&](std::vector<OdeVariable>& output) {
            do {
                const auto typeToken = current();
                const auto variableType=declaredTypeName();const auto variableSchema=m_lastDeclaredSchema;
                if(variableType!=Type::Real&&variableType!=Type::Pentarnion&&variableType!=Type::Complex&&variableType!=Type::Element&&variableType!=Type::RealMatrix&&variableType!=Type::PentarnionMatrix&&variableType!=Type::ElementMatrix&&variableType!=Type::Tensor)syntax("E1801","ODE states/parameters require real, complex, algebra elements or fixed arrays.",typeToken.span);
                if(!coordinateWidth(variableType,variableSchema))add("E2530","A native ODE element type requires an immutable algebra qualifier.",typeToken.span);
                const auto variable = name();
                const std::string variableName(text(variable));
                if (protectedName(variable)) add("E1203","ODE local names cannot be protected.",variable.span);
                if (!localNames.insert(variableName).second) add("E1202","Duplicate ODE local name.",variable.span);
                if (&output==&definition->states && variableName=="t") add("E1801","The solution member t is reserved for output times.",variable.span);
                if (std::find(options.begin(),options.end(),variableName) != options.end())
                    add("E1801","An ODE local name conflicts with a solver option.",variable.span);
                output.push_back({variableName,variableType,variable.span,variableSchema});
                if (output.size()>(&output==&definition->states?64U:128U)) resource("ODE variable count exceeds its native limit.",variable.span);
            } while (match(","));
        };
        variables(definition->states);
        if (match(";") && !is(")")) variables(definition->parameters);
        expect(")"); expect("{");
        auto saved = std::move(m_bindings);
        struct Restore { decltype(m_bindings)& target; decltype(m_bindings)& saved; ~Restore(){target=std::move(saved);} } restore{m_bindings,saved};
        m_bindings.clear();
        for(const auto& [bindingName,binding]:saved)if(binding.type==Type::Algebra)m_bindings.emplace(bindingName,binding);
        m_bindings.emplace(definition->timeName,Binding{Type::Real,time.span});
        std::size_t coordinates = 0;
        for (const auto& state : definition->states) {
            m_bindings.emplace(state.name,Binding{state.type,state.span,{},{},state.schema});
            coordinates += coordinateWidth(state.type,state.schema);
        }
        if (coordinates > 64) add("E1801","An ODE model supports at most 64 real state coordinates.",beginning.span);
        std::size_t parameterCoordinates=0;
        for (const auto& parameter : definition->parameters) {
            m_bindings.emplace(parameter.name,Binding{parameter.type,parameter.span,{},{},parameter.schema});
            parameterCoordinates += coordinateWidth(parameter.type,parameter.schema);
        }
        if (parameterCoordinates>128) add("E1801","An ODE model supports at most 128 real parameter coordinates.",beginning.span);
        definition->derivatives.resize(definition->states.size());
        const auto bodyNodes=m_nodes;
        while (!atEnd() && !is("}")) {
            cancelled();
            const auto state = name(); expect("'"); expect("=");
            auto derivative = expression();
            if (m_nodes-bodyNodes>20000) resource("Native ODE expression node limit exceeded.",derivative->span);
            expect(";");
            const auto found = std::find_if(definition->states.begin(),definition->states.end(),[&](const auto& candidate){return candidate.name==text(state);});
            const auto actual = check(derivative);
            nativeExpression(derivative);
            if (found == definition->states.end()) add("E1802","A derivative must target a declared ODE state.",state.span);
            else {
                const auto index = static_cast<std::size_t>(found-definition->states.begin());
                if (definition->derivatives[index]) add("E1802","A state derivative was supplied more than once.",state.span);
                else definition->derivatives[index] = derivative;
                if (actual != Type::Invalid && !assignable(derivative,actual,found->type))
                    add("E1204","The derivative does not match its state type.",derivative->span);
                if(((actual==Type::Element&&found->type==Type::Element)||(actual==Type::Tensor&&found->type==Type::Tensor))&&!compatibleSchema(found->schema,derivative->schema))add("E2530","The derivative uses a different algebra definition from its state.",derivative->span);
            }
        }
        const auto close = expect("}");
        match(";");
        for (std::size_t index=0;index<definition->states.size();++index)
            if (!definition->derivatives[index]) add("E1802","Missing derivative for state "+definition->states[index].name,definition->states[index].span);
        definition->span = cover(beginning.span,close.span);
        if (!protectedName(identifier) && !saved.contains(modelName)) {
            saved.emplace(modelName,Binding{Type::OdeModel,identifier.span,definition});
            m_result.symbols.push_back({modelName,Type::OdeModel,identifier.span,definition});
        }
        Statement result;
        result.kind=StatementKind::OdeDefinition; result.name=modelName; result.nameSpan=identifier.span;
        result.span=definition->span; result.ode=std::move(definition);
        return result;
    }
    Statement directive() {
        const auto beginning = advance();
        const bool positionValid = !m_seenDirective && !m_seenNonempty;
        if (!positionValid) add("E1501", "use_algebra must be the first nonempty statement and occur only once.", beginning.span);
        m_seenDirective = true;
        expect("(");
        if (current().kind != TokenKind::String) syntax("E1502", "Expected algebra alias string \"A_gamma\".", current().span);
        const auto alias = advance();
        const std::string algebra = decoded(text(alias));
        if (algebra != "A_gamma") add("E1502", "Only the algebra alias A_gamma is supported.", alias.span);
        expect(",");
        if (!is("gamma")) syntax("E1503", "Expected gamma=<signed numeric literal>.", current().span);
        advance(); expect("=");
        std::optional<Token> sign;
        if (is("+") || is("-")) sign = advance();
        const auto literal = current();
        if (literal.kind != TokenKind::Integer && literal.kind != TokenKind::Real)
            syntax("E1503", "Gamma must be a signed numeric literal.", literal.span);
        advance();
        auto gamma = node(literal.kind == TokenKind::Integer ? ExprKind::IntegerLiteral : ExprKind::RealLiteral,
                          literal.span, std::string(text(literal)));
        gamma->type = literal.kind == TokenKind::Integer ? Type::Integer : Type::Real;
        if (sign) {
            gamma = node(ExprKind::Unary, cover(sign->span, literal.span), std::string(text(*sign)), {gamma});
            gamma->type = gamma->children.front()->type;
        }
        const auto number = signedLiteral(gamma);
        const bool validGamma = number && *number >= 0 && *number <= 1 && std::isfinite(*number);
        if (!validGamma) add("E1503", "Gamma must be finite and lie in [0,1].", gamma->span);
        if (!is(")")) syntax("E1503", "Gamma must be one signed literal, not an expression.", current().span);
        const auto close = expect(")");
        Statement result;
        result.kind = StatementKind::Directive;
        result.name = algebra;
        result.nameSpan = alias.span;
        result.expression = gamma;
        result.span = terminate(beginning.span, close.span);
        if (positionValid && algebra == "A_gamma" && validGamma) m_result.program.gamma = *number;
        return result;
    }
    Statement fixedArrayDeclaration() {
        const auto beginning=current();const auto type=declaredTypeName();const auto schema=m_lastDeclaredSchema;
        const auto identifier=name();const std::string spelling(text(identifier));
        if(protectedName(identifier)||m_bindings.contains(spelling))add("E1203","A fixed array requires a new unprotected identifier.",identifier.span);
        expect("=");auto value=initializer();if(value->kind==ExprKind::MatrixLiteral)value->schema=schema;
        const auto actual=check(value,false,false,false,type);
        if(actual!=Type::Invalid&&!assignable(value,actual,type))add("E1204","Fixed array initializer has an incompatible element type.",value->span);
        if(actual!=Type::Invalid&&!compatibleSchema(schema,value->schema))add("E2530","Fixed array initializer differs from its declared shape or algebra.",value->span);
        m_bindings.emplace(spelling,Binding{type,identifier.span,{},{},schema});m_result.symbols.push_back({spelling,type,identifier.span,{},{},schema});
        Statement statement;statement.kind=StatementKind::Declaration;statement.name=spelling;statement.nameSpan=identifier.span;statement.declaredType=type;statement.declaredSchema=schema;statement.expression=value;
        statement.span=terminate(beginning.span,value->span);return statement;
    }
    Statement declaration() {
        const auto beginning = advance();
        Type declared = text(beginning) == "integer" ? Type::Integer :
                        text(beginning) == "real" ? Type::Real : text(beginning)=="string" ? Type::String : text(beginning) == "solution" ? Type::Solution :
                        text(beginning)=="pentarnionmatrix" ? Type::PentarnionMatrix :
                        arrayDeclaration(text(beginning)) ? Type::RealMatrix : Type::Pentarnion;
        const bool inferArray=text(beginning)=="matrix" || text(beginning)=="vector";
        if(arrayDeclaration(text(beginning)) && m_options.languageVersion!="0.2")add("E1601","Matrix declarations require language 0.2.",beginning.span);
        if(declared==Type::Solution && m_options.languageVersion!="0.2") add("E1601","Solution values require language0.2.",beginning.span);
        if (match("range")) {
            if (declared == Type::Pentarnion || declared == Type::Solution || matrixType(declared)) add("E1204", "Only integer and real range declarations are supported.", beginning.span);
            else declared = declared == Type::Integer ? Type::IntegerRange : Type::RealRange;
        }
        const auto identifier = name();
        const std::string identifierText(text(identifier));
        const bool protectedIdentifier = protectedName(identifier);
        const bool duplicate = m_bindings.contains(identifierText);
        if (protectedIdentifier) add("E1203", "A keyword, basis or builtin name cannot be declared.", identifier.span);
        if (duplicate) add("E1202", "This identifier is already declared in the file.", identifier.span);
        expect("=");
        const auto errorsBefore = m_errors;
        auto value = initializer();
        const Type actual = check(value, false, false, false, declared);
        if(inferArray && (matrixType(actual)||actual==Type::ElementMatrix))declared=actual;
        if(inferArray && rangeType(actual))declared=Type::RealMatrix;
        const bool compatible = assignable(value, actual, declared);
        if (!compatible && actual != Type::Invalid) add("E1204", "Cannot initialize " + std::string(typeName(declared)) + " from " + std::string(typeName(actual)) + ".", value->span);
        const bool valid = compatible && actual != Type::Invalid && errorsBefore == m_errors;
        auto variableSchema=value->schema;
        if(variableSchema&&(matrixType(declared)||declared==Type::ElementMatrix)){auto dynamic=std::make_shared<ValueSchema>(*variableSchema);dynamic->shape.clear();variableSchema=dynamic;}
        if (!protectedIdentifier && !duplicate) {
            m_bindings.emplace(identifierText, Binding{valid ? declared : Type::Invalid, identifier.span,value->ode,value->function,variableSchema});
            if (valid) m_result.symbols.push_back({identifierText, declared, identifier.span,value->ode,value->function,variableSchema});
        }
        Statement result;
        result.kind = StatementKind::Declaration;
        result.name = identifierText;
        result.nameSpan = identifier.span;
        result.declaredType = declared;
        result.expression = value;
        if(text(beginning)=="vector")result.text="vector";
        result.span = terminate(beginning.span, value->span);
        return result;
    }
    Statement assignment() {
        const auto identifier = advance();
        const std::string identifierText(text(identifier));
        expect("=");
        auto value = initializer();
        Type target = Type::Invalid;
        const auto binding = m_bindings.find(identifierText);
        if (protectedName(identifier)) add("E1203", "A keyword, basis or builtin name cannot be assigned.", identifier.span);
        else if (binding == m_bindings.end()) add("E1201", "Assignment requires an earlier declaration.", identifier.span);
        else target = binding->second.type;
        if(value->kind==ExprKind::MatrixLiteral&&binding!=m_bindings.end()&&binding->second.schema&&!binding->second.schema->shape.empty())value->schema=binding->second.schema;
        const Type actual = check(value, false, false, false, target);
        if((target==Type::Record||target==Type::Function||target==Type::Element||target==Type::Algebra||target==Type::ElementMatrix||(binding!=m_bindings.end()&&binding->second.schema&&!binding->second.schema->shape.empty()))&&binding!=m_bindings.end()&&!(target==Type::Element&&scalarType(actual))&&!compatibleSchema(binding->second.schema,value->schema))add("E1204","Assignment cannot change the declared structured, algebra or fixed-shape schema.",value->span);
        if (target != Type::Invalid && actual != Type::Invalid && !assignable(value, actual, target))
            add("E1204", "Assignment cannot change the declared type.", value->span);
        if (target == Type::OdeModel) add("E1801", "ODE definitions cannot be assigned; define a new model name.", identifier.span);
        if (target == Type::Solution && actual == Type::Solution && binding != m_bindings.end()) {
            binding->second.ode = value->ode;
            for (auto& symbol : m_result.symbols) if (symbol.name == identifierText) symbol.ode = value->ode;
        }
        Statement result;
        result.kind = StatementKind::Assignment;
        result.name = identifierText;
        result.nameSpan = identifier.span;
        result.declaredType = target;
        result.expression = value;
        result.span = terminate(identifier.span, value->span);
        return result;
    }
    Statement display() {
        const auto beginning = advance();
        if (m_options.languageVersion != "0.2") add("E1601", "display requires language version 0.2.", beginning.span);
        expect("(");
        std::vector<ExprPtr> arguments;
        if (!is(")")) {
            do { arguments.push_back(expression()); } while (match(","));
        }
        const auto close = expect(")");
        if (arguments.empty()) add("E1205", "display requires at least one argument.", beginning.span);
        for (const auto& argument : arguments) check(argument, true);
        Statement result;
        result.kind = StatementKind::Display;
        result.name = "display";
        result.nameSpan = beginning.span;
        result.arguments = std::move(arguments);
        result.span = terminate(beginning.span, close.span);
        return result;
    }

    Statement clearCommand() {
        const auto beginning = advance();
        const auto errorsBefore = m_errors;
        if (m_options.languageVersion != "0.2")
            add("E1601", "clear commands require language version 0.2.", beginning.span);
        std::vector<std::string> targets;
        SourceSpan last = beginning.span;
        auto target = [&](std::string spelling, SourceSpan span) {
            cancelled();
            const auto tokens = lex(spelling);
            const bool oneName = tokens.tokens.size() == 2 && nameToken(tokens.tokens.front()) &&
                tokens.tokens.front().span.offset == 0 && tokens.tokens.front().span.length == spelling.size();
            if (!oneName) add("E1102", "clear targets must be individual ASCII identifiers.", span);
            else {
                const auto kind = tokens.tokens.front().kind;
                const bool legacyCommandName = m_options.languageVersion == "0.1" && additiveName(spelling);
                if (!legacyCommandName && (kind == TokenKind::Basis || kind == TokenKind::Builtin || kind == TokenKind::Keyword))
                    add("E1203", "A keyword, basis or builtin name cannot be cleared.", span);
            }
            targets.push_back(std::move(spelling));
            last = span;
        };
        if (match("(")) {
            if (!is(")")) {
                do {
                    const auto argument = current();
                    if (argument.kind != TokenKind::String)
                        syntax("E1102", "Function-style clear requires quoted variable names.", argument.span);
                    advance();
                    target(decoded(text(argument)), argument.span);
                } while (match(","));
            }
            last = expect(")").span;
        } else {
            while (!atEnd() && !is(";")) {
                const auto identifier = name();
                target(std::string(text(identifier)), identifier.span);
            }
        }
        if (std::find(targets.begin(), targets.end(), "all") != targets.end()) {
            if (targets.size() != 1) add("E1102", "Use clear all by itself to clear the entire workspace.", beginning.span);
            targets.clear();
        }
        Statement result;
        result.kind = StatementKind::Clear;
        result.name = "clear";
        result.nameSpan = beginning.span;
        result.names = targets;
        result.span = terminate(beginning.span, last);
        if (m_errors == errorsBefore) {
            if (targets.empty()) { m_bindings.clear(); m_result.symbols.clear(); }
            else {
                const std::unordered_set<std::string> removed(targets.begin(), targets.end());
                for (const auto& variable : targets) { cancelled(); m_bindings.erase(variable); }
                m_result.symbols.erase(std::remove_if(m_result.symbols.begin(), m_result.symbols.end(),
                    [&removed](const auto& symbol) { return removed.contains(symbol.name); }), m_result.symbols.end());
            }
        }
        return result;
    }

    Statement clearConsole() {
        const auto beginning = advance();
        if (m_options.languageVersion != "0.2")
            add("E1601", "clc requires language version 0.2.", beginning.span);
        SourceSpan last = beginning.span;
        if (match("(")) {
            if (!is(")")) {
                add("E1205", "clc does not accept arguments.", beginning.span);
                do { expression(); } while (match(","));
            }
            last = expect(")").span;
        }
        Statement result;
        result.kind = StatementKind::ClearConsole;
        result.name = "clc";
        result.nameSpan = beginning.span;
        result.span = terminate(beginning.span, last);
        return result;
    }

    std::pair<std::vector<ExprPtr>, SourceSpan> commandArguments() {
        expect("(");
        std::vector<ExprPtr> arguments;
        if (!is(")")) do { arguments.push_back(expression()); } while (match(","));
        const auto close = expect(")");
        return {std::move(arguments), close.span};
    }

    void figureProfile(const Token& command) {
        if (m_options.languageVersion != "0.2")
            add("E1601", "Plotting/figure commands require language version 0.2.", command.span);
    }

    Statement plot() {
        const auto beginning = advance();
        figureProfile(beginning);
        const int dimensions = text(beginning) == "plot2" ? 2 : 3;
        auto [arguments, close] = commandArguments();
        Statement result;
        result.kind = StatementKind::Plot;
        result.name = std::string(text(beginning));
        result.nameSpan = beginning.span;
        result.dimensions = dimensions;
        result.span = terminate(beginning.span, close);
        if(dimensions==3 && !arguments.empty() && arguments.front()->kind!=ExprKind::StringLiteral) {
            result.name="plot3_coords";result.axes={{-1,-1,-1}};
            if(arguments.size()!=3)add("E1205","Coordinate plot3 requires x, y and z real vectors/series.",result.span);
            for(const auto& argument:arguments) {
                const auto type=check(argument);
                if(type!=Type::RealMatrix && type!=Type::RealSeries && type!=Type::Invalid)
                    add("E1204","Coordinate plot3 requires real vectors or real series.",argument->span);
            }
            result.arguments=std::move(arguments);return result;
        }
        const auto axisCount = static_cast<std::size_t>(dimensions);
        if (arguments.size() <= axisCount)
            add("E1205", "A plot needs its basis labels followed by at least one algebra vector expression.", result.span);
        if (arguments.size() > axisCount + 256)
            add("E1703", "At most 256 algebra vectors may be included in one plot.", result.span);
        algebra::universal::DefinitionPtr definition;bool legacy=false,generic=false;
        for (std::size_t index = axisCount; index < arguments.size(); ++index) {
            const auto type = check(arguments[index]);
            if(type==Type::Element||type==Type::ElementSeries){
                generic=true;const auto current=universalDefinition(arguments[index]);
                if(!current)add("E2530","An algebra projection requires a known immutable definition.",arguments[index]->span);
                else if(definition&&definition->hash()!=current->hash())add("E2530","A projection cannot mix different algebra definitions.",arguments[index]->span);
                else definition=current;
            }else if(type==Type::Pentarnion||type==Type::PentarnionSeries)legacy=true;
            else if(type!=Type::Invalid)add("E1204","Projection arguments must be algebra elements or homogeneous algebra trajectories.",arguments[index]->span);
            result.arguments.push_back(arguments[index]);
        }
        if(legacy&&generic)add("E2530","A projection cannot mix legacy Pentarnions and explicitly typed native algebra elements.",result.span);
        const std::vector<std::string> axes=definition?definition->basis():std::vector<std::string>{"1","i","j","k","ell"};
        if(definition)result.declaredSchema=universalSchema(definition);
        std::vector<bool> selected(axes.size(),false);
        for (std::size_t index = 0; index < std::min(axisCount, arguments.size()); ++index) {
            const auto& axis = arguments[index];
            if (axis->kind != ExprKind::StringLiteral) {
                check(axis);
                add("E1701", "Plot axes must be literal strings naming basis coordinates of the plotted algebra.", axis->span);
                continue;
            }
            axis->type = Type::String;
            const auto label=axis->text=="1"&&!axes.empty()&&axes.front()=="one"?std::string("one"):axis->text;
            const auto found = std::find(axes.begin(), axes.end(), label);
            if (found == axes.end()) {
                add("E1701", "Unknown plot basis coordinate for the selected algebra.", axis->span);
                continue;
            }
            const auto component = static_cast<std::size_t>(found - axes.begin());
            if (selected[component]) add("E1701", "Each axis in a plot projection must be distinct.", axis->span);
            selected[component] = true;
            result.axes[index] = static_cast<int>(component);
            result.names.push_back(axis->text);
        }
        if (!result.arguments.empty()) for (const auto& argument : result.arguments)
            if (argument->type != Type::Invalid && result.arguments.front()->type != Type::Invalid &&
                argument->type != result.arguments.front()->type)
                add("E1204","An individual plot cannot mix arrows and sampled trajectories.",argument->span);
        return result;
    }

    Statement timePlot() {
        const auto beginning=advance(); figureProfile(beginning);
        auto [arguments,close]=commandArguments();
        Statement result;result.kind=StatementKind::Plot;result.name="plot";result.nameSpan=beginning.span;
        result.span=terminate(beginning.span,close);result.dimensions=2;result.axes={{-1,-1,-1}};
        if (arguments.size()!=2) add("E1205","plot requires exactly two real series, x and y.",result.span);
        for (const auto& argument:arguments) {
            const auto type=check(argument);
            if (type!=Type::RealSeries && type!=Type::RealMatrix && type!=Type::Invalid) add("E1204","plot coordinates must be real vectors or real series.",argument->span);
        }
        result.arguments=std::move(arguments);return result;
    }
    Statement subplot() {
        const auto beginning=advance();figureProfile(beginning);
        auto [arguments,close]=commandArguments();
        Statement result;result.kind=StatementKind::Subplot;result.name="subplot";result.nameSpan=beginning.span;
        result.span=terminate(beginning.span,close);result.arguments=arguments;
        if (arguments.size()!=3) add("E1205","subplot requires rows, columns and a one-based row-major index.",result.span);
        for (const auto& argument:arguments) {
            const auto type=check(argument);
            if(type!=Type::Integer && type!=Type::Invalid) add("E1204","subplot dimensions/index must have Integer type.",argument->span);
            const auto literal=signedLiteral(argument);
            if(literal && (*literal<1 || *literal>64)) add("E1705","subplot dimensions/index must be in 1..64.",argument->span);
        }
        if(arguments.size()==3) {
            const auto rows=signedLiteral(arguments[0]), columns=signedLiteral(arguments[1]), index=signedLiteral(arguments[2]);
            if(rows && columns && *rows>=1 && *rows<=64 && *columns>=1 && *columns<=64 &&
                (*rows**columns>64 || (index && *index>*rows**columns)))
                add("E1705","subplot grid supports at most64 cells and its index must fit.",result.span);
        }
        return result;
    }

    Statement figureCommand() {
        const auto beginning = advance();
        figureProfile(beginning);
        auto [arguments, close] = commandArguments();
        const bool select = text(beginning) == "figure";
        Statement result;
        result.kind = select ? StatementKind::Figure : StatementKind::CloseFigure;
        result.name = std::string(text(beginning));
        result.nameSpan = beginning.span;
        result.span = terminate(beginning.span, close);
        if (select) {
            std::size_t firstProperty = 0;
            if (!arguments.empty() && arguments.front()->kind != ExprKind::StringLiteral) {
                result.expression = arguments.front();
                checkFigureIdentifier(result.expression);
                firstProperty = 1;
            }
            figureProperties(arguments, firstProperty, result);
            return result;
        }
        if (arguments.size() != 1) {
            add("E1205", "close requires one integer identifier or the literal string all.", result.span);
            for (const auto& argument : arguments) check(argument, true);
            return result;
        }
        const auto& argument = arguments.front();
        if (argument->kind == ExprKind::StringLiteral && argument->text == "all") {
            argument->type = Type::String;
            result.kind = StatementKind::CloseAllFigures;
            return result;
        }
        result.expression = argument;
        checkFigureIdentifier(argument);
        return result;
    }

    void checkFigureIdentifier(const ExprPtr& argument) {
        const auto type = check(argument, true);
        if (type != Type::Integer && type != Type::Invalid)
            add(type == Type::String ? "E1702" : "E1204", "A figure identifier must be an integer expression in 1..64; close also accepts the literal string all.", argument->span);
        const auto literal = signedLiteral(argument);
        if (literal && (*literal < 1 || *literal > 64))
            add("E1702", "Literal figure identifiers must lie in 1..64.", argument->span);
    }

    std::optional<std::string> literalText(const ExprPtr& argument, std::string_view purpose) {
        if (argument->kind != ExprKind::StringLiteral) {
            check(argument, true);
            add("E1204", std::string(purpose) + " must be a literal string.", argument->span);
            return {};
        }
        argument->type = Type::String;
        if (argument->text.size() > 4096) {
            add("E1706", "Figure names and text are limited to 4096 UTF-8 bytes.", argument->span);
            return {};
        }
        return argument->text;
    }

    void figureProperties(const std::vector<ExprPtr>& arguments, std::size_t first, Statement& result) {
        if ((arguments.size() - first) % 2 != 0)
            add("E1205", "Figure properties must be supplied as key/value string pairs.", result.span);
        for (std::size_t index = first; index + 1 < arguments.size(); index += 2) {
            const auto key = literalText(arguments[index], "A figure property key");
            const auto value = literalText(arguments[index + 1], "A figure property value");
            if (!key || !value) continue;
            const auto property = asciiLower(*key);
            if (property == "name") {
                if (result.figureName) add("E1704", "The Name property was supplied more than once.", arguments[index]->span);
                else result.figureName = *value;
            } else if (property == "numbertitle") {
                if (result.numberTitle) add("E1704", "The NumberTitle property was supplied more than once.", arguments[index]->span);
                else if (asciiLower(*value) == "on") result.numberTitle = true;
                else if (asciiLower(*value) == "off") result.numberTitle = false;
                else add("E1704", "NumberTitle must be the literal string on or off.", arguments[index + 1]->span);
            } else add("E1704", "Unknown figure property; use Name or NumberTitle.", arguments[index]->span);
        }
    }

    Statement dockCommand() {
        const auto beginning = advance();
        figureProfile(beginning);
        auto [arguments, close] = commandArguments();
        Statement result;
        result.kind = text(beginning) == "dock" ? StatementKind::DockFigure : StatementKind::UndockFigure;
        result.name = std::string(text(beginning));
        result.nameSpan = beginning.span;
        result.span = terminate(beginning.span, close);
        if (arguments.size() != 1) {
            add("E1205", "dock/undock require exactly one integer figure identifier.", result.span);
            for (const auto& argument : arguments) check(argument);
        } else {
            result.expression = arguments.front();
            checkFigureIdentifier(result.expression);
        }
        return result;
    }

    Statement tileFigures() {
        const auto beginning = advance();
        figureProfile(beginning);
        auto [arguments, close] = commandArguments();
        Statement result;
        result.kind = StatementKind::TileFigures;
        result.name = "tilefigures";
        result.nameSpan = beginning.span;
        result.text = "studio";
        result.span = terminate(beginning.span, close);
        if (arguments.empty()) return result;
        if (arguments.size() != 1 && arguments.size() != 3)
            add("E1205", "Use tilefigures(), tilefigures(target), or tilefigures(target,rows,columns).", result.span);
        const auto destination = literalText(arguments.front(), "A tiling target");
        if (destination) {
            const auto target = asciiLower(*destination);
            if (target != "studio" && target != "desktop")
                add("E1704", "A tiling target must be studio or desktop.", arguments.front()->span);
            else result.text = target;
        }
        for (std::size_t index = 1; index < arguments.size(); ++index) {
            const auto type = check(arguments[index]);
            if (type != Type::Integer && type != Type::Invalid)
                add("E1204", "Tiling rows/columns must have Integer type.", arguments[index]->span);
            const auto bound = signedLiteral(arguments[index]);
            if (bound && (*bound < 1 || *bound > 64))
                add("E1705", "Tiling rows/columns must be integers in 1..64.", arguments[index]->span);
            result.arguments.push_back(arguments[index]);
        }
        if (arguments.size() == 3) {
            const auto rows = signedLiteral(arguments[1]);
            const auto columns = signedLiteral(arguments[2]);
            if (rows && columns && *rows >= 1 && *rows <= 64 && *columns >= 1 && *columns <= 64 && *rows * *columns > 64)
                add("E1705", "A tiling grid may contain at most 64 cells.", result.span);
        }
        return result;
    }

    Statement figureText() {
        const auto beginning = advance();
        figureProfile(beginning);
        auto [arguments, close] = commandArguments();
        Statement result;
        result.name = std::string(text(beginning));
        result.nameSpan = beginning.span;
        result.kind = result.name == "title" ? StatementKind::SetTitle : result.name == "xlabel" ? StatementKind::SetXLabel :
            result.name == "ylabel" ? StatementKind::SetYLabel : result.name == "zlabel" ? StatementKind::SetZLabel : StatementKind::SetCaption;
        result.span = terminate(beginning.span, close);
        if (arguments.empty()) {
            add("E1205", "Figure text commands require one literal string.", result.span);
            return result;
        }
        if (const auto content = literalText(arguments.front(), "Figure text")) result.text = *content;
        if (arguments.size() % 2 != 1) add("E1205", "Text properties must be key/value pairs after the text literal.", result.span);
        bool seenInterpreter = false;
        for (std::size_t index = 1; index + 1 < arguments.size(); index += 2) {
            const auto key = literalText(arguments[index], "A text property key");
            const auto value = literalText(arguments[index + 1], "A text property value");
            if (!key || !value) continue;
            if (asciiLower(*key) != "interpreter") {
                add("E1704", "Unknown text property; only Interpreter is supported.", arguments[index]->span);
                continue;
            }
            if (seenInterpreter) add("E1704", "Interpreter was supplied more than once.", arguments[index]->span);
            seenInterpreter = true;
            const auto mode = asciiLower(*value);
            if (mode != "latex" && mode != "none")
                add("E1704", "Interpreter must be the literal string latex or none.", arguments[index + 1]->span);
            else result.latex = mode == "latex";
        }
        return result;
    }

    Statement clearFigure() {
        const auto beginning = advance();
        figureProfile(beginning);
        SourceSpan last = beginning.span;
        if (is("(")) {
            auto [arguments, close] = commandArguments();
            last = close;
            if (!arguments.empty()) add("E1205", "clf does not accept arguments.", beginning.span);
            for (const auto& argument : arguments) check(argument);
        }
        Statement result;
        result.kind = StatementKind::ClearFigure;
        result.name = "clf";
        result.nameSpan = beginning.span;
        result.span = terminate(beginning.span, last);
        return result;
    }

    void warnWiden(const ExprPtr& value, Type target) {
        if (value->type != Type::Integer || (target != Type::Real && target != Type::Pentarnion)) return;
        ExprPtr literal = value;
        while ((literal->kind == ExprKind::Unary || literal->kind == ExprKind::Group) && literal->children.size() == 1)
            literal = literal->children.front();
        if (literal->kind != ExprKind::IntegerLiteral) return;
        const auto magnitude = unsignedInteger(literal->text);
        if (magnitude && static_cast<std::uint64_t>(static_cast<double>(*magnitude)) != *magnitude)
            add("W2001", "This integer literal loses exactness when embedded in binary64 coefficients.", value->span, Severity::Warning);
    }
    bool assignable(const ExprPtr& value, Type actual, Type target) {
        if(actual==Type::Tuple&&target!=Type::Tuple&&value->schema&&!value->schema->elements.empty())return typeAssignable(value->schema->elements.front().type,target);
        if(typeAssignable(actual,target)&& (target==Type::Complex||target==Type::Function||target==Type::Record||target==Type::Tuple||target==Type::LogicalMatrix||target==Type::Element||target==Type::Algebra))return true;
        if(target==Type::RealMatrix&&actual==Type::LogicalMatrix)return true;
        if (actual == Type::Invalid || target == Type::Invalid) return false;
        const bool allowed = actual == target || (target == Type::Real && actual == Type::Integer) ||
                             (target == Type::Pentarnion && scalarType(actual)) ||
                             (target == Type::PentarnionMatrix && actual == Type::RealMatrix) ||
                             (matrixType(target) && rangeType(actual));
        if (allowed) warnWiden(value, target);
        return allowed;
    }
    std::size_t algebraOperands(const ExprPtr& value) {
        if (value->kind == ExprKind::Binary && multiplicative(value->text))
            return algebraOperands(value->children[0]) + algebraOperands(value->children[1]);
        return (pentarnionType(value->type)||value->type==Type::Element||value->type==Type::ElementMatrix||value->type==Type::Tensor) ? 1 : 0;
    }
    Type checkOdeSolve(const ExprPtr& value) {
        if(m_options.languageVersion!="0.2") add("E1601","Native ODE solving requires language0.2.",value->span);
        const auto found=m_bindings.find(value->text);
        if(found==m_bindings.end() || found->second.type!=Type::OdeModel || !found->second.ode) {
            add("E1805","odesolve requires an earlier ODE definition.",value->span);
            for(const auto& child:value->children) check(child,true);
            return Type::Invalid;
        }
        value->ode=found->second.ode;
        std::unordered_set<std::string> supplied;
        std::unordered_map<std::string,ExprPtr> options;
        for(std::size_t index=0;index<value->children.size();++index) {
            const auto& key=value->argumentNames[index];const auto& argument=value->children[index];
            if(!supplied.insert(key).second) add("E1805","Duplicate ODE binding/option: "+key,argument->span);
            options.emplace(key,argument);
            Type target=Type::Invalid;std::shared_ptr<const ValueSchema> targetSchema;
            for(const auto& state:value->ode->states) if(state.name==key){target=state.type;targetSchema=state.schema;}
            for(const auto& parameter:value->ode->parameters) if(parameter.name==key){target=parameter.type;targetSchema=parameter.schema;}
            const bool stringOption=key=="method" || key=="jacobian";
            const bool integerOption=key=="max_steps" || key=="max_samples";
            const bool realOption=key=="t0" || key=="t1" || key=="sample_step" || key=="rtol" || key=="atol" || key=="max_seconds";
            const auto actual=check(argument,stringOption);
            if(target!=Type::Invalid) {
                if(actual!=Type::Invalid && !assignable(argument,actual,target))
                    add("E1204","ODE initial value/parameter has an incompatible type.",argument->span);
                if(((target==Type::Element&&actual==Type::Element)||(target==Type::Tensor&&actual==Type::Tensor))&&!compatibleSchema(targetSchema,argument->schema))add("E2530","ODE initial value/parameter uses a different algebra definition.",argument->span);
            } else if(stringOption) {
                if(argument->kind!=ExprKind::StringLiteral) add("E1204","ODE method/Jacobian mode must be literal strings.",argument->span);
                else if((key=="method" && argument->text!="bdf" && argument->text!="adams" && argument->text!="erk" && argument->text!="esdirk") ||
                    (key=="jacobian" && argument->text!="analytic" && argument->text!="finite_difference"))
                    add("E1805","Unknown ODE method or Jacobian mode.",argument->span);
            } else if(integerOption || realOption) {
                if(actual!=Type::Invalid && (integerOption ? actual!=Type::Integer : !scalarType(actual)))
                    add("E1204","ODE solver option has an incompatible type.",argument->span);
                const auto literal=signedLiteral(argument);
                if(literal && key!="t0" && key!="t1" && *literal<=0)
                    add("E1805","ODE step/tolerances/resource limits must be positive.",argument->span);
                if(literal && ((key=="rtol" && *literal>=1) || (key=="max_steps" && *literal>100000000) ||
                    (key=="max_samples" && *literal>1000000) || (key=="max_seconds" && *literal>3600)))
                    add("E1805","The literal ODE tolerance/resource limit exceeds its supported bound.",argument->span);
            } else if(checkAdvancedOdeOption(key,argument,actual)) {}
            else add("E1805","Unknown ODE binding/option: "+key,argument->span);
        }
        for(const auto& state:value->ode->states) if(!supplied.contains(state.name)) add("E1805","Missing initial state binding: "+state.name,value->span);
        for(const auto& parameter:value->ode->parameters) if(!supplied.contains(parameter.name)) add("E1805","Missing parameter binding: "+parameter.name,value->span);
        for(const auto* time:{"t0","t1"}) if(!supplied.contains(time)) add("E1805",std::string("Missing solver option: ")+time,value->span);
        return Type::Solution;
    }
    Type check(const ExprPtr& value, bool allowString = false, bool parentChain = false,
               bool allowMinimumMagnitude = false, Type expected = Type::Invalid) {
        cancelled();
        auto finish = [&value](Type type) { value->type = type; return type; };
        switch (value->kind) {
        case ExprKind::IntegerLiteral: {
            const auto integer = unsignedInteger(value->text);
            const auto maximum = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
            if (!integer || *integer > maximum + (allowMinimumMagnitude ? 1U : 0U)) {
                add("E1402", "Integer literal is outside the signed 64-bit range.", value->span);
                return finish(Type::Invalid);
            }
            return finish(Type::Integer);
        }
        case ExprKind::RealLiteral:
            if (!realLiteral(value->text)) {
                add("E1403", "Real literal is nonfinite or not representable as binary64.", value->span);
                return finish(Type::Invalid);
            }
            return finish(Type::Real);
        case ExprKind::StringLiteral:return finish(Type::String);
        case ExprKind::AllIndices:return finish(Type::IntegerRange);
        case ExprKind::EndIndex:return finish(Type::Integer);
        case ExprKind::Reference: {
            if (value->text == "i" || value->text == "j" || value->text == "k" || value->text == "ell")
                return finish(Type::Pentarnion);
            const auto binding = m_bindings.find(value->text);
            if (binding == m_bindings.end()) {
                add("E1201", "Identifier is used before declaration: " + value->text, value->span);
                return finish(Type::Invalid);
            }
            value->ode=binding->second.ode;value->schema=binding->second.schema?binding->second.schema:callableSchema(binding->second.function);value->function=value->schema?value->schema->function:nullptr;
            return finish(binding->second.type);
        }
        case ExprKind::Group: {
            const auto type=check(value->children[0], allowString, false, false, expected);
            value->ode=value->children[0]->ode;value->schema=value->children[0]->schema;value->function=value->children[0]->function;return finish(type);
        }
        case ExprKind::FunctionHandle:return finish(checkFunctionHandle(value));
        case ExprKind::Lambda:return finish(Type::Function);
        case ExprKind::Invoke:return finish(checkInvoke(value,expected));
        case ExprKind::TupleLiteral:{auto schema=std::make_shared<ValueSchema>();for(const auto& child:value->children){const auto type=check(child,true);schema->elements.push_back({{},type,child->schema});}value->schema=schema;return finish(Type::Tuple);}
        case ExprKind::OdeSolve:return finish(checkOdeSolve(value));
        case ExprKind::MatrixLiteral: {
            return finish(checkUniversalArrayLiteral(value));
        }
        case ExprKind::Index: {
            if(m_options.languageVersion!="0.2")add("E1601","Indexing requires language 0.2.",value->span);
            const auto type=check(value->children.front());
            if(type==Type::Function){value->kind=ExprKind::Invoke;return finish(checkInvoke(value,expected));}
            if(auto tensor=checkTensorIndex(value,type))return finish(*tensor);
            if(auto array=checkUniversalArrayIndex(value,type))return finish(*array);
            const bool series=type==Type::RealSeries || type==Type::PentarnionSeries || type==Type::ElementSeries;
            bool valid=matrixType(type)||series,slice=false;
            if(type!=Type::Invalid && !valid)add("E1204","Only matrices, vectors and sampled series support indexing.",value->span);
            if(value->children.size()!=2 && (!matrixType(type)||value->children.size()!=3)) {
                add("E1205","Use A(index), A(row,column), or series(index).",value->span);valid=false;
            }
            for(std::size_t index=1;index<value->children.size();++index) {
                const auto indexType=check(value->children[index]);
                if(indexType!=Type::Integer && indexType!=Type::Real && !rangeType(indexType) && indexType!=Type::RealMatrix && indexType!=Type::LogicalMatrix && indexType!=Type::Invalid) {
                    add("E1204","Indices must be positive integers, ranges, real index vectors or colon.",value->children[index]->span);valid=false;
                }
                slice=slice||rangeType(indexType)||matrixType(indexType);
                const auto literal=signedLiteral(value->children[index]);
                if(literal && (*literal<1||std::trunc(*literal)!=*literal))add("E1902","Indices are one-based positive integers.",value->children[index]->span);
            }
            if(type==Type::ElementSeries){
                value->schema=value->children.front()->schema;
                if(slice){add("E2530","Project coefficients with coeff(series,n) before selecting sample arrays.",value->span);return finish(Type::Invalid);}
                const auto elementType=value->schema&&value->schema->seriesElementType==Type::Complex?Type::Complex:Type::Element;
                if(value->schema){auto schema=std::make_shared<ValueSchema>(*value->schema);schema->seriesElementType=Type::Invalid;value->schema=schema;}
                return finish(valid?elementType:Type::Invalid);
            }
            const bool pent=type==Type::PentarnionMatrix||type==Type::PentarnionSeries;
            return finish(valid?(slice?(pent?Type::PentarnionMatrix:Type::RealMatrix):(pent?Type::Pentarnion:Type::Real)):Type::Invalid);
        }
        case ExprKind::Transpose: {
            if(m_options.languageVersion!="0.2")add("E1601","Transpose requires language 0.2.",value->span);
            const auto type=check(value->children.front());
            if(type==Type::ElementMatrix||(universalArrayType(type)&&!universalExpressionShape(value->children.front()).empty())){
                auto dimensions=universalExpressionShape(value->children.front());if(dimensions.size()==2)std::swap(dimensions[0],dimensions[1]);
                if(type==Type::ElementMatrix&&value->text=="'"&&(!value->children.front()->schema||!value->children.front()->schema->algebra||value->children.front()->schema->algebra->conjugation().empty())){add("E2540","Conjugate transpose requires a declared entry conjugation map.",value->span);return finish(Type::Invalid);}
                value->schema=universalArraySchema(value->children.front()->schema,std::move(dimensions));return finish(type);
            }
            if(numericType(type) || matrixType(type))return finish(type);
            if(type!=Type::Invalid)add("E1204","Transpose requires a numeric scalar or matrix.",value->span);
            return finish(Type::Invalid);
        }
        case ExprKind::Member: {
            const auto type=check(value->children[0]);
            if(auto universal=checkUniversalMember(value))return finish(*universal);
            value->ode=value->children[0]->ode;
            if(type==Type::Record){const auto schema=value->children[0]->schema;if(schema)for(const auto& field:schema->fields)if(field.name==value->text){value->schema=field.schema;value->function=field.schema?field.schema->function:nullptr;return finish(field.type);}add("E1204","Unknown or untyped record member: "+value->text,value->span);return finish(Type::Invalid);}
            value->ode=value->children[0]->ode;
            if(type!=Type::Solution || !value->ode) {
                if(type!=Type::Invalid) add("E1806","Only solution values provide sampled members.",value->span);
                return finish(Type::Invalid);
            }
            if(value->text=="t") return finish(Type::RealSeries);
            for(const auto& state:value->ode->states) if(state.name==value->text){
                if(universalArrayType(state.type)||state.type==Type::Tensor){auto schema=state.schema?std::make_shared<ValueSchema>(*state.schema):std::make_shared<ValueSchema>();schema->seriesElementType=state.type;value->schema=schema;return finish(Type::ArraySeries);}
                if(state.type==Type::Element||state.type==Type::Complex){auto schema=state.schema?std::make_shared<ValueSchema>(*state.schema):std::make_shared<ValueSchema>();schema->seriesElementType=state.type;if(state.type==Type::Complex)schema->algebra=algebra::universal::builtin("complex");value->schema=schema;return finish(Type::ElementSeries);}
                return finish(state.type==Type::Pentarnion?Type::PentarnionSeries:Type::RealSeries);
            }
            add("E1806","Unknown solution state: "+value->text,value->span);return finish(Type::Invalid);
        }
        case ExprKind::Unary: {
            const auto& child = value->children[0];
            const Type operand = check(child, allowString, false,
                value->text == "-" && child->kind == ExprKind::IntegerLiteral);
            if (operand == Type::Invalid) return finish(Type::Invalid);
            if((operand==Type::Element||operand==Type::ElementMatrix||operand==Type::Tensor)&&(value->text=="+"||value->text=="-")){value->schema=child->schema;return finish(operand);}
            if(value->text=="!"||value->text=="~") {
                if(matrixType(operand)&&!pentarnionType(operand))return finish(Type::LogicalMatrix);
                if(!scalarType(operand)){add("E1204","Logical not requires real scalar or array values.",value->span);return finish(Type::Invalid);}
                return finish(Type::Integer);
            }
            if (!numericType(operand) && !matrixType(operand)) {
                add("E1204", "Unary signs require an integer, real or Pentarnion operand.", value->span);
                return finish(Type::Invalid);
            }
            return finish(operand==Type::LogicalMatrix&&value->text=="-"?Type::RealMatrix:operand);
        }
        case ExprKind::Binary: {
            const bool chain = multiplicative(value->text);
            Type left = check(value->children[0], allowString, chain);
            Type right = check(value->children[1], allowString, chain);
            if (left == Type::Invalid || right == Type::Invalid) return finish(Type::Invalid);
            if(auto tensor=checkTensorBinary(value,left,right)){
                finish(*tensor);const auto schema=value->schema;const bool nonassociative=schema&&(schema->tensorEntryType==Type::Pentarnion||(schema->tensorEntryType==Type::Element&&schema->algebra&&!(schema->algebra->properties().associative.value&&schema->algebra->properties().associative.evidence==algebra::universal::Evidence::ProvedExact)));
                if(nonassociative&&chain&&!parentChain&&algebraOperands(value)>=3)add("E1301","Parenthesize products containing three or more nonassociative tensor/algebra operands.",value->span);
                return *tensor;
            }
            if(auto array=checkUniversalArrayBinary(value,left,right)){
                finish(*array);bool nonassociative=*array==Type::PentarnionMatrix;
                if(*array==Type::ElementMatrix&&value->schema&&value->schema->algebra){const auto& property=value->schema->algebra->properties().associative;nonassociative=!(property.value&&property.evidence==algebra::universal::Evidence::ProvedExact);}
                if(nonassociative&&chain&&!parentChain&&algebraOperands(value)>=3)add("E1301","Parenthesize products/divisions containing three or more operands in a nonassociative array algebra.",value->span);
                return *array;
            }
            if(left==Type::Element||right==Type::Element){
                const auto element=left==Type::Element?value->children[0]:value->children[1];
                const auto other=left==Type::Element?right:left;
                if(!element->schema||!element->schema->algebra||(other!=Type::Element&&!scalarType(other))){add("E2530","Generic algebra arithmetic requires matching elements or real scalars.",value->span);return finish(Type::Invalid);}
                if(other==Type::Element&&!compatibleSchema(value->children[0]->schema,value->children[1]->schema)){add("E2530","Arithmetic cannot mix different algebra definitions.",value->span);return finish(Type::Invalid);}
                const bool needsEmbedding=other!=Type::Element&&(value->text=="+"||value->text=="-"||value->text=="=="||value->text=="!="||value->text=="~="||(value->text=="/"&&right==Type::Element)||(value->text=="\\"&&left==Type::Element));
                if(needsEmbedding&&element->schema->algebra->unit().empty()){add("E2530","This algebra has no unit for embedding real scalar operands.",value->span);return finish(Type::Invalid);}
                if(value->text=="=="||value->text=="!="||value->text=="~=")return finish(Type::Integer);
                if(value->text!="+"&&value->text!="-"&&value->text!="*"&&value->text!="/"&&value->text!="\\"&&value->text!="^"){add("E2530","Unsupported generic algebra operator.",value->span);return finish(Type::Invalid);}
                if(value->text=="^"&&(left!=Type::Element||right!=Type::Integer)){add("E2530","An algebra power requires an element and an integer exponent.",value->span);return finish(Type::Invalid);}
                if(value->text=="^"){const auto exponent=signedLiteral(value->children[1]);if(exponent&&(*exponent< -4096||*exponent>4096)){add("E2530","Generic powers require an exponent in[-4096,4096].",value->span);return finish(Type::Invalid);}if(exponent&&*exponent<=0&&element->schema->algebra->unit().empty()){add("E2530","Zero and negative powers require an algebra unit.",value->span);return finish(Type::Invalid);}}
                const auto& associativity=element->schema->algebra->properties().associative;
                if(chain&&!parentChain&&algebraOperands(value)>=3&&!(associativity.value&&associativity.evidence==algebra::universal::Evidence::ProvedExact))add("E1301","Parenthesize products/divisions containing three or more operands in a nonassociative algebra.",value->span);
                value->schema=element->schema;return finish(Type::Element);
            }
            if((left==Type::Complex&&right!=Type::Complex&&!scalarType(right))||(right==Type::Complex&&left!=Type::Complex&&!scalarType(left))){add("E1204","Complex scalars must remain distinct from Pentarnions and real/Pentarnion arrays.",value->span);return finish(Type::Invalid);}
            if(rangeType(left))left=Type::RealMatrix;
            if(rangeType(right))right=Type::RealMatrix;
            const auto& logical=value->text;
            if((logical=="<"||logical==">"||logical=="<="||logical==">="||logical=="&"||logical=="|")&&(matrixType(left)||matrixType(right))){if(pentarnionType(left)||pentarnionType(right)||left==Type::Complex||right==Type::Complex){add("E1204","Logical array operations require real entries.",value->span);return finish(Type::Invalid);}return finish(Type::LogicalMatrix);}
            if(logical=="&&"||logical=="||"||logical=="&"||logical=="|"||logical=="<"||logical==">"||logical=="<="||logical==">=") {
                if(!scalarType(left)||!scalarType(right)){add("E1204","Conditions and ordered comparisons require real scalar operands.",value->span);return finish(Type::Invalid);}
                return finish(Type::Integer);
            }
            if(logical=="=="||logical=="!="||logical=="~=") {
                if((numericType(left)&&numericType(right))||(left==Type::String&&right==Type::String)||(matrixType(left)&&matrixType(right)))return finish(Type::Integer);
                add("E1204","Equality requires compatible numeric, string or matrix values.",value->span);return finish(Type::Invalid);
            }
            if(rangeType(left))left=Type::RealMatrix;
            if(rangeType(right))right=Type::RealMatrix;
            if(matrixType(left) || matrixType(right)) {
                if((!numericType(left) && !matrixType(left)) || (!numericType(right) && !matrixType(right))) {
                    add("E1204","Matrix arithmetic requires numeric scalar or matrix operands.",value->span);
                    return finish(Type::Invalid);
                }
                const auto& op=value->text;
                if(op=="\\" || op=="^") {
                    add("E1204",op=="\\"?"Use linsolve(A,B) or solveleft(A,B) for matrix equations; reciprocal left division is a scalar algebra operation.":"Use .^ for entrywise matrix powers; matrix ^ is not supported.",value->span);
                    return finish(Type::Invalid);
                }
                if((op=="/" && !scalarType(right)) || (op=="./" && !scalarType(right) && right!=Type::RealMatrix)) {
                    add("E1204","Matrix division requires a real scalar divisor, or real matrix for ./.",value->children[1]->span);
                    return finish(Type::Invalid);
                }
                if(op==".^" && right!=Type::Integer && right!=Type::Real && right!=Type::RealMatrix) {
                    add("E1204","Entrywise exponents must be real scalars or real matrices.",value->children[1]->span);
                    return finish(Type::Invalid);
                }
                if(op==".^" && pentarnionType(left) && right==Type::Real) {
                    add("E1208","A Pentarnion exponent must have Integer type.",value->children[1]->span);
                    return finish(Type::Invalid);
                }
                const auto result=pentarnionType(left) || (op!=".^" && pentarnionType(right))?Type::PentarnionMatrix:Type::RealMatrix;
                finish(result);
                if(chain && !parentChain && algebraOperands(value)>=3)
                    add("E1301","Parenthesize products/divisions containing three or more Pentarnion operands.",value->span);
                return result;
            }
            if(left==Type::Complex||right==Type::Complex){if((!scalarType(left)&&left!=Type::Complex)||(!scalarType(right)&&right!=Type::Complex)||value->text=="\\"){add("E1204","Complex arithmetic cannot mix Pentarnion values or use left division.",value->span);return finish(Type::Invalid);}return finish(Type::Complex);}
            if (!numericType(left) || !numericType(right)) {
                add("E1204", "Arithmetic operators require numeric scalar or Pentarnion operands.", value->span);
                return finish(Type::Invalid);
            }
            Type result;
            if (value->text == "^" || value->text==".^") {
                if (left == Type::Pentarnion && right != Type::Integer) {
                    add("E1208", "A Pentarnion exponent must have integer type.", value->children[1]->span);
                    return finish(Type::Invalid);
                }
                if (left != Type::Pentarnion && !scalarType(right)) {
                    add("E1204", "Scalar exponentiation requires a scalar exponent.", value->children[1]->span);
                    return finish(Type::Invalid);
                }
                result = left == Type::Pentarnion ? Type::Pentarnion : Type::Real;
            } else if (value->text == "/" || value->text == "\\" || value->text=="./") {
                if(value->text=="./" && !scalarType(right)) {
                    add("E1204","Elementwise division requires a real divisor.",value->children[1]->span);return finish(Type::Invalid);
                }
                const auto& divisor = value->children[value->text == "/" ? 1 : 0];
                const auto literal = signedLiteral(divisor);
                if (literal && *literal == 0) add("E1401", "Literal zero divisor.", divisor->span);
                result = left == Type::Pentarnion || right == Type::Pentarnion ? Type::Pentarnion : Type::Real;
            } else result = left == Type::Pentarnion || right == Type::Pentarnion ? Type::Pentarnion :
                          left == Type::Real || right == Type::Real ? Type::Real : Type::Integer;
            finish(result);
            if (result == Type::Real || result == Type::Pentarnion) {
                warnWiden(value->children[0], result);
                warnWiden(value->children[1], result);
            }
            if (chain && !parentChain && algebraOperands(value) >= 3)
                add("E1301", "Parenthesize products/divisions containing three or more Pentarnion operands.", value->span);
            return result;
        }
        case ExprKind::Range: {
            bool valid = true;
            bool allInteger = true;
            for (const auto& child : value->children) {
                const Type component = check(child, allowString);
                if (component == Type::Invalid) valid = false;
                else if (!scalarType(component)) {
                    add("E1204", "Range components must be scalar expressions.", child->span);
                    valid = false;
                }
                if (component != Type::Integer) allInteger = false;
            }
            const auto step = signedLiteral(value->children[1]);
            if (step && *step == 0) add("E1404", "A finite range step cannot be literal zero.", value->children[1]->span);
            if (!valid) return finish(Type::Invalid);
            if (expected == Type::IntegerRange && !allInteger) {
                add("E1204", "An integer range requires integer start, step and stop expressions.", value->span);
                return finish(Type::Invalid);
            }
            const Type type = expected == Type::RealRange || !allInteger ? Type::RealRange : Type::IntegerRange;
            if (type == Type::RealRange) for (const auto& child : value->children) warnWiden(child, Type::Real);
            return finish(type);
        }
        case ExprKind::Call:
        case ExprKind::PentConstructor: {
            for (const auto& child : value->children) check(child, true);
            if(auto type=checkTensorCall(value))return finish(*type);
            if(auto type=checkUniversalArrayCall(value))return finish(*type);
            if(auto type=checkUniversalCall(value))return finish(*type);
            if(auto type=checkDeveloperToolsCall(value))return finish(*type);
            if(value->text=="compile_info") {
                if(!value->children.empty())add("E1205","Use compile_info() without arguments.",value->span);
                auto schema=std::make_shared<ValueSchema>();
                for(const auto field:{"requested_jobs","effective_jobs","translation_units","compiler_attempts","cache_hits","failed_preparations","peak_preparations","peak_compiler_jobs"})schema->fields.push_back({field,Type::Integer,{}});
                schema->fields.push_back({"compilation_seconds",Type::Real,{}});value->schema=std::move(schema);return finish(Type::Record);
            }
            if(auto type=checkAdvancedOdeCall(value))return finish(*type);
            if(auto type=checkFittingCall(value))return finish(*type);
            if(auto type=checkOptimizationCall(value))return finish(*type);
            if(auto type=checkIterationCall(value))return finish(*type);
            if(auto type=checkMapsCall(value))return finish(*type);
            if(auto type=checkPdeEnsembleCall(value))return finish(*type);
            if(auto type=checkPdeCall(value))return finish(*type);
            if(auto type=checkFemCall(value))return finish(*type);
            if(auto type=checkBifurcationCall(value))return finish(*type);
            if(auto type=checkExperimentBundleCall(value))return finish(*type);
            if(auto type=checkExtraGeometryCall(value))return finish(*type);
            if(auto type=checkGeometryCall(value))return finish(*type);
            if(auto type=checkChaosCall(value))return finish(*type);
            if(auto type=checkChartGeometryCall(value))return finish(*type);
            if(auto type=checkModelSensitivityCall(value))return finish(*type);
            if(auto type=checkSensitivityCall(value))return finish(*type);
            if(auto type=checkDaeCall(value))return finish(*type);
            if(auto type=checkQuantityCall(value))return finish(*type);
            if(auto type=checkConstrainedMechanicsCall(value))return finish(*type);
            if(auto type=checkMechanicsCall(value))return finish(*type);
            if(auto type=checkNDCall(value))return finish(*type);
            if(auto type=checkInferenceCall(value))return finish(*type);
            if(auto type=checkAnimationCall(value))return finish(*type);
            if(auto type=checkVideoCall(value))return finish(*type);
            if(auto type=checkPublicationCall(value))return finish(*type);
            if(auto type=checkVerificationCall(value))return finish(*type);
            if(auto type=checkHybridOdeCall(value))return finish(*type);
            if(auto type=checkDiscoveryCall(value))return finish(*type);
            if(auto type=checkProvenanceCall(value))return finish(*type);
            if(auto type=checkPentSymbolicCall(value))return finish(*type);
            if(auto type=checkOrbitsCall(value))return finish(*type);
            if(auto type=checkGraphViewCall(value))return finish(*type);
            if(auto type=checkGraphToolsCall(value))return finish(*type);
            if(auto type=checkNetworkCall(value))return finish(*type);
            if(auto type=checkGraphCall(value))return finish(*type);
            if(auto type=checkScienceDataCall(value))return finish(*type);
            if(auto type=checkEvolutionCall(value))return finish(*type);
            if(auto type=checkDerivativeModelCall(value))return finish(*type);
            if(auto type=checkVectorThreadingCall(value))return finish(*type);
            if(auto type=checkLinalgCall(value))return finish(*type);
            if(auto type=checkDerivativeCall(value))return finish(*type);
            if(auto type=checkThirdDerivativeCall(value))return finish(*type);
            if(auto type=checkRootsCall(value))return finish(*type);
            if(foundationBuiltin(value->text))return finish(checkFoundationCall(value));
            if(const auto found=m_bindings.find(value->text);found!=m_bindings.end()&&found->second.function) {
                const auto& function=*found->second.function;
                reorderCallArguments(value,function);
                value->schema=function.resultSchema;
                for(const auto& child:value->children)if(child->type==Type::Invalid)check(child,true);
                if(function.parameters.size()!=value->children.size()){add("E1205","Incorrect user function argument count.",value->span);return finish(Type::Invalid);}
                bool valid=true;
                for(std::size_t n=0;n<function.parameters.size();++n)if(!assignable(value->children[n],value->children[n]->type,function.parameters[n].type)) {
                    add("E1204","User function argument has an incompatible type.",value->children[n]->span);valid=false;
                }
                for(std::size_t n=0;n<function.parameters.size()&&n<value->children.size();++n)if(n<function.parameterSchemas.size()&&!((function.parameters[n].type==Type::Element&&scalarType(value->children[n]->type))||compatibleSchema(function.parameterSchemas[n],value->children[n]->schema))){add("E1204","Function option/callback schema differs from its declared default.",value->children[n]->span);valid=false;}
                return finish(valid?function.result:Type::Invalid);
            }
            if(const auto found=m_bindings.find(value->text);found!=m_bindings.end()&&found->second.type==Type::Function){if(!value->argumentNames.empty()&&std::any_of(value->argumentNames.begin(),value->argumentNames.end(),[](const auto& n){return !n.empty();}))add("E1205","Named callback arguments need a known signature.",value->span);return finish(expected!=Type::Invalid?expected:Type::Real);}
            if(hasNamedArguments(value))add("E1205","This builtin accepts positional arguments only.",value->span);
            if(value->children.size()==1&&value->children[0]->type==Type::Complex){const auto& name=value->text;if(name=="abs"||name=="norm2")return finish(Type::Real);if(name=="conj"||name=="sin"||name=="cos"||name=="exp"||name=="sinh"||name=="cosh"||name=="sqrt"||name=="tan"||name=="asin"||name=="acos"||name=="atan"||name=="tanh"||name=="asinh"||name=="acosh"||name=="atanh"||name=="log"||name=="log10"||name=="log2")return finish(Type::Complex);}
            if(utilityFunction(value->text))return finish(checkUtility(value));
            if(arrayFunction(value->text)) {
                if(m_options.languageVersion!="0.2")add("E1601","Matrix/random functions require language 0.2.",value->span);
                const auto count=value->children.size();
                bool valid=true;
                auto arity=[&](bool okay,const char*message) {if(!okay){add("E1205",message,value->span);valid=false;}};
                auto integer=[&](std::size_t index,bool positive) {
                    if(index>=count)return;
                    const auto& child=value->children[index];
                    if(child->type!=Type::Integer && child->type!=Type::Invalid) {
                        add("E1204","Dimensions, indices and random seeds must have Integer type.",child->span);valid=false;
                    }
                    const auto literal=signedLiteral(child);
                    if(literal && *literal<(positive?1:0)) {
                        add("E1902",positive?"This argument must be positive.":"This argument must be nonnegative.",child->span);valid=false;
                    }
                };
                Type result=Type::RealMatrix;
                if(value->text=="sum" || value->text=="mean" || value->text=="min" || value->text=="max") {
                    const bool ordered=value->text=="min"||value->text=="max";
                    arity(ordered?(count>=1&&count<=3):(count==1||count==2),"Use sum/mean(A[,dim]), min/max(A), min/max(A,B), or min/max(A,[],dim).");
                    if(count) {
                        auto type=value->children[0]->type;if(rangeType(type)||type==Type::RealSeries)type=Type::RealMatrix;else if(type==Type::PentarnionSeries)type=Type::PentarnionMatrix;
                        if(!numericType(type)&&!matrixType(type)&&type!=Type::Invalid){add("E1204","Reduction requires numeric scalar, vector or matrix.",value->span);valid=false;}
                        if(ordered&&pentarnionType(type)){add("E1204","Ordered reductions require real values.",value->span);valid=false;}
                        result=ordered&&type==Type::Integer?Type::Real:type;
                    }
                    if(ordered&&count==2){
                        const auto sourceType=value->children[1]->type;
                        const auto type=sourceType==Type::RealSeries?Type::RealMatrix:sourceType;
                        if(!scalarType(type)&&type!=Type::RealMatrix&&!rangeType(type)&&type!=Type::Invalid){add("E1204","Pairwise min/max requires real values.",value->span);valid=false;}
                        if(matrixType(type)||rangeType(type))result=Type::RealMatrix;
                        else if(result==Type::Integer)result=Type::Real;
                    }
                    if((ordered&&count==3)||(!ordered&&count==2)) {
                        const auto slot=count-1;integer(slot,true);const auto dim=signedLiteral(value->children[slot]);
                        static_cast<void>(dim);
                        if(ordered&&(value->children[1]->kind!=ExprKind::MatrixLiteral||!value->children[1]->children.empty())){add("E1204","Use an empty [] as the second argument for an explicit reduction dimension.",value->span);valid=false;}
                    }
                } else if(value->text=="rng") {
                    arity(count==1,"Use rng(seed) or rng(\"default\").");result=Type::Void;
                    if(count==1 && value->children[0]->kind==ExprKind::StringLiteral) {
                        if(value->children[0]->text!="default") {add("E1204","The only supported RNG string is default.",value->children[0]->span);valid=false;}
                    } else integer(0,false);
                } else if(value->text=="transpose") {
                    arity(count==1,"transpose requires one numeric scalar or matrix.");
                    if(count==1) {
                        result=rangeType(value->children[0]->type)?Type::RealMatrix:value->children[0]->type;
                        if(!numericType(result) && !matrixType(result) && result!=Type::Invalid) {
                            add("E1204","transpose requires a numeric scalar or matrix.",value->span);valid=false;
                        }
                    }
                } else if(value->text=="size" || value->text=="numel" || value->text=="length") {
                    arity(count==1 || (value->text=="size" && count==2),"Use size(A[,dimension]), numel(A) or length(A).");
                    if(count && !matrixType(value->children[0]->type) && !numericType(value->children[0]->type) && !rangeType(value->children[0]->type) && value->children[0]->type!=Type::RealSeries && value->children[0]->type!=Type::PentarnionSeries && value->children[0]->type!=Type::Invalid) {
                        add("E1204","Array size functions require a numeric scalar or matrix.",value->children[0]->span);valid=false;
                    }
                    if(count==2)integer(1,true);
                    result=value->text=="size" && count==1?Type::RealMatrix:Type::Integer;
                } else if(value->text=="linspace") {
                    arity(count==2 || count==3,"Use linspace(start,stop[,count]).");
                    for(std::size_t index=0;index<std::min<std::size_t>(2,count);++index)
                        if(!scalarType(value->children[index]->type) && value->children[index]->type!=Type::Invalid) {
                            add("E1204","linspace endpoints must be real scalars.",value->children[index]->span);valid=false;
                        }
                    integer(2,false);
                } else if(value->text=="randi") {
                    arity(count>=1 && count<=3,"Use randi(imax), randi(imax,n) or randi(imax,rows,columns).");
                    integer(0,true);
                    if(count && signedLiteral(value->children[0]).value_or(1)>9007199254740991.0) {
                        add("E1902","randi's maximum must not exceed 2^53-1.",value->children[0]->span);valid=false;
                    }
                    for(std::size_t index=1;index<count;++index)integer(index,false);
                    result=count==1?Type::Integer:Type::RealMatrix;
                } else {
                    const bool random=value->text=="rand" || value->text=="randn";
                    arity(count<=2 && (random || count>=1),"Use function(n) or function(rows,columns); rand/randn also allow no arguments.");
                    for(std::size_t index=0;index<count;++index)integer(index,false);
                    result=random && count==0?Type::Real:Type::RealMatrix;
                }
                for(const auto& child:value->children)if(child->type==Type::Invalid)valid=false;
                return finish(valid?result:Type::Invalid);
            }
            if(value->text=="conj" && value->children.size()==1 && (matrixType(value->children[0]->type)||rangeType(value->children[0]->type)))
                return finish(rangeType(value->children[0]->type)?Type::RealMatrix:value->children[0]->type);
            if(value->children.size()==1 && (matrixType(value->children[0]->type)||rangeType(value->children[0]->type)) &&
                (elementaryFunction(value->text) || value->text=="sqrt" || value->text=="scalar" ||
                 value->text=="pure" || value->text=="norm2")) {
                const auto operand=rangeType(value->children[0]->type)?Type::RealMatrix:value->children[0]->type;
                const bool whole=value->text=="sin" || value->text=="cos" || value->text=="exp" ||
                    value->text=="sinh" || value->text=="cosh";
                const bool algebra=value->text=="scalar" || value->text=="pure" || value->text=="norm2";
                if(operand==Type::PentarnionMatrix && !whole && !algebra) {
                    add("E1204","This elementwise function supports real matrix entries only.",value->span);
                    return finish(Type::Invalid);
                }
                return finish(value->text=="scalar" || value->text=="norm2" ? Type::RealMatrix :
                    value->text=="pure" ? Type::PentarnionMatrix : operand);
            }
            if(value->text=="coeff" && value->children.size()==2 && value->children[0]->type==Type::PentarnionMatrix) {
                if(value->children[1]->type!=Type::Integer && value->children[1]->type!=Type::Invalid)
                    add("E1204","A matrix coefficient index must have Integer type.",value->children[1]->span);
                const auto index=signedLiteral(value->children[1]);
                if(index && (*index<0 || *index>4))add("E1405","Literal coefficient index must lie in 0..4.",value->children[1]->span);
                return finish(Type::RealMatrix);
            }
            if(value->text=="toc") {
                if(m_options.languageVersion!="0.2")add("E1601","toc requires language0.2.",value->span);
                if(!value->children.empty())add("E1205","toc does not accept timer handles or arguments.",value->span);
                return finish(Type::Real);
            }
            if(value->text=="coeff" && value->children.size()==2 && value->children[0]->type==Type::PentarnionSeries) {
                if(value->children[1]->type!=Type::Integer && value->children[1]->type!=Type::Invalid)
                    add("E1204","A series coefficient index must have Integer type.",value->children[1]->span);
                const auto index=signedLiteral(value->children[1]);
                if(index && (*index<0 || *index>4)) add("E1405","Literal coefficient index must lie in0..4.",value->children[1]->span);
                return finish(Type::RealSeries);
            }
            if (additiveCommand(value->text)) {
                add("E1601", "This command is only supported as a top-level statement in language 0.2.", value->span);
                return finish(Type::Invalid);
            }
            if (elementaryFunction(value->text) && m_options.languageVersion != "0.2") {
                add("E1601", "This elementary function requires language version 0.2.", value->span);
                return finish(Type::Invalid);
            }
            const auto signature = std::find_if(signatures.begin(), signatures.end(),
                [&value](const auto& item) { return item.name == value->text; });
            if (signature == signatures.end()) {
                add(forbiddenConstruct(value->text) ? "E1601" : "E1206", "Unknown or unsupported callable: " + value->text, value->span);
                return finish(Type::Invalid);
            }
            if (value->children.size() != signature->arity ||
                (value->text == "pent" && value->kind != ExprKind::PentConstructor)) {
                add(value->text == "pent" ? "E1207" : "E1205", "Incorrect builtin argument count or constructor form.", value->span);
                return finish(Type::Invalid);
            }
            bool valid = true;
            for (std::size_t index = 0; index < value->children.size(); ++index) {
                const auto& child = value->children[index];
                const auto parameter = signature->parameters[index];
                const bool compatible = parameter == Parameter::Scalar ? scalarType(child->type) :
                    parameter == Parameter::Integer ? child->type == Type::Integer : numericType(child->type);
                if (child->type == Type::Invalid) valid = false;
                else if (!compatible) {
                    add("E1204", "Builtin argument has an incompatible type.", child->span);
                    valid = false;
                } else if (parameter == Parameter::Pentarnion) warnWiden(child, Type::Pentarnion);
                else if (parameter == Parameter::Scalar || parameter == Parameter::Numeric) warnWiden(child, Type::Real);
            }
            if (value->text == "coeff") {
                const auto index = signedLiteral(value->children[1]);
                if (index && (*index < 0 || *index > 4)) add("E1405", "Literal coefficient index must lie in 0..4.", value->children[1]->span);
            }
            const Type result = elementaryFunction(value->text) && value->children.front()->type == Type::Pentarnion
                ? Type::Pentarnion : signature->result;
            return finish(valid ? result : Type::Invalid);
        }
        }
        return finish(Type::Invalid);
    }
};
} // namespace

bool AnalysisResult::ok() const noexcept {
    return !cancelled && std::none_of(diagnostics.begin(), diagnostics.end(),
        [](const auto& item) { return item.severity == Severity::Error; });
}

AnalysisResult analyse(std::string_view source, const AnalysisOptions& options) {
    return Frontend(source, options).run();
}

SourceLocation sourceLocation(std::string_view source, std::size_t byteOffset) noexcept {
    SourceLocation result;
    const std::size_t end = std::min(byteOffset, source.size());
    const std::size_t first = source.starts_with("\xEF\xBB\xBF") ? std::min<std::size_t>(3, end) : 0;
    for (std::size_t at = first; at < end; ++at) {
        const auto byte = static_cast<unsigned char>(source[at]);
        if (byte == '\n') { ++result.line; result.column = 1; }
        else if (byte == '\r' && at + 1 < source.size() && source[at + 1] == '\n') {}
        else if ((byte & 0xc0U) != 0x80U) ++result.column;
    }
    return result;
}

std::string_view typeName(Type type) noexcept {
    switch (type) {
    case Type::Invalid: return "invalid";
    case Type::Integer: return "integer";
    case Type::Real: return "real";
    case Type::Pentarnion: return "pentarnion";
    case Type::IntegerRange: return "integer range";
    case Type::RealRange: return "real range";
    case Type::String: return "string";
    case Type::Void: return "void";
    case Type::OdeModel: return "ode";
    case Type::Solution: return "solution";
    case Type::RealSeries: return "real series";
    case Type::PentarnionSeries: return "pentarnion series";
    case Type::ElementSeries: return "algebra element series";
    case Type::RealMatrix: return "realmatrix";
    case Type::PentarnionMatrix: return "pentarnionmatrix";
    case Type::Function:return "function";
    case Type::Complex:return "complex";
    case Type::Record:return "options";
    case Type::Tuple:return "tuple";
    case Type::LogicalMatrix:return "logicalmatrix";
    case Type::Algebra:return "algebra";
    case Type::Element:return "element";
    case Type::ElementMatrix:return "elementmatrix";
    case Type::ArraySeries:return "array series";
    case Type::Tensor:return "tensor";
    }
    return "invalid";
}
} // namespace penta::language
