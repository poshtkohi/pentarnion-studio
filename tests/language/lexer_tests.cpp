#include "language/lexer.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace penta::language;

#ifndef PENTA_SOURCE_DIR
#define PENTA_SOURCE_DIR "."
#endif

namespace {
int failures = 0;
int assertions = 0;

void require(bool condition, std::string_view message) {
    ++assertions;
    if (!condition) {
        ++failures;
        if (failures < 30) std::cerr << "FAIL: " << message << '\n';
    }
}

bool hasCode(const LexResult& result, std::string_view code) {
    return std::any_of(result.diagnostics.begin(), result.diagnostics.end(),
                       [code](const auto& item) { return item.code == code; });
}

void spansAreValid(const LexResult& result, std::string_view source) {
    require(!result.tokens.empty(), "full lex includes EOF");
    std::size_t previousEnd = 0;
    for (const auto& token : result.tokens) {
        require(token.span.offset >= previousEnd, "tokens retain source order and do not overlap");
        require(token.span.offset <= source.size(), "token starts inside source");
        require(token.span.length <= source.size() - std::min(token.span.offset, source.size()),
                "token ends inside source");
        require(token.span.length > 0 || token.kind == TokenKind::EndOfFile,
                "only EOF is zero width");
        previousEnd = token.span.offset + token.span.length;
    }
    require(result.tokens.back().kind == TokenKind::EndOfFile, "last token is EOF");
    require(result.tokens.back().span.offset == source.size(), "EOF points after final source byte");
    require(result.diagnostics.size() <= 128, "diagnostics have a finite budget");
    require(result.tokens.size() <= source.size() + 1, "token count is bounded by input bytes");
    for (const auto& item : result.diagnostics) {
        require(item.span.offset <= source.size(), "diagnostic starts inside source");
        require(item.span.length <= source.size() - std::min(item.span.offset, source.size()),
                "diagnostic ends inside source");
    }
}

void sameFullAndLineTokens(std::string_view source) {
    const auto full = lex(source);
    std::vector<Token> lineTokens;
    LexerState state;
    for (std::size_t at = 0; at < source.size();) {
        const std::size_t next = source.find('\n', at);
        const std::size_t end = next == std::string_view::npos ? source.size() : next + 1;
        auto line = lexLine(source.substr(at, end - at), state, at);
        state = line.state;
        lineTokens.insert(lineTokens.end(), line.tokens.begin(), line.tokens.end());
        at = end;
    }
    require(lineTokens.size() + 1 == full.tokens.size(), "full and resumed line token counts agree");
    for (std::size_t index = 0; index < std::min(lineTokens.size(), full.tokens.size()); ++index) {
        require(lineTokens[index].kind == full.tokens[index].kind, "full and resumed line token kinds agree");
        require(lineTokens[index].span.offset == full.tokens[index].span.offset, "full and line byte starts agree");
        require(lineTokens[index].span.length == full.tokens[index].span.length, "full and line byte lengths agree");
    }
    require(state.inBlockComment == full.state.inBlockComment, "full and line final comment states agree");
}

TokenKind goldenKind(char prefix) {
    switch (prefix) {
    case 'K': return TokenKind::Keyword;
    case 'B': return TokenKind::Basis;
    case 'F': return TokenKind::Builtin;
    case 'I': return TokenKind::Identifier;
    case 'N': return TokenKind::Integer;
    case 'R': return TokenKind::Real;
    case 'S': return TokenKind::String;
    case 'O': return TokenKind::Operator;
    case 'P': return TokenKind::Punctuation;
    default: return TokenKind::Error;
    }
}

// These are explicit expected token spellings and kinds, written independently
// of the lexer. Comments are checked separately as preserved source spans.
void golden(std::string_view label, const std::string& source, std::string_view expected) {
    const auto result = lex(source);
    require(result.diagnostics.empty(), std::string(label) + ": valid source has no lexical diagnostics");
    std::vector<Token> code;
    for (const auto& token : result.tokens) {
        if (token.kind == TokenKind::Comment) {
            const auto text = std::string_view(source).substr(token.span.offset, token.span.length);
            require(!text.empty() && text.front() == '%', "example comments are retained whole");
        } else if (token.kind != TokenKind::EndOfFile) code.push_back(token);
    }
    std::istringstream stream{std::string(expected)};
    std::string item;
    std::size_t at = 0;
    while (stream >> item) {
        require(at < code.size(), std::string(label) + ": enough code tokens");
        if (at >= code.size()) break;
        const auto& token = code[at++];
        require(token.kind == goldenKind(item.front()), std::string(label) + ": golden token kind for " + item);
        require(std::string_view(source).substr(token.span.offset, token.span.length) == std::string_view(item).substr(1),
                std::string(label) + ": golden spelling for " + item);
    }
    require(at == code.size(), std::string(label) + ": golden stream has all code tokens");
    spansAreValid(result, source);
    sameFullAndLineTokens(source);
}

std::string readExample(std::string_view relative) {
    const auto path = std::filesystem::path(PENTA_SOURCE_DIR) / std::string(relative);
    std::ifstream file(path, std::ios::binary);
    require(file.good(), std::string("example is readable: ") + std::string(relative));
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

void exampleGoldens() {
    golden("basics", readExample("examples/basics.p"),
        "Kuse_algebra P( S\"A_gamma\" P, Igamma O= N1 P) P; "
        "Kreal Im O= R1.0 P; Kinteger In O= N3 P; "
        "Kpentarnion Ip O= N1 O+ N2 O* Bi O- Bj O+ Bell P; "
        "Kpentarnion Iq O= Fpent P( P[ N0 P, N1 P, O- N1 P, N0 P, N1 P] P) P; "
        "Kpentarnion Iproduct O= Ip O* Iq P; "
        "Kpentarnion Iinverse_p O= Ip O^ P( O- N1 P) P; "
        "Kreal Imagnitude_squared O= Fnorm2 P( Ip P) P; "
        "Kinteger Krange Iexponents O= N1 P: N1 P: N5 P; "
        "Kreal Krange Istrengths O= R0.05 P: R0.05 P: R0.50 P;");
    golden("grouping", readExample("examples/grouping.p"),
        "Kpentarnion Ip O= Bi P; Kpentarnion Iq O= Bj P; Kpentarnion Ir O= Bj P; "
        "Kpentarnion Ileft O= P( Ip O* Iq P) O* Ir P; "
        "Kpentarnion Iright O= Ip O* P( Iq O* Ir P) P; "
        "Kpentarnion Idefect O= Ileft O- Iright P; "
        "Kpentarnion Ireciprocal_product O= Ip O* Finv P( Iq P) P; "
        "Kpentarnion Iequation_quotient O= Ip O/ Iq P;");
    golden("quaternion sector", readExample("examples/quaternion_sector.p"),
        "Kuse_algebra P( S\"A_gamma\" P, Igamma O= N0 P) P; "
        "Kpentarnion Ip O= N1 O+ Bi P; Kpentarnion Iq O= Bj O- Bk P; "
        "Kpentarnion Ir O= N2 O- Bi O+ Bj P; "
        "Kpentarnion Ia1 O= P( Ip O* Iq P) O* Ir P; "
        "Kpentarnion Ia2 O= Ip O* P( Iq O* Ir P) P;");
    golden("ranges", readExample("examples/ranges.p"),
        "Kinteger Krange Icounts O= N1 P: N1 P: N10 P; "
        "Kinteger Krange Ireverse O= N10 P: O- N1 P: N1 P; "
        "Kinteger Krange Icopy_counts O= Icounts P; "
        "Kreal Krange Ib_values O= R0.05 P: R0.05 P: R0.5 P; "
        "Kreal Krange Idecreasing O= R1.0 P: O- R0.2 P: R0.0 P;");
    golden("first project main", readExample("examples/first-project/main.p"),
        "Kuse_algebra P( S\"A_gamma\" P, Igamma O= N1 P) P; "
        "Kreal Istrength O= R0.25 P; "
        "Kpentarnion Ip O= N1 O+ Bi O- Bj O+ Bell P; "
        "Kpentarnion Iq O= Fpent P( P[ N0 P, N1 P, O- N1 P, N0 P, N1 P] P) P; "
        "Kpentarnion Iproduct O= Ip O* Iq P;");
    golden("first project grouping", readExample("examples/first-project/models/grouping.p"),
        "Kpentarnion Ip O= Bi P; Kpentarnion Iq O= Bj P; "
        "Kpentarnion Ileft O= P( Ip O* Iq P) O* Iq P; "
        "Kpentarnion Iright O= Ip O* P( Iq O* Iq P) P; "
        "Kpentarnion Ireciprocal_product O= Ip O* Finv P( Iq P) P; "
        "Kpentarnion Iequation_quotient O= Ip O/ Iq P;");
}

void categoriesAndNumericBoundaries() {
    golden("reserved names", "integer real pentarnion range use_algebra i j k ell sqrt pent conj scalar pure norm2 inv coeff commutator associator display clear clc plot2 plot3 figure clf close dock undock tilefigures title xlabel ylabel zlabel caption sin cos exp sinh cosh tan asin acos atan atan2 tanh asinh acosh atanh log log10 log2 abs floor ceil round Integer ell2 _i gamma e for function plot search simulate",
        "Kinteger Kreal Kpentarnion Krange Kuse_algebra Bi Bj Bk Bell Fsqrt Fpent Fconj Fscalar Fpure Fnorm2 Finv Fcoeff Fcommutator Fassociator Fdisplay Fclear Fclc Fplot2 Fplot3 Ffigure Fclf Fclose Fdock Fundock Ftilefigures Ftitle Fxlabel Fylabel Fzlabel Fcaption Fsin Fcos Fexp Fsinh Fcosh Ftan Fasin Facos Fatan Fatan2 Ftanh Fasinh Facosh Fatanh Flog Flog10 Flog2 Fabs Ffloor Fceil Fround IInteger Iell2 I_i Igamma Ie Kfor Kfunction Fplot Isearch Isimulate");
    golden("native ODE", "ode flow(t,pentarnion p;real a){p'=a*p;} solution s=odesolve(flow,p=i,a=1,t0=0,t1=1); plot(s.t,coeff(s.p,0)); subplot(1,2,1);",
        "Kode Iflow P( It P, Kpentarnion Ip P; Kreal Ia P) P{ Ip P' O= Ia O* Ip P; P} Ksolution Is O= Fodesolve P( Iflow P, Ip O= Bi P, Ia O= N1 P, It0 O= N0 P, It1 O= N1 P) P; Fplot P( Is P. It P, Fcoeff P( Is P. Ip P, N0 P) P) P; Fsubplot P( N1 P, N2 P, N1 P) P;");
    golden("timing", "tic; tic(); toc; toc(); real elapsed=toc();", "Ftic P; Ftic P( P) P; Ftoc P; Ftoc P( P) P; Kreal Ielapsed O= Ftoc P( P) P;");
    golden("numbers", "0 12 1.0 .5 1. 2e-3 4E+7 -9223372036854775808 9223372036854775808 1e9999 q^-1 p\\q",
        "N0 N12 R1.0 R.5 R1. R2e-3 R4E+7 O- N9223372036854775808 N9223372036854775808 R1e9999 Iq O^ O- N1 Ip O\\ Iq");
    for (const auto* source : {"2i", "2ell", "0x10", "1e", "1e+", "1e-2q"}) {
        const auto result = lex(source);
        require(hasCode(result, "E1001"), std::string("invalid numeric token: ") + source);
        require(result.tokens.front().kind == TokenKind::Error, "invalid numeric token is not a valid number");
        spansAreValid(result, source);
    }
    const auto unsupported = lex("' @ α");
    require(hasCode(unsupported, "E1001"), "single quote, unsupported characters and Unicode identifiers are invalid");
    require(unsupported.tokens[2].span.length == 2, "one Unicode scalar keeps its full UTF-8 byte span");
}

void stringsAndComments() {
    const std::string valid = R"p("A_gamma" "λ % integer i" "\\\"\n\r\t")p";
    const auto strings = lex(valid);
    require(strings.diagnostics.empty(), "all supported escapes and Unicode string contents are valid");
    require(strings.tokens.size() == 4, "three whole string tokens plus EOF");
    for (std::size_t at = 0; at < 3; ++at) require(strings.tokens[at].kind == TokenKind::String, "strings do not expose fake keyword tokens");
    const auto badEscape = lex(R"p("bad\q")p");
    require(hasCode(badEscape, "E1002"), "unknown escape has E1002");
    require(badEscape.tokens.front().kind == TokenKind::String, "bad escape remains styled as a string");
    const auto unfinished = lex("\"unfinished % integer");
    require(hasCode(unfinished, "E1002"), "unfinished string has E1002");
    require(unfinished.tokens.front().kind == TokenKind::String, "unfinished string remains styled as a string");
    const auto newline = lex("\"unfinished\nreal value=1;");
    require(hasCode(newline, "E1002"), "physical newline terminates an invalid string");
    require(std::any_of(newline.tokens.begin(), newline.tokens.end(), [](const auto& token) { return token.kind == TokenKind::Keyword; }),
            "an unfinished string never consumes the following source line");

    const std::string block = " %\x7b\r\n integer i = \"\\q\"; λ 😀\r\n%\x7b\r\n text\r\n %} \r\nreal x = .5; % ordinary\r\n";
    const auto commentResult = lex(block);
    require(commentResult.diagnostics.empty(), "non-nesting block comments ignore contained code and escapes");
    require(std::count_if(commentResult.tokens.begin(), commentResult.tokens.end(), [](const auto& token) { return token.kind == TokenKind::Comment; }) == 6,
            "all six comment spans are preserved");
    require(std::count_if(commentResult.tokens.begin(), commentResult.tokens.end(), [](const auto& token) { return token.kind == TokenKind::Keyword; }) == 1,
            "only the real declaration outside comments is a keyword");
    require(!commentResult.state.inBlockComment, "closing standalone delimiter resumes source");
    spansAreValid(commentResult, block);
    sameFullAndLineTokens(block);
    for (const auto* source : {"%{ not a block\nreal x=1;", "real x=1; %{\nreal y=2;", "%}\nreal z=3;"}) {
        const auto result = lex(source);
        require(!result.state.inBlockComment, "only otherwise-whitespace opening delimiter lines start a block");
        require(result.diagnostics.empty(), "ordinary percent line comments remain ordinary comments");
    }
    const std::string missing = "%{\nnot closed %} text";
    const auto missingResult = lex(missing);
    require(hasCode(missingResult, "E1003"), "missing standalone block close has E1003");
    require(missingResult.state.inBlockComment, "unfinished block carries resumable state");
    require(missingResult.diagnostics.front().span.offset == 0 && missingResult.diagnostics.front().span.length == 2,
            "unterminated-block diagnostic points at opening delimiter");
    const auto open = lexLine("  %{", {}, 12);
    require(open.state.inBlockComment && open.state.blockCommentStart == 14, "line API carries opening byte offset");
    require(open.diagnostics.empty(), "line API does not prematurely flag a continued block");
    const auto continuation = lexLine("real integer i (", open.state, 20);
    require(continuation.tokens.size() == 1 && continuation.tokens.front().kind == TokenKind::Comment,
            "continued comment uses the same categories for IDE styling");
    const auto close = lexLine("\t%}  ", continuation.state, 40);
    require(!close.state.inBlockComment, "line API resumes after whitespace-only closing delimiter");
}

void unicodeCrLfAndBom() {
    const std::string source = "% λ 😀\r\nreal x = $;\r\n";
    const auto result = lex(source);
    require(result.diagnostics.size() == 1, "one error after a Unicode comment");
    require(!result.diagnostics.empty() && result.diagnostics.front().span.offset == source.find('$'), "Unicode/CRLF do not shift error byte offset");
    require(result.tokens.front().span.length == source.find('\r'), "comment span excludes CRLF terminator");
    sameFullAndLineTokens(source);
    spansAreValid(result, source);

    const std::string bom = std::string("\xEF\xBB\xBF") + "real x=1;\r\n";
    const auto bomResult = lex(bom);
    require(bomResult.diagnostics.empty(), "leading UTF-8 BOM is accepted as encoding metadata");
    require(bomResult.tokens.front().span.offset == 3, "BOM does not change original source byte offsets");
    require(bomResult.tokens.front().kind == TokenKind::Keyword, "first token after BOM is keyword");
    sameFullAndLineTokens(bom);
    sameFullAndLineTokens(std::string("\xEF\xBB\xBF") + "%{\ncomment\n%}\n");
    require(lex("\xEF\xBB\xBF").tokens.size() == 1, "a BOM-only buffer has only EOF");
    for (const auto& invalid : {std::string("\xc0\x80", 2), std::string("\xed\xa0\x80", 3),
                                std::string("\xf4\x90\x80\x80", 4), std::string("\xe2", 1)}) {
        require(hasCode(lex(invalid), "E1001"), "invalid UTF-8 source is diagnosed");
        require(hasCode(lex("% " + invalid), "E1001"), "invalid UTF-8 in a comment is diagnosed");
        require(hasCode(lex("\"" + invalid + "\""), "E1001"), "invalid UTF-8 in a string is diagnosed");
    }
}

void boundedInvalidInput() {
    golden("matrix operators", "matrix A=[1,2;3,4]; A=1.*A./2.^3; A.'; A'; randn(2); linspace(0,1,2);",
        "Kmatrix IA O= P[ N1 P, N2 P; N3 P, N4 P] P; IA O= N1 O.* IA O./ N2 O.^ N3 P; IA O.' P; IA P' P; Frandn P( N2 P) P; Flinspace P( N0 P, N1 P, N2 P) P;");
    const std::string manyErrors(2000, '$');
    const auto result = lex(manyErrors);
    require(result.diagnostics.size() == 128, "large invalid source caps diagnostic allocation");
    require(result.tokens.size() == 2001, "diagnostic cap does not prevent reaching EOF");
    std::uint32_t random = 0x71a53c29U;
    auto next = [&random] {
        random ^= random << 13;
        random ^= random >> 17;
        random ^= random << 5;
        return random;
    };
    for (int iteration = 0; iteration < 2000; ++iteration) {
        const std::size_t length = next() % 129;
        std::string source;
        for (std::size_t index = 0; index < length; ++index) source.push_back(static_cast<char>(next() & 0xff));
        const auto fuzzed = lex(source);
        spansAreValid(fuzzed, source);
        sameFullAndLineTokens(source);
    }
    for (const auto* incomplete : {"", "p=(i*", "\"", "\"\\", "%{", "1e-", "[", "\\"}) {
        spansAreValid(lex(incomplete), incomplete);
        sameFullAndLineTokens(incomplete);
    }
}

} // namespace

int main() {
    exampleGoldens();
    categoriesAndNumericBoundaries();
    stringsAndComments();
    unicodeCrLfAndBom();
    boundedInvalidInput();
    if (failures) {
        std::cerr << failures << " assertion(s) failed from " << assertions << " checks.\n";
        return 1;
    }
    std::cout << "Lexer tests passed: " << assertions << " checks, six example goldens, 2000 deterministic invalid-byte cases.\n";
    return 0;
}
