#include "lexer.hpp"
#include "extension_names.hpp"

#include <algorithm>
#include <array>

namespace penta::language {
namespace {

constexpr std::size_t maximumDiagnostics = 128;

bool asciiDigit(char value) noexcept { return value >= '0' && value <= '9'; }
bool identifierStart(char value) noexcept {
    return (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z') || value == '_';
}
bool identifierPart(char value) noexcept { return identifierStart(value) || asciiDigit(value); }
bool whitespace(char value) noexcept {
    return value == ' ' || value == '\t' || value == '\r' || value == '\n' || value == '\v' || value == '\f';
}

template<std::size_t Size>
bool in(std::string_view value, const std::array<std::string_view, Size>& names) noexcept {
    return std::find(names.begin(), names.end(), value) != names.end();
}

TokenKind identifierKind(std::string_view value) noexcept {
    constexpr std::array algebraKeywords{std::string_view("algebra"),std::string_view("element")};
    constexpr std::array algebraBuiltins{std::string_view("builtin_algebra"),std::string_view("import_algebra"),
        std::string_view("algebra_info"),std::string_view("algebra_basis"),std::string_view("algebra_coefficients"),
        std::string_view("algebra_leftop"),std::string_view("algebra_rightop"),std::string_view("algebra_solveleft"),
        std::string_view("algebra_solveright"),std::string_view("algebra_norm"),std::string_view("algebra_json"),std::string_view("algebra_products"),std::string_view("statecoord")};
    if(in(value,algebraKeywords))return TokenKind::Keyword;
    if(in(value,algebraBuiltins))return TokenKind::Builtin;
    constexpr std::array keywords{std::string_view("integer"), std::string_view("real"),
        std::string_view("pentarnion"), std::string_view("range"), std::string_view("use_algebra"),
        std::string_view("ode"), std::string_view("solution"), std::string_view("matrix"),
        std::string_view("vector"), std::string_view("realmatrix"), std::string_view("pentarnionmatrix"),
        std::string_view("string"),std::string_view("void"),std::string_view("if"),std::string_view("else"),
        std::string_view("for"),std::string_view("while"),std::string_view("break"),std::string_view("continue"),
        std::string_view("return"),std::string_view("function"),std::string_view("true"),std::string_view("false"),std::string_view("end"),std::string_view("callback"),std::string_view("options"),std::string_view("record"),std::string_view("tuple"),std::string_view("complex"),std::string_view("logicalmatrix"),std::string_view("import"),std::string_view("as"),std::string_view("native")};
    constexpr std::array bases{std::string_view("i"), std::string_view("j"),
        std::string_view("k"), std::string_view("ell")};
    constexpr std::array builtins{std::string_view("sqrt"), std::string_view("pent"),
        std::string_view("conj"), std::string_view("scalar"), std::string_view("pure"),
        std::string_view("norm2"), std::string_view("inv"), std::string_view("coeff"),
        std::string_view("commutator"), std::string_view("associator"), std::string_view("display"),
        std::string_view("clear"), std::string_view("clc"), std::string_view("plot2"),
        std::string_view("plot3"), std::string_view("figure"), std::string_view("clf"),
        std::string_view("close"), std::string_view("sin"), std::string_view("cos"),
        std::string_view("exp"), std::string_view("cosh"), std::string_view("sinh"),
        std::string_view("tan"), std::string_view("asin"), std::string_view("acos"),
        std::string_view("atan"), std::string_view("atan2"), std::string_view("tanh"),
        std::string_view("asinh"), std::string_view("acosh"), std::string_view("atanh"),
        std::string_view("log"), std::string_view("log10"), std::string_view("log2"),
        std::string_view("abs"), std::string_view("floor"), std::string_view("ceil"), std::string_view("round"),
        std::string_view("dock"), std::string_view("undock"), std::string_view("tilefigures"),
        std::string_view("title"), std::string_view("xlabel"), std::string_view("ylabel"),
        std::string_view("zlabel"), std::string_view("caption"), std::string_view("odesolve"),
        std::string_view("subplot"), std::string_view("plot"), std::string_view("tic"), std::string_view("toc"),
        std::string_view("zeros"), std::string_view("ones"), std::string_view("eye"),
        std::string_view("rand"), std::string_view("randn"), std::string_view("randi"),
        std::string_view("rng"), std::string_view("size"), std::string_view("numel"),
        std::string_view("length"), std::string_view("transpose"), std::string_view("linspace"),
        std::string_view("sum"),std::string_view("min"),std::string_view("max"),std::string_view("mean"),
        std::string_view("assert"),std::string_view("error"),std::string_view("warning"),std::string_view("help"),
        std::string_view("who"),std::string_view("whos"),std::string_view("save"),std::string_view("load"),
        std::string_view("readmatrix"),std::string_view("writematrix"),std::string_view("csvread"),
        std::string_view("csvwrite"),std::string_view("xlsread"),std::string_view("xlswrite"),std::string_view("complex"),std::string_view("realpart"),std::string_view("imagpart"),std::string_view("arg"),std::string_view("with_options"),std::string_view("get_option"),std::string_view("reshape"),std::string_view("diag"),std::string_view("meshgrid"),std::string_view("ndgrid"),std::string_view("find"),std::string_view("logical"),std::string_view("dot"),std::string_view("norm"),std::string_view("interp1"),std::string_view("trapz"),std::string_view("cumtrapz"),std::string_view("diff"),std::string_view("gradient"),std::string_view("divergence"),std::string_view("laplacian"),std::string_view("deval"),std::string_view("integral"),std::string_view("threads"),std::string_view("parallel_info"),std::string_view("expm1"),std::string_view("log1p"),std::string_view("erf"),std::string_view("erfc"),std::string_view("hypot"),std::string_view("sign"),std::string_view("axpy"),std::string_view("matmul"),std::string_view("batchmul"),std::string_view("det"),std::string_view("leftop"),std::string_view("rightop"),std::string_view("solveleft"),std::string_view("solveright"),std::string_view("linsolve"),std::string_view("solve_report"),std::string_view("lu"),std::string_view("qr"),std::string_view("chol"),std::string_view("svd"),std::string_view("eig"),std::string_view("schur"),std::string_view("rank"),std::string_view("rcond"),std::string_view("cond"),std::string_view("sparse"),std::string_view("full"),std::string_view("nnz"),std::string_view("sparsemul"),std::string_view("sparsetranspose"),std::string_view("sparsesolve"),std::string_view("sparse_solve_report"),std::string_view("jacobian"),std::string_view("hessian"),std::string_view("jvp"),std::string_view("vjp"),std::string_view("hvp"),std::string_view("derivative_check"),std::string_view("thirdderivative"),std::string_view("root"),std::string_view("rootsolve"),std::string_view("equilibrium"),std::string_view("fixedpoint"),std::string_view("lookfor"),std::string_view("curvefit"),std::string_view("lsqfit"),std::string_view("lsqnonlin"),std::string_view("odefit"),std::string_view("minimize"),std::string_view("odeevents"),std::string_view("odesens"),std::string_view("odestats"),std::string_view("odewindow"),std::string_view("odeadjoint"),std::string_view("mapiterate"),std::string_view("mapdiagram"),std::string_view("returnmap"),std::string_view("compile_info"),std::string_view("pdesolve"),std::string_view("pdecoeff"),std::string_view("lyapunov"),std::string_view("poincare"),std::string_view("recurrence"),std::string_view("stability"),std::string_view("ensemble"),std::string_view("surf"),std::string_view("imagesc"),std::string_view("contour"),std::string_view("isosurface"),std::string_view("curvegeometry"),std::string_view("evolve"),std::string_view("fft"),std::string_view("ifft"),std::string_view("pwelch"),std::string_view("autocorr"),std::string_view("findpeaks"),std::string_view("variance"),std::string_view("stddev"),std::string_view("covariance"),std::string_view("quantile"),std::string_view("histogram"),std::string_view("normpdf"),std::string_view("normcdf"),std::string_view("norminv"),std::string_view("bootstrap"),std::string_view("sym"),std::string_view("symdiff"),std::string_view("symsubs"),std::string_view("symsimplify"),std::string_view("symnumeric"),std::string_view("symtext"),std::string_view("analysisrecord"),std::string_view("saveanalysis"),std::string_view("loadanalysis"),std::string_view("iterationinfo"),std::string_view("pdeensemble"),std::string_view("pdeensemble2"),std::string_view("pderun"),std::string_view("pdeparams"),std::string_view("pdesettings"),std::string_view("pdereplay"),std::string_view("graphsparse"),std::string_view("graphgen"),std::string_view("networksolve"),std::string_view("networkcoeff"),std::string_view("meshplane"),std::string_view("meshsphere"),std::string_view("meshcylinder"),std::string_view("meshcircle"),std::string_view("paramesh"),std::string_view("trimesh"),std::string_view("meshinfo"),std::string_view("meshtransform"),std::string_view("meshrefine"),std::string_view("meshplot"),std::string_view("meshread"),std::string_view("meshwrite"),std::string_view("lineplane"),std::string_view("raysphere"),std::string_view("dform"),std::string_view("formfield"),std::string_view("wedge"),std::string_view("exteriord"),std::string_view("hodge"),std::string_view("laplacebeltrami"),std::string_view("surfaceheat"),std::string_view("surfacecoeff"),std::string_view("savegraphlayout"),std::string_view("loadgraphlayout"),std::string_view("exportvideo"),std::string_view("profilefit"),std::string_view("fitbootstrap"),std::string_view("multistartfit"),std::string_view("ndarray"),std::string_view("complexarray"),std::string_view("pentarray"),std::string_view("ndinfo"),std::string_view("ndreshape"),std::string_view("ndpermute"),std::string_view("ndslice"),std::string_view("ndadd"),std::string_view("ndsub"),std::string_view("ndmul"),std::string_view("nddiv"),std::string_view("ndmatmul"),std::string_view("ndconj"),std::string_view("ndabs"),std::string_view("ndreal"),std::string_view("ndimag"),std::string_view("ndcoeff"),std::string_view("ndfft"),std::string_view("ndifft"),std::string_view("ndvalues"),std::string_view("ndplot"),std::string_view("ndmap"),std::string_view("ndget"),std::string_view("ndset"),std::string_view("ndinterp"),std::string_view("quantity"),std::string_view("unitconvert"),std::string_view("unitadd"),std::string_view("unitsub"),std::string_view("unitmul"),std::string_view("unitdiv"),std::string_view("unitpow"),std::string_view("unitvalues"),std::string_view("unitcompatible"),std::string_view("unitinfo"),std::string_view("constrainedmechanics"),std::string_view("modelsensitivity"),std::string_view("mechanics"),std::string_view("symexpand"),std::string_view("symcoeff"),std::string_view("sympentdiff"),std::string_view("sympentsubs"),std::string_view("sympentnumeric"),std::string_view("sympenttext"),std::string_view("sympentcode"),std::string_view("daesolve"),std::string_view("animatefield"),std::string_view("animationframe"),std::string_view("playanimation"),std::string_view("exportframes"),std::string_view("provenance"),std::string_view("researchrecord"),std::string_view("sensitivity"),std::string_view("exportexperiment"),std::string_view("chartgeometry"),std::string_view("geodesic"),std::string_view("hold"),std::string_view("legend"),std::string_view("plotstyle"),std::string_view("xlim"),std::string_view("ylim"),std::string_view("zlim"),std::string_view("axis"),std::string_view("linkaxes"),std::string_view("exportgraphics"),std::string_view("searchset"),std::string_view("searchinterval"),std::string_view("pentdirections"),std::string_view("femsolve"),std::string_view("femplot"),std::string_view("refinebifurcation"),std::string_view("saveexperiment"),std::string_view("loadexperiment"),std::string_view("experimentinfo"),std::string_view("experimentsource"),std::string_view("hybridsolve"),std::string_view("hybridval"),std::string_view("hybridadjoint"),std::string_view("verifyexperiment"),std::string_view("verifyrun"),std::string_view("discoverplan"),std::string_view("discover"),std::string_view("odeensemble"),std::string_view("discoverstatus"),std::string_view("experimentrun"),std::string_view("periodicorbit"),std::string_view("continueequilibrium"),std::string_view("continueperiodic"),std::string_view("graph"),std::string_view("digraph"),std::string_view("graphinfo"),std::string_view("graphcomponents"),std::string_view("components"),std::string_view("shortestpath"),std::string_view("spanningtree"),std::string_view("toposort"),std::string_view("centrality"),std::string_view("graphlaplacian"),std::string_view("graphadjacency"),std::string_view("graphlayout"),std::string_view("graphcycle"),std::string_view("plotgraph"),std::string_view("pdesolve2"),std::string_view("pdeslice")};
    if (in(value, keywords)) return TokenKind::Keyword;
    if (in(value, bases)) return TokenKind::Basis;
    if (in(value, builtins)||extensionBuiltin(value)) return TokenKind::Builtin;
    return TokenKind::Identifier;
}

// Return the length of a valid UTF-8 scalar, or zero for an invalid sequence.
// Reject overlong forms, surrogate code points and values above U+10FFFF.
std::size_t utf8Length(std::string_view source, std::size_t at) noexcept {
    const auto first = static_cast<unsigned char>(source[at]);
    if (first < 0x80) return 1;
    std::size_t length = 0;
    if (first >= 0xc2 && first <= 0xdf) length = 2;
    else if (first >= 0xe0 && first <= 0xef) length = 3;
    else if (first >= 0xf0 && first <= 0xf4) length = 4;
    else return 0;
    if (length > source.size() - at) return 0;
    for (std::size_t index = 1; index < length; ++index) {
        const auto byte = static_cast<unsigned char>(source[at + index]);
        if (byte < 0x80 || byte > 0xbf) return 0;
    }
    const auto second = static_cast<unsigned char>(source[at + 1]);
    if ((first == 0xe0 && second < 0xa0) || (first == 0xed && second >= 0xa0) ||
        (first == 0xf0 && second < 0x90) || (first == 0xf4 && second >= 0x90)) return 0;
    return length;
}

void diagnostic(LexResult& result, std::string_view code, std::string_view message,
                std::size_t offset, std::size_t length) {
    if (result.diagnostics.size() < maximumDiagnostics)
        result.diagnostics.push_back({std::string(code), std::string(message), {offset, length}});
}

void validateUtf8(LexResult& result, std::string_view line, std::size_t begin,
                  std::size_t end, std::size_t baseOffset) {
    for (std::size_t at = begin; at < end;) {
        const std::size_t length = utf8Length(line, at);
        if (length == 0) {
            diagnostic(result, "E1001", "Invalid UTF-8 byte sequence.", baseOffset + at, 1);
            ++at;
        } else at += length;
    }
}

} // namespace

LexResult lexLine(std::string_view line, LexerState initial, std::size_t baseOffset) {
    LexResult result;
    result.state = initial;
    if (!line.empty() && line.back() == '\n') line.remove_suffix(1);
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    auto token = [&](TokenKind kind, std::size_t begin, std::size_t end) {
        if (end > begin) result.tokens.push_back({kind, {baseOffset + begin, end - begin}});
    };

    // A BOM is encoding metadata only at the beginning of the source buffer.
    const std::size_t contentStart = baseOffset == 0 && !initial.inBlockComment &&
        line.starts_with("\xEF\xBB\xBF") ? 3 : 0;
    std::size_t first = contentStart;
    while (first < line.size() && whitespace(line[first])) ++first;
    std::size_t last = line.size();
    while (last > first && whitespace(line[last - 1])) --last;
    const std::string_view trimmed = line.substr(first, last - first);
    if (result.state.inBlockComment) {
        token(TokenKind::Comment, 0, line.size());
        validateUtf8(result, line, 0, line.size(), baseOffset);
        if (trimmed == "%}") result.state.inBlockComment = false;
        return result;
    }
    if (trimmed == "%{") {
        token(TokenKind::Comment, first, line.size());
        result.state.inBlockComment = true;
        result.state.blockCommentStart = baseOffset + first;
        return result;
    }

    for (std::size_t at = contentStart; at < line.size();) {
        const char current = line[at];
        if (whitespace(current)) { ++at; continue; }
        if (current == '%') {
            token(TokenKind::Comment, at, line.size());
            validateUtf8(result, line, at, line.size(), baseOffset);
            break;
        }
        if (identifierStart(current)) {
            const std::size_t begin = at++;
            while (at < line.size() && identifierPart(line[at])) ++at;
            token(identifierKind(line.substr(begin, at - begin)), begin, at);
            continue;
        }
        if (asciiDigit(current) || (current == '.' && at + 1 < line.size() && asciiDigit(line[at + 1]))) {
            const std::size_t begin = at;
            bool real = false;
            bool malformed = false;
            while (at < line.size() && asciiDigit(line[at])) ++at;
            if (at < line.size() && line[at] == '.' &&
                !(at + 1 < line.size() && std::string_view("*/^'").find(line[at + 1]) != std::string_view::npos)) {
                real = true;
                ++at;
                while (at < line.size() && asciiDigit(line[at])) ++at;
            }
            if (at < line.size() && (line[at] == 'e' || line[at] == 'E')) {
                real = true;
                ++at;
                if (at < line.size() && (line[at] == '+' || line[at] == '-')) ++at;
                const std::size_t exponentStart = at;
                while (at < line.size() && asciiDigit(line[at])) ++at;
                malformed = exponentStart == at;
            }
            if (at < line.size() && identifierStart(line[at])) {
                while (at < line.size() && identifierPart(line[at])) ++at;
                malformed = true;
            }
            if (malformed) {
                token(TokenKind::Error, begin, at);
                diagnostic(result, "E1001", "Invalid numeric token or implicit multiplication; use an explicit '*'.",
                           baseOffset + begin, at - begin);
            } else token(real ? TokenKind::Real : TokenKind::Integer, begin, at);
            continue;
        }
        if (current == '"') {
            const std::size_t begin = at++;
            bool closed = false;
            while (at < line.size()) {
                if (line[at] == '"') { ++at; closed = true; break; }
                if (line[at] == '\n' || line[at] == '\r') break;
                if (line[at] == '\\') {
                    const std::size_t escape = at++;
                    if (at == line.size()) break;
                    if (std::string_view("\\\"nrt").find(line[at]) == std::string_view::npos)
                        diagnostic(result, "E1002", "Unsupported string escape; use \\\\, \\\" , \\n, \\r or \\t.",
                                   baseOffset + escape, 2);
                    ++at;
                    continue;
                }
                const std::size_t length = utf8Length(line, at);
                if (length == 0) {
                    diagnostic(result, "E1001", "Invalid UTF-8 byte sequence in string.", baseOffset + at, 1);
                    ++at;
                } else if (static_cast<unsigned char>(line[at]) < 32 && line[at] != '\t') {
                    diagnostic(result, "E1001", "Unsupported control character in string.", baseOffset + at, length);
                    at += length;
                } else at += length;
            }
            // Keep incomplete strings styled as strings; diagnostics carry errors.
            token(TokenKind::String, begin, at);
            if (!closed)
                diagnostic(result, "E1002", "Unterminated string literal.", baseOffset + begin, at - begin);
            continue;
        }
        if (current == '.' && at + 1 < line.size() &&
            std::string_view("*/^'").find(line[at + 1]) != std::string_view::npos) {
            token(TokenKind::Operator, at, at + 2);
            at += 2;
            continue;
        }
        if (at+1<line.size()) {
            const auto pair=line.substr(at,2);
            if(pair=="=>" || pair=="==" || pair=="!=" || pair=="<=" || pair==">=" || pair=="&&" || pair=="||" || pair=="~=") {
                token(TokenKind::Operator,at,at+2);at+=2;continue;
            }
        }
        if (std::string_view("+-*/\\^=<>!~@&|").find(current) != std::string_view::npos) {
            token(TokenKind::Operator, at, at + 1);
            ++at;
            continue;
        }
        if (std::string_view("()[]{};:,.'").find(current) != std::string_view::npos) {
            token(TokenKind::Punctuation, at, at + 1);
            ++at;
            continue;
        }
        const std::size_t length = std::max<std::size_t>(1, utf8Length(line, at));
        token(TokenKind::Error, at, at + length);
        diagnostic(result, "E1001", "Unsupported source character; identifiers must use ASCII letters, digits and '_'.",
                   baseOffset + at, length);
        at += length;
    }
    return result;
}

LexResult lex(std::string_view source) {
    LexResult result;
    std::size_t at = 0;
    while (at < source.size()) {
        const std::size_t next = source.find('\n', at);
        const std::size_t end = next == std::string_view::npos ? source.size() : next + 1;
        LexResult line = lexLine(source.substr(at, end - at), result.state, at);
        result.state = line.state;
        result.tokens.insert(result.tokens.end(), line.tokens.begin(), line.tokens.end());
        for (auto& item : line.diagnostics) {
            if (result.diagnostics.size() >= maximumDiagnostics) break;
            result.diagnostics.push_back(std::move(item));
        }
        at = end;
    }
    if (result.state.inBlockComment)
        diagnostic(result, "E1003", "Unterminated block comment.", result.state.blockCommentStart, 2);
    result.tokens.push_back({TokenKind::EndOfFile, {source.size(), 0}});
    return result;
}

std::string_view tokenKindName(TokenKind kind) noexcept {
    switch (kind) {
    case TokenKind::Keyword: return "keyword";
    case TokenKind::Basis: return "basis";
    case TokenKind::Builtin: return "builtin";
    case TokenKind::Identifier: return "identifier";
    case TokenKind::Integer: return "integer";
    case TokenKind::Real: return "real";
    case TokenKind::String: return "string";
    case TokenKind::Comment: return "comment";
    case TokenKind::Operator: return "operator";
    case TokenKind::Punctuation: return "punctuation";
    case TokenKind::Error: return "error";
    case TokenKind::EndOfFile: return "eof";
    }
    return "error";
}

} // namespace penta::language
