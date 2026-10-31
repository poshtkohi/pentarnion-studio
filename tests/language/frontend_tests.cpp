#include "language/frontend.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#ifndef PENTA_SOURCE_DIR
#define PENTA_SOURCE_DIR "."
#endif

using namespace penta::language;

namespace {
int checks = 0;
int failures = 0;

void require(bool value, std::string_view reason) {
    ++checks;
    if (!value) {
        ++failures;
        if (failures < 40) std::cerr << "FAIL: " << reason << '\n';
    }
}

bool code(const AnalysisResult& result, std::string_view expected) {
    return std::any_of(result.diagnostics.begin(), result.diagnostics.end(),
        [expected](const auto& item) { return item.code == expected; });
}

std::size_t countCode(const AnalysisResult& result, std::string_view expected) {
    return static_cast<std::size_t>(std::count_if(result.diagnostics.begin(), result.diagnostics.end(),
        [expected](const auto& item) { return item.code == expected; }));
}

void printDiagnostics(const AnalysisResult& result) {
    for (const auto& item : result.diagnostics)
        std::cerr << item.code << " @" << item.span.offset << ": " << item.message << '\n';
}

AnalysisResult good(std::string_view source, const AnalysisOptions& options = {}) {
    auto result = analyse(source, options);
    if (!result.ok()) printDiagnostics(result);
    require(result.ok(), std::string("valid analysis: ") + std::string(source.substr(0, 100)));
    return result;
}

void bad(std::string_view source, std::string_view expected, const AnalysisOptions& options = {}) {
    const auto result = analyse(source, options);
    require(!result.ok(), "invalid source never reports static success");
    if (!code(result, expected)) printDiagnostics(result);
    require(code(result, expected), std::string("expected ") + std::string(expected) + " in " + std::string(source.substr(0, 100)));
}

std::string read(std::string_view relative) {
    std::ifstream file(std::filesystem::path(PENTA_SOURCE_DIR) / relative, std::ios::binary);
    require(file.good(), std::string("fixture exists: ") + std::string(relative));
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

void contractFixtures() {
    struct Fixture { const char* path; const char* required; };
    const std::vector<Fixture> fixtures{
        {"valid/all_arithmetic.p", ""}, {"valid/grouping_sign_power.p", ""}, {"valid/comments_and_ranges.p", ""},
        {"invalid/undeclared.p", "E1201"}, {"invalid/narrowing.p", "E1204"},
        {"invalid/noninteger_exponent.p", "E1208"}, {"invalid/implicit_multiplication.p", "E1001"},
        {"invalid/bad_grouping.p", "E1301"}, {"invalid/short_constructor.p", "E1207"},
        {"invalid/range_into_scalar.p", "E1204"}, {"invalid/zero_step.p", "E1404"},
        {"invalid/real_step_for_integer_range.p", "E1204"}, {"invalid/protected_basis.p", "E1203"},
        {"invalid/bad_coeff.p", "E1405"}, {"invalid/gamma_out_of_range.p", "E1503"},
        {"invalid/late_directive.p", "E1501"}, {"invalid/literal_zero_divide.p", "E1401"},
        {"invalid/missing_semicolon.p", "E1101"}, {"invalid/bad_arity.p", "E1205"},
        {"invalid/no_plotting.p", "E1601"}, {"invalid/unicode_source_map.p", "E1204"},
        {"invalid/overflow.p", "E1402"}, {"invalid/unterminated_block.p", "E1003"}
    };
    // Each path/code is independently transcribed from the existing registry.
    const auto registry = read("fixtures/language/expectations.json");
    require(fixtures.size() == 23, "all 23 original contract fixtures are exercised");
    AnalysisOptions old;
    old.languageVersion = "0.1";
    for (const auto& fixture : fixtures) {
        require(registry.find(fixture.path) != std::string::npos, "fixture is still present in contract registry");
        const auto source = read(std::string("fixtures/language/") + fixture.path);
        const auto result = analyse(source, old);
        if (*fixture.required == '\0') {
            if (!result.ok()) printDiagnostics(result);
            require(result.ok(), fixture.path);
        } else {
            if (!code(result, fixture.required)) printDiagnostics(result);
            require(!result.ok() && code(result, fixture.required), fixture.path);
        }
    }
    for (const auto* example : {"basics.p", "grouping.p", "quaternion_sector.p", "ranges.p"})
        good(read(std::string("examples/") + example), old);
}

void astGoldensAndPrecedence() {
    const auto result = good("pentarnion p=i; pentarnion q=j; "
        "pentarnion left=(p*q)*q; pentarnion right=p*(q*q); "
        "pentarnion negative=-q^2; pentarnion squared=(-q)^2; "
        "pentarnion inverse=q^-1; real exponent=2^3^2;");
    const auto& statements = result.program.statements;
    require(statements.size() == 8, "all ordered declarations remain in AST");
    if (statements.size() != 8) return;
    const auto left = statements[2].expression;
    const auto right = statements[3].expression;
    require(left->kind == ExprKind::Binary && left->text == "*", "left model outer product retained");
    require(left->children[0]->kind == ExprKind::Group && left->children[0]->children[0]->kind == ExprKind::Binary,
        "left model retains Group on first product");
    require(right->children[1]->kind == ExprKind::Group && right->children[0]->kind == ExprKind::Reference,
        "right model retains different grouping");
    require(statements[4].expression->kind == ExprKind::Unary && statements[4].expression->text == "-" &&
        statements[4].expression->children[0]->kind == ExprKind::Binary, "-q^2 means unary minus of power");
    require(statements[5].expression->kind == ExprKind::Binary &&
        statements[5].expression->children[0]->kind == ExprKind::Group, "(-q)^2 preserves grouped negative base");
    require(statements[6].expression->children[1]->kind == ExprKind::Unary, "q^-1 has signed integer exponent");
    require(statements[7].expression->children[1]->kind == ExprKind::Binary &&
        statements[7].expression->children[1]->text == "^", "power is right associative");
    require(statements[7].expression->type == Type::Real, "scalar power has real type");
    const auto constructor = good("pentarnion p=pent([1,2,3,4,5]);");
    require(constructor.program.statements.front().expression->kind == ExprKind::PentConstructor &&
        constructor.program.statements.front().expression->children.size() == 5, "constructor retains five ordered coefficient expressions");
}

void declarationsAndPromotions() {
    const auto valid = good("integer n=1; real x=n; pentarnion p=x; n=2; x=3; p=4;");
    require(valid.symbols.size() == 3, "reassignment does not redeclare symbols");
    require(valid.program.statements[1].declaredType == Type::Real, "declared type survives widening");
    bad("real x=i;", "E1204");
    bad("integer n=1.5;", "E1204");
    bad("integer n=1; n=.5;", "E1204");
    bad("real x=y; real y=1;", "E1201");
    bad("missing=1;", "E1201");
    bad("real x=1; integer x=2;", "E1202");
    for (const auto* source : {"integer i=1;", "real sqrt=1;", "integer real=1;", "ell=2;", "norm2=3;", "real=2;", "integer display=1;"})
        bad(source, "E1203");
    const auto poisoned = analyse("integer invalid=.5; real dependent=invalid+1; real independent=3;");
    require(code(poisoned, "E1204"), "invalid declaration has its primary cause");
    require(!code(poisoned, "E1201"), "poisoned binding suppresses undeclared cascades");
    require(poisoned.symbols.size() == 1 && poisoned.symbols.front().name == "independent",
        "invalid declarations never create invented usable symbol types");
    const auto warning = good("real large=9007199254740993;");
    require(code(warning, "W2001"), "known lossy literal widening warns without failing validation");
    require(!code(good("real exact=18014398509481984;"), "W2001"), "large exactly representable integer does not falsely warn");
    bad("pentarnion q=i; pentarnion p=q^(2^3);", "E1208");
    good("pentarnion q=i; pentarnion p=q^8;");
}

void groupingChains() {
    const std::string declarations = "real a=2; pentarnion p=i; pentarnion q=j; pentarnion r=k; pentarnion s=ell; ";
    for (const auto* expression : {"a*p*q;", "p*a*q;", "p*(q*r);", "(p*q)*r;", "p^2*q;", "(p*q)*(r*p);"})
        good(declarations + expression);
    for (const auto* expression : {"p*q*r;", "p/q*r;", "p\\q/r;", "(p*q*r);", "p*q*r*s;", "1/p/q/r;", "p*2*q*3*r;"}) {
        const auto result = analyse(declarations + expression);
        require(countCode(result, "E1301") == 1, "only root of each offending ungrouped multiplicative chain is diagnosed");
    }
    require(countCode(analyse(declarations + "p*q*r + p*q*r;"), "E1301") == 2,
        "separate offending chains receive their own diagnostic");
}

void rangesAndLiteralChecks() {
    good("integer step=2; integer range xs=1:step:9; integer range copy=xs; copy=3:-1:1; "
         "real range ys=1:1:5; real range zs=ys; ys=.1:.1:1.;");
    bad("integer range xs=0:.5:1;", "E1204");
    bad("real scalar_value=0:.1:1;", "E1204");
    bad("integer range xs=1:1:5; real range ys=xs;", "E1204");
    bad("integer range xs=1:1:5; real x=xs+1;", "E1204");
    for (const auto* source : {"integer range xs=1:0:5;", "real range xs=1:-0.0:5;", "real range xs=1:(+0):5;"})
        bad(source, "E1404");
    good("integer zero=0; integer range xs=1:zero:9223372036854775807;");
    good("real range xs=0:1e-300:1e300;");
    bad("real x=1/(-0.0);", "E1401");
    bad("pentarnion p=i; pentarnion x=0\\p;", "E1401");
    good("integer zero=0; real x=1/zero;");
    good("integer x=9223372036854775807; integer y=-9223372036854775808; integer z=-0009223372036854775808;");
    bad("integer x=9223372036854775808;", "E1402");
    bad("integer x=+9223372036854775808;", "E1402");
    bad("integer x=-(9223372036854775808);", "E1402");
    bad("integer x=-9223372036854775809;", "E1402");
    bad("real x=1e9999;", "E1403");
    bad("real x=1e-9999;", "E1403");
    // These require execution to decide. The checker must not evaluate them.
    good("integer x=9223372036854775807+1; real y=sqrt(-1); pentarnion p=inv(0);");
}

void builtinsAndUnsupportedConstructs() {
    good("pentarnion p=i; real a=sqrt(4); pentarnion q=conj(p); real b=scalar(p); "
         "pentarnion u=pure(p); real n=norm2(p); pentarnion inverse=inv(p); real c=coeff(p,4); "
         "pentarnion comm=commutator(p,p); pentarnion assoc=associator(p,p,p); "
         "pentarnion coefficients=pent([1,2,3,4,5]); pentarnion embedded=conj(2);");
    for (const auto* name : {"sqrt", "conj", "scalar", "pure", "norm2", "inv", "coeff", "commutator", "associator"})
        bad(std::string(name) + "();", "E1205");
    for (const auto* source : {"pent([1,2]);", "pent([1,2,3,4,5,6]);", "pent(1,2,3,4,5);"}) bad(source, "E1207");
    for (const auto* source : {"sqrt(i);", "pent([i,0,0,0,0]);", "coeff(i,2.0);"})
        bad(source, "E1204");
    bad("coeff(i,-1);", "E1405");
    bad("coeff(i,5);", "E1405");
    bad("unknown(1);", "E1206");
    bad("real x=[1,2];", "E1204");
    bad("plot(i);","E1205");
    bad("for n=1:1:3;", "E1102");
    bad("function f();", "E1204");
    bad("native(i);", "E1102");
    bad("import data;", "E1204");
    for (const auto* source : {"parallel(i);", "solveODE(i);", "system(1);"})
        bad(source, "E1601");
}

void directivesAndDisplay() {
    const auto result = good("; ; % before directive\nuse_algebra(\"A_gamma\",gamma=+.5); "
        "pentarnion p=i; integer range xs=1:1:3; display(\"label\\n\",p,xs);");
    require(result.program.gamma == .5, "first nonempty directive overrides default context");
    const auto& display = result.program.statements.back();
    require(display.kind == StatementKind::Display && display.arguments.size() == 3, "display is an ordered top-level statement");
    require(display.arguments[0]->kind == ExprKind::StringLiteral && display.arguments[0]->text == "label\n",
        "display string escapes are decoded without executing anything");
    require(display.arguments[2]->type == Type::IntegerRange, "range descriptors are printable without enumeration");
    AnalysisOptions old;
    old.languageVersion = "0.1";
    bad("display(1);", "E1601", old);
    good("real display=1; display=2; real existing_name=display;", old);
    bad("display();", "E1205");
    bad("real x=display(1);", "E1601");
    bad("real x=\"label\";", "E1204");
    bad("display(\"a\"+\"b\");", "E1204");
    bad("use_algebra(\"other\",gamma=1);", "E1502");
    bad("use_algebra(\"A_gamma\",gamma=1.1);", "E1503");
    bad("use_algebra(\"A_gamma\",gamma=0+1);", "E1503");
    bad("real g=.5; use_algebra(\"A_gamma\",gamma=g);", "E1501");
    bad("use_algebra(\"A_gamma\",gamma=0); use_algebra(\"A_gamma\",gamma=1);", "E1501");
    AnalysisOptions context;
    context.gamma = .25;
    require(good("real x=1;", context).program.gamma == .25, "project context is used without directive");
    require(good("use_algebra(\"A_gamma\",gamma=0);", context).program.gamma == 0, "file directive is per analysis");
    require(good("real x=1;", context).program.gamma == .25, "one analysis never mutates another context");
}

void seededWorkspace() {
    AnalysisOptions options;
    options.initialSymbols = {{"x", Type::Real, {}}, {"p", Type::Pentarnion, {}}, {"xs", Type::IntegerRange, {}}};
    const auto result = good("x=2; p=p+i; display(x,p,xs); integer n=3;", options);
    require(result.symbols.size() == 4, "workspace symbols plus new declaration are returned");
    require(result.program.statements.front().declaredType == Type::Real, "seeded assignment preserves static declared type");
    bad("real x=2;", "E1202", options);
    bad("x=i;", "E1204", options);
    bad("xs=2;", "E1204", options);
    require(good("display(p);", options).symbols.size() == 3, "analysis does not mutate the input workspace");
    require(options.initialSymbols.size() == 3, "input workspace metadata is immutable");
    AnalysisOptions badSeed;
    badSeed.initialSymbols = {{"i", Type::Real, {}}};
    bad("", "E1203", badSeed);
    badSeed.initialSymbols = {{" space ", Type::Real, {}}};
    bad("", "E1203", badSeed);
}

void clearAndConsoleCommands() {
    AnalysisOptions seeded;
    seeded.initialSymbols = {{"p", Type::Pentarnion, {}}, {"q", Type::Real, {}}, {"state", Type::Integer, {}}};
    const auto named = good("clear p q; integer p=2; display(p,state);", seeded);
    require(named.program.statements.front().kind == StatementKind::Clear &&
        named.program.statements.front().names == std::vector<std::string>({"p", "q"}), "named clear targets retained in AST");
    require(named.symbols.size() == 2 && named.symbols.back().name == "p" && named.symbols.back().type == Type::Integer,
        "clear removes old static types and permits typed redeclaration");
    bad("clear p; display(p);", "E1201", seeded);
    good("clear ghost; display(p);", seeded);
    for (const auto* command : {"clear;", "clear all;", "clear();", "clear(\"all\");"}) {
        const auto cleared = good(command, seeded);
        require(cleared.symbols.empty(), "clear-all forms remove all user symbols");
        require(cleared.program.statements.front().names.empty(), "empty Clear targets means all variables");
        good(std::string(command) + " display(i,j,k,ell);", seeded);
        bad(std::string(command) + " display(state);", "E1201", seeded);
    }
    const auto functionStyle = good("clear(\"p\",\"state\"); display(q);", seeded);
    require(functionStyle.symbols.size() == 1 && functionStyle.symbols.front().name == "q", "quoted function-style clear targets work");
    const auto console = good("clc; clc(); display(p,q,state);", seeded);
    require(console.program.statements[0].kind == StatementKind::ClearConsole && console.symbols.size() == 3,
        "clc is an AST command without variable type side effects");
    for (const auto* command : {"clear i;", "clear sqrt;", "clear integer;", "clear display;", "clear clear;", "clear clc;", "clear(\"ell\");"})
        bad(command, "E1203", seeded);
    bad("clear(\"two words\");", "E1102", seeded);
    bad("clear all p;", "E1102", seeded);
    bad("clc(1);", "E1205", seeded);
    bad("real x=clear();", "E1601", seeded);
    bad("real x=clc();", "E1601", seeded);
    bad("integer clear=1;", "E1203");
    bad("integer clc=1;", "E1203");
    AnalysisOptions old;
    old.languageVersion = "0.1";
    good("real clear=1; real clc=2; clear=3; clc;", old);
    bad("clear;", "E1601", old);
    bad("clc();", "E1601", old);
}

void plottingAndElementaryFunctions() {
    const auto plotted = good("pentarnion p=1+i+2*j+3*k+4*ell; plot2(\"1\",\"ell\",p); plot3(\"i\",\"j\",\"k\",p,(p+p));");
    const auto& two = plotted.program.statements[1];
    require(two.kind == StatementKind::Plot && two.dimensions == 2 && two.axes[0] == 0 && two.axes[1] == 4,
        "plot2 retains canonical scalar/ell projection indices");
    require(two.arguments.size() == 1 && two.arguments[0]->type == Type::Pentarnion,
        "plot arguments contain only typed vector expressions");
    const auto& three = plotted.program.statements[2];
    require(three.dimensions == 3 && three.axes == std::array<int,3>{{1,2,3}} &&
        three.arguments[1]->kind == ExprKind::Group, "plot3 keeps selected axes and vector grouping");
    for (const auto* source : {"plot2(\"x\",\"j\",i);", "plot2(\"i\",\"i\",i);", "plot3(\"i\",\"j\",\"j\",i);", "plot2(i,j,i);", "plot2((\"i\"),\"j\",i);"})
        bad(source, "E1701");
    for (const auto* source : {"plot2(\"i\",\"j\",1);", "plot3(\"i\",\"j\",\"k\",1.0);", "integer range xs=1:1:2; plot2(\"i\",\"j\",xs);"})
        bad(source, "E1204");
    bad("plot2(\"i\",\"j\");", "E1205");
    bad("plot3(\"i\",\"j\",\"k\");", "E1205");
    bad("real handle=figure();", "E1601");
    bad("pentarnion p=i; plot2(\"i\",\"j\",p*p*p);", "E1301");
    good("pentarnion p=i; plot2(\"i\",\"j\",(p*p)*p);");
    std::string many = "plot2(\"i\",\"j\"";
    for (int at = 0; at < 256; ++at) many += ",i";
    good(many + ");");
    bad(many + ",i);", "E1703");
    const auto figures = good("integer n=2; figure(); figure(n+1); clf; clf(); close(n); close(\"all\");");
    require(figures.program.statements[1].kind == StatementKind::Figure && !figures.program.statements[1].expression,
        "figure() preserves automatic selection as an action");
    require(figures.program.statements[2].expression->kind == ExprKind::Binary &&
        figures.program.statements.back().kind == StatementKind::CloseAllFigures, "dynamic figure ids and close all retain intended AST");
    for (const auto* source : {"figure(0);", "figure(65);", "figure(-1);", "close(\"other\");"}) bad(source, "E1702");
    bad("figure(1.0);", "E1204");
    for (const auto* source : {"figure(1,2);", "close();", "clf(1);"}) bad(source, "E1205");
    good("integer n=0; figure(n);"); // Dynamic bounds belong to the runtime.
    const auto functions = good("real a=sin(1); real b=cos(1.0); real c=exp(0); real d=cosh(0); real e=sinh(0); "
        "pentarnion p=i; pentarnion a1=sin(p); pentarnion b1=cos(p); pentarnion c1=exp(p); "
        "pentarnion d1=cosh(p); pentarnion e1=sinh(p);");
    require(functions.program.statements[0].expression->type == Type::Real &&
        functions.program.statements.back().expression->type == Type::Pentarnion, "elementary functions overload by scalar/Pentarnion type");
    for (const auto* name : {"sin", "cos", "exp", "sinh", "cosh"}) {
        bad(std::string(name) + "();", "E1205");
        bad(std::string(name) + "(1,2);", "E1205");
        const auto rangeFunction=good("integer range xs=1:1:3; matrix values=" + std::string(name) + "(xs);");
        require(rangeFunction.program.statements.back().expression->type==Type::RealMatrix,"elementary functions materialize finite real ranges as arrays");
        bad("real " + std::string(name) + "=1;", "E1203");
    }
    AnalysisOptions old;
    old.languageVersion = "0.1";
    good("real plot2=1; real plot3=2; real figure=3; real clf=4; real close=5; "
         "real sin=6; real cos=7; real exp=8; real sinh=9; real cosh=10; plot3=11; clf; sin;", old);
    for (const auto* source : {"figure();", "clf;", "close(1);", "plot2(\"i\",\"j\",i);", "sin(1);"})
        bad(source, "E1601", old);
    for (const auto* name : {"tan", "asin", "acos", "atan", "tanh", "asinh", "acosh", "atanh", "log", "log10", "log2", "abs", "floor", "ceil", "round"}) {
        const auto typed = good("real result=" + std::string(name) + "(1);");
        require(typed.program.statements.front().expression->type == Type::Real, "real-only function returns real");
        bad(std::string(name) + "(i);", "E1204");
        bad(std::string(name) + "();", "E1205");
        bad(std::string(name) + "(1,2);", "E1205");
        bad(std::string(name) + "(1);", "E1601", old);
        good("real " + std::string(name) + "=1; " + std::string(name) + ";", old);
    }
    good("real angle=atan2(1,0);");
    bad("atan2(1);", "E1205");
    bad("atan2(i,1);", "E1204");
    bad("atan2(1,i);", "E1204");
    bad("integer result=floor(1.5);", "E1204");
}

void figureLayoutAndText() {
    const auto selected = good(R"(figure("Name","Phase α","NumberTitle","off");
        figure(4,"NUMBERtitle","ON","nAmE",""); figure(4); figure("NumberTitle","off");)");
    const auto& first = selected.program.statements[0];
    const auto& second = selected.program.statements[1];
    const auto& plain = selected.program.statements[2];
    require(!first.expression && first.figureName == "Phase α" && first.numberTitle == false,
        "new figure preserves Unicode name and disabled number title");
    require(second.expression && second.figureName == "" && second.numberTitle == true,
        "properties are case insensitive, order independent and allow an explicit empty name");
    require(!plain.figureName && !plain.numberTitle, "plain figure selection does not reset either property");
    require(!selected.program.statements[3].figureName && selected.program.statements[3].numberTitle == false,
        "NumberTitle can be supplied without Name");
    for (const auto* source : {R"(figure("Name");)", R"(figure(1,"Name","a","NumberTitle");)"}) bad(source,"E1205");
    for (const auto* source : {R"(figure("Color","red");)", R"(figure("Name","a","name","b");)",
         R"(figure("NumberTitle","off","numbertitle","on");)", R"(figure("NumberTitle","yes");)"}) bad(source,"E1704");
    bad(R"(figure("Name",1);)","E1204");
    bad(R"(figure("NumberTitle",1);)","E1204");
    const auto layout = good(R"(integer n=3; dock(n+1); undock(64); tilefigures(); tilefigures("DESKTOP");
        tilefigures("studio",8,8); tilefigures("desktop",n,2);)");
    require(layout.program.statements[1].kind == StatementKind::DockFigure &&
        layout.program.statements[2].kind == StatementKind::UndockFigure, "docking has explicit typed AST statements");
    require(layout.program.statements[3].text == "studio" && layout.program.statements[3].arguments.empty() &&
        layout.program.statements[4].text == "desktop" && layout.program.statements[5].arguments.size() == 2,
        "automatic and explicit tile destinations/dimensions survive parsing");
    for (const auto* source : {"dock();","undock(1,2);",R"(tilefigures("studio",2);)",R"(tilefigures("studio",1,1,1);)"}) bad(source,"E1205");
    for (const auto* source : {"dock(0);","undock(65);","dock(-1);"}) bad(source,"E1702");
    bad("dock(1.0);","E1204");
    bad(R"(tilefigures("studio",1.0,2);)","E1204");
    bad("tilefigures(1);","E1204");
    bad(R"(tilefigures("outside");)","E1704");
    for (const auto* source : {R"(tilefigures("studio",0,1);)",R"(tilefigures("desktop",1,65);)",
         R"(tilefigures("studio",8,9);)",R"(tilefigures("studio",-1,1);)"}) bad(source,"E1705");
    const auto labels = good(R"(title("Phase $\\alpha^2$\nnext"); xlabel("$i$","Interpreter","none");
        ylabel("$j$","interpreter","LaTeX"); zlabel("ell"); caption("α 😀 \"quoted\"");)");
    require(labels.program.statements[0].kind == StatementKind::SetTitle && labels.program.statements[0].text == "Phase $\\alpha^2$\nnext" &&
        labels.program.statements[0].latex, "text stores exact decoded math escapes and default LaTeX mode");
    require(labels.program.statements[1].kind == StatementKind::SetXLabel && !labels.program.statements[1].latex &&
        labels.program.statements[2].latex && labels.program.statements[4].kind == StatementKind::SetCaption,
        "all label targets and literal rendering modes remain explicit");
    for (const auto* source : {"title();",R"(xlabel("x","Interpreter");)"}) bad(source,"E1205");
    for (const auto* source : {R"(title("x","Font","a");)",R"(title("x","Interpreter","tex");)",
         R"(title("x","Interpreter","none","interpreter","latex");)"}) bad(source,"E1704");
    bad("caption(1);","E1204");
    good("title(\"" + std::string(4096,'x') + "\");");
    bad("title(\"" + std::string(4097,'x') + "\");","E1706");
    bad("figure(\"Name\",\"" + std::string(4097,'x') + "\");","E1706");
    std::string unicode;
    for (int n=0;n<1024;++n) unicode += "😀";
    good("caption(\""+unicode+"\");");
    bad("caption(\""+unicode+"x\");","E1706");
    AnalysisOptions old; old.languageVersion = "0.1";
    for (const auto* name : {"dock","undock","tilefigures","title","xlabel","ylabel","zlabel","caption"}) {
        good("real " + std::string(name) + "=1; " + name + ";",old);
        bad("real " + std::string(name) + "=1;","E1203");
    }
    for (const auto* source : {"dock(1);", "undock(1);", "tilefigures();",R"(title("x");)",R"(caption("x");)"})
        bad(source,"E1601",old);
    bad(R"(real x=title("x");)","E1601");
}

void nativeOdeSyntax() {
    const std::string model="ode flow(t,pentarnion p;real a,pentarnion b){p'=b*p-a*p;} ";
    const auto checked=good(model+"pentarnion p0=1+i;real damping=.2;pentarnion b=j;"
        "solution s=odesolve(flow,p=p0,a=damping,b=b,t0=0,t1=20,sample_step=.01,method=\"bdf\",jacobian=\"analytic\",rtol=1e-8,atol=1e-10);"
        "display(s);subplot(2,2,1);plot(s.t,coeff(s.p,0));subplot(2,2,2);plot2(\"i\",\"j\",s.p);plot3(\"i\",\"j\",\"ell\",s.p);");
    const auto definition=checked.program.statements[0].ode;
    require(definition && definition->timeName=="t" && definition->states.size()==1 && definition->parameters.size()==2 &&
        definition->derivatives[0]->type==Type::Pentarnion,"ODE AST preserves typed state/explicit parameter schema");
    const auto& solve=checked.program.statements[4].expression;
    require(solve->kind==ExprKind::OdeSolve && solve->type==Type::Solution && solve->ode==definition &&
        solve->argumentNames.front()=="p", "solver expression retains named runtime bindings and model schema");
    const auto& curve=checked.program.statements[7];
    require(curve.name=="plot" && curve.axes[0]==-1 && curve.arguments[0]->type==Type::RealSeries &&
        curve.arguments[1]->type==Type::RealSeries,"solution time/coefficient access creates checked real series");
    require(checked.program.statements[9].arguments[0]->type==Type::PentarnionSeries,"projection plot accepts sampled Pentarnions");
    good("ode coupled(t,real x,pentarnion p;real a){p'=a*p;x'=scalar(p)+sin(t);}" 
        "solution s=odesolve(coupled,x=1,p=i,a=2,t0=0,t1=1,method=\"adams\",jacobian=\"finite_difference\");plot(s.t,s.x);");
    good("ode local(t,real x){x'=-x;}solution s=odesolve(local,x=1,t0=0,t1=1);solution copy=s;plot(copy.t,copy.x);");
    good("ode local(t,real x){x'=-x;}solution s=odesolve(local,x=1,t0=0,t1=1,method=\"erk\",jacobian=\"analytic\");");
    good("ode local(t,real x){x'=-x;}solution s=odesolve(local,x=1,t0=0,t1=1,method=\"esdirk\",jacobian=\"finite_difference\");");
    good("ode local(t,real x){x'=-x;}solution s=odesolve(local,x=1,t0=2,t1=2);");
    good("ode power(t,pentarnion p){p'=p^2;}ode funcs(t,real x;pentarnion p){x'=coeff(p,0)+norm2(p)+sqrt(1)+exp(t)+cos(t);}");
    bad("real hidden=1;ode f(t,real x){x'=hidden*x;}","E1201");
    good("real hidden=1;ode f(t,real x;real hidden){x'=hidden*x;}");
    bad("ode f(t,real x){x'=x;}x;","E1201");
    for(const auto* source:{"ode f(t,real x){x'=x;x'=x;}","ode f(t,real x,real y){x'=y;}","ode f(t,real x){y'=x;}"}) bad(source,"E1802");
    bad("ode f(t,real x){x'=i;}","E1204");
    bad("ode f(t,real t){t'=1;}","E1202");
    bad("ode f(time,real t){t'=1;}","E1801");
    bad("ode f(t,real rtol){rtol'=1;}","E1801");
    bad("ode f(t,integer x){x'=1;}","E1801");
    bad("ode f(t,real x){x'=1;}real f=1;","E1202");
    for(const auto* rhs:{"sin(p)","inv(p)","p/p","p\\p","p^-1","p^65","p^n","coeff(p,n)","floor(t)","abs(t)"})
        bad("ode f(t,pentarnion p;real n){p'="+std::string(rhs)+";}","E1804");
    bad("ode f(t,pentarnion p){p'=p*p*p;}","E1301");
    const auto groups=good("ode left(t,pentarnion p;pentarnion b){p'=(b*p)*p;}ode right(t,pentarnion p;pentarnion b){p'=b*(p*p);}");
    require(groups.program.statements[0].ode->derivatives[0]->children[0]->kind==ExprKind::Group &&
        groups.program.statements[1].ode->derivatives[0]->children[1]->kind==ExprKind::Group,"ODE lowering receives distinct multiplication grouping");
    const std::string simple="ode f(t,pentarnion p;real a){p'=a*p;} ";
    for(const auto* args:{"p=i,t0=0,t1=1", "p=i,a=1,t0=0", "p=i,a=1,t0=0,t1=1,a=2", "p=i,a=1,t0=0,t1=1,unknown=1",
        "p=i,a=1,t0=0,t1=1,rtol=0", "p=i,a=1,t0=0,t1=1,sample_step=-1",
        "p=i,a=1,t0=0,t1=1,method=\"rk4\"", "p=i,a=1,t0=0,t1=1,jacobian=\"manual\"",
        "p=i,a=1,t0=0,t1=1,rtol=1", "p=i,a=1,t0=0,t1=1,max_steps=100000001",
        "p=i,a=1,t0=0,t1=1,max_samples=1000001", "p=i,a=1,t0=0,t1=1,max_seconds=3601"})
        bad(simple+"solution s=odesolve(f,"+args+");","E1805");
    bad(simple+"solution s=odesolve(f,p=i,a=i,t0=0,t1=1);","E1204");
    bad(simple+"solution s=odesolve(f,p=i,a=1,t0=0,t1=1,max_steps=1.5);","E1204");
    bad("solution s=odesolve(missing,p=i,t0=0,t1=1);","E1805");
    good(simple+"solution backward=odesolve(f,p=i,a=1,t0=1,t1=0);");
    const auto withSolution=simple+"solution s=odesolve(f,p=i,a=1,t0=0,t1=1);";
    bad(withSolution+"display(s.missing);","E1806");bad("real x=1;display(x.t);","E1806");
    bad(withSolution+"plot(s.t,s.p);","E1204");bad(withSolution+"plot2(\"i\",\"j\",s.p,i);","E1204");
    bad(withSolution+"display(coeff(s.p,5));","E1405");
    for(const auto* source:{"subplot();","subplot(1,2);"})bad(source,"E1205");
    for(const auto* source:{"subplot(1,2,3);","subplot(8,9,1);","subplot(0,1,1);"})bad(source,"E1705");
    bad("subplot(1.0,2,1);","E1204");
    AnalysisOptions seeded;seeded.initialSymbols=checked.symbols;
    good("plot(s.t,coeff(s.p,1));",seeded);
    good("solution newer=odesolve(flow,p=i,a=2,b=j,t0=0,t1=1);",seeded);
    bad("clear s;display(s);","E1201",seeded);
    AnalysisOptions old;old.languageVersion="0.1";
    good("real ode=1;real solution=2;real odesolve=3;real subplot=4;real plot=5;ode;solution;odesolve;subplot;plot;",old);
    bad("ode f(t,real x){x'=x;}","E1601",old);
    bad("solution s=odesolve(f,x=1,t0=0,t1=1);","E1601",old);
    bad("subplot(1,1,1);","E1601",old);
}

void timingSyntax() {
    const auto result=good("tic;tic();toc;toc();real elapsed=toc();real combined=toc()+1;display(toc());");
    require(result.program.statements[0].kind==StatementKind::Tic && result.program.statements[1].kind==StatementKind::Tic &&
        result.program.statements[2].kind==StatementKind::Toc && result.program.statements[3].kind==StatementKind::Toc,
        "timer command forms retain explicit statement kinds");
    require(result.program.statements[4].expression->type==Type::Real && result.program.statements[5].expression->type==Type::Real,
        "toc() is a real-valued expression when captured or composed");
    good("toc;real elapsed=toc();"); // Checking does not inspect or measure runtime timer state.
    for(const auto* source:{"tic(1);","toc(1);","real elapsed=toc(1);"})bad(source,"E1205");
    bad("real elapsed=tic();","E1601");
    bad("integer elapsed=toc();","E1204");
    for(const auto* name:{"tic","toc"}) {
        bad("real "+std::string(name)+"=1;","E1203");
        bad("clear "+std::string(name)+";","E1203");
    }
    bad("ode f(t,real x){x'=toc();}","E1804");
    AnalysisOptions old;old.languageVersion="0.1";
    good("real tic=1;real toc=2;tic=tic+toc;tic;toc;",old);
    for(const auto* source:{"tic();","toc();","real elapsed=toc();"})bad(source,"E1601",old);
}

void matricesAndRandomFunctions() {
    const auto result=good("matrix A=[1,2;3,4]; vector v=[1;2]; matrix P=[i,j;k,ell]; "
        "realmatrix B=A*A; pentarnionmatrix Q=P*A; matrix row=v.'; matrix conjugate=P'; "
        "real entry=A(2,1); pentarnion element=P(3); A(1,2)=7; P(2)=1+i; "
        "matrix sumValues=A+2; matrix hadamard=A.*A; matrix quot=A./2; matrix powers=A.^2; "
        "matrix components=coeff(P,1); matrix conjugated=conj(P); matrix empty=[];");
    if(result.program.statements.size()==18) {
        const auto literal=result.program.statements[0].expression;
        require(literal->kind==ExprKind::MatrixLiteral && literal->rows==2 && literal->columns==2 && literal->children.size()==4,
            "literal retains rectangular row-major shape and every scalar child");
        require(result.program.statements[1].text=="vector" && result.program.statements[1].declaredType==Type::RealMatrix,
            "vector declaration marks runtime orientation validation");
        require(result.program.statements[2].declaredType==Type::PentarnionMatrix,"matrix infers Pentarnion element type");
        require(result.program.statements[5].expression->kind==ExprKind::Transpose && result.program.statements[5].expression->text==".'",
            "plain transpose is an explicit postfix node");
        require(result.program.statements[6].expression->text=="'","conjugate transpose retains distinct spelling");
        const auto index=result.program.statements[7].expression;
        require(index->kind==ExprKind::Index && index->children.size()==3 && index->children[0]->text=="A",
            "index AST stores base before one-based row/column arguments");
        const auto& assignment=result.program.statements[9];
        require(assignment.kind==StatementKind::IndexedAssignment && assignment.name=="A" && assignment.arguments.size()==2 &&
            assignment.declaredType==Type::RealMatrix && assignment.expression->type==Type::Integer,
            "indexed assignment has scalar RHS, target type and original index order");
        require(result.program.statements.back().expression->rows==0 && result.program.statements.back().expression->columns==0,
            "empty matrix is distinct zero-by-zero array");
    } else require(false,"matrix fixture has all statements");
    const auto whitespace=good("matrix A=[1 2;3 4]; matrix B=[1 -2]; matrix C=[1 - 2]; matrix D=[1\n2\n3];");
    if(whitespace.program.statements.size()==4) {
        require(whitespace.program.statements[1].expression->columns==2,"adjacent signed entry after space starts a new column");
        require(whitespace.program.statements[2].expression->columns==1,"spaced subtraction remains one matrix entry");
        require(whitespace.program.statements[3].expression->rows==3,"newline separates matrix rows");
    }
    const auto builtins=good("rng(123); rng(\"default\"); real uniform=rand(); real normal=randn(); integer die=randi(6); "
        "matrix U=rand(2,3); matrix N=randn(2); matrix I=randi(5,2,3); matrix Z=zeros(0,0); "
        "matrix O=ones(2); matrix E=eye(2,3); matrix S=size(E); integer r=size(E,1); "
        "integer count=numel(E); integer longest=length(E); matrix T=transpose(E); vector X=linspace(0,1,9);");
    require(builtins.program.statements[0].expression->type==Type::Void,"rng is an explicit Void call");
    require(builtins.program.statements[7].declaredType==Type::RealMatrix,"dimensioned randi is a real matrix");
    good("matrix A=[1]; real value=(A+1)(1);");
    good("matrix A=[1,2]; matrix B=sin(A)+sqrt(A); matrix P=[i,j]; matrix Q=exp(P)+pure(A); matrix S=scalar(P)+norm2(P);");
    for(const auto* source:{"matrix A=[1,2;3];","matrix A=[1;];"})bad(source,"E1901");
    good("matrix A=[[1,2]]; real x=A(1.0); matrix S=norm2(1:2);");
    for(const auto* source:{"matrix A=1;","realmatrix A=[i];","matrix A=[1]; A(1)=[2];",
        "matrix A=[1]; matrix B=A/[2];","matrix A=[1]; matrix B=A\\A;",
        "matrix A=[1]; matrix B=A^2;","matrix P=[i]; matrix Q=P./i;",
        "rng(\"unknown\");","matrix A=zeros(2.0);","matrix A=sqrt([i]);"})bad(source,"E1204");
    bad("matrix A=inv([i]);","E1961");
    bad("matrix P=[i]; matrix Q=P.^0.5;","E1208");
    for(const auto* source:{"matrix A=[1]; real x=A(0);","matrix A=zeros(-1);","rng(-1);","randi(0);"})bad(source,"E1902");
    for(const auto* source:{"matrix A=rand(1,2,3);","rng();","matrix A=[1]; real x=A();","matrix A=linspace(0);"})bad(source,"E1205");
    good("vector x=linspace(0,1);");
    const auto plots=good("vector x=linspace(0,1,20); vector y=sin(x); plot(x,y); plot3(x,y,x.^2);");
    require(plots.program.statements.back().name=="plot3_coords" && plots.program.statements.back().dimensions==3 &&
        plots.program.statements.back().arguments.size()==3,"coordinate plot3 retains three real array arguments");
    bad("plot3([i],[1],[2]);","E1204");
    const auto nativeDots=good("ode f(t,real y){y'=2.*y + t.^2;} solution s=odesolve(f,y=1,t0=0,t1=1);");
    require(nativeDots.program.statements.front().ode->derivatives.front()->children[0]->text=="*" &&
        nativeDots.program.statements.front().ode->derivatives.front()->children[1]->text=="^",
        "native scalar dot operations normalize before native generation");
    bad("matrix P=[i]; matrix Q=P*P*P;","E1301");
    good("matrix P=[i]; matrix Q=(P*P)*P;");
    bad("ode f(t,real y){y'=rand();}","E1804");
    AnalysisOptions old;old.languageVersion="0.1";
    good("real matrix=2; real vector=3; real rand=4; real size=5; matrix=matrix+vector+rand+size;",old);
    bad("matrix A=[1];","E1601",old);
    AnalysisOptions workspace;workspace.initialSymbols={{"A",Type::RealMatrix,{}}};
    good("real entry=A(1); A(1)=2;",workspace);
}

void sourceLocationsAndRecovery() {
    const std::string source = "% α 😀\r\nreal x = i;\r\n";
    const auto result = analyse(source);
    require(code(result, "E1204"), "Unicode source mapping example has type mismatch");
    const auto mismatch = std::find_if(result.diagnostics.begin(), result.diagnostics.end(), [](const auto& item) { return item.code == "E1204"; });
    if (mismatch != result.diagnostics.end()) {
        require(mismatch->span.offset == source.rfind('i') && mismatch->span.length == 1, "diagnostic preserves exact UTF8 byte span");
        const auto position = sourceLocation(source, mismatch->span.offset);
        require(position.line == 2 && position.column == 10, "Unicode comment and CRLF preserve user-facing location");
    }
    const auto scalarPosition = sourceLocation("α😀 @", std::string("α😀 ").size());
    require(scalarPosition.line == 1 && scalarPosition.column == 4, "columns count Unicode scalars rather than UTF8 bytes");
    const std::string bom = std::string("\xEF\xBB\xBF") + "real x=1;";
    good(bom);
    require(sourceLocation(bom, 3).column == 1, "leading BOM is encoding metadata for column mapping");
    const auto recovered = analyse("real first=; real good=2; real bad=unknown; real last=4;");
    require(code(recovered, "E1102") && code(recovered, "E1201"), "semicolon recovery finds later independent errors");
    require(std::any_of(recovered.symbols.begin(), recovered.symbols.end(), [](const auto& symbol) { return symbol.name == "last"; }),
        "syntax error does not suppress later valid declarations");
    bad("real x=1", "E1101");
    bad("pentarnion p=(i*j;", "E1102");
}

void limitsCancellationAndHostileInput() {
    AnalysisOptions limit;
    limit.maximumSourceBytes = 3;
    bad("real x=1;", "E1103", limit);
    limit = {}; limit.maximumTokens = 3;
    bad("real x=1;", "E1103", limit);
    limit = {}; limit.maximumNodes = 2;
    bad("real x=1+2;", "E1103", limit);
    limit = {}; limit.maximumStatements = 1;
    bad("real x=1; real y=2;", "E1103", limit);
    limit = {}; limit.maximumNesting = 8;
    bad("real x=((((((((((1))))))))));", "E1103", limit);
    std::string flat = "real x=1";
    std::string powers = "real x=1";
    for (int at = 0; at < 5000; ++at) { flat += "+1"; powers += "^1"; }
    flat += ';'; powers += ';';
    bad(flat, "E1103");
    bad(powers, "E1103");
    limit = {}; limit.maximumDiagnostics = 2;
    const auto budget = analyse("a;b;c;d;", limit);
    require(!budget.ok() && budget.diagnostics.size() == 2, "diagnostic limit never hides static failure");
    limit.maximumDiagnostics = 1;
    const auto warningBeforeError = analyse("real large=9007199254740993; missing;", limit);
    require(!warningBeforeError.ok() && code(warningBeforeError, "E1201"), "a full warning budget cannot hide a later error");
    AnalysisOptions cancellation;
    cancellation.cancellation = std::make_shared<std::atomic_bool>(true);
    const auto stopped = analyse("real x=1;", cancellation);
    require(stopped.cancelled && !stopped.ok(), "cancelled analysis never reports success");
    std::uint32_t random = 0x58a1c3d9U;
    for (int iteration = 0; iteration < 1500; ++iteration) {
        std::string source;
        random ^= random << 13; random ^= random >> 17; random ^= random << 5;
        const std::size_t length = random % 100;
        for (std::size_t index = 0; index < length; ++index) {
            random ^= random << 13; random ^= random >> 17; random ^= random << 5;
            source.push_back(static_cast<char>(random & 0xff));
        }
        const auto invalid = analyse(source);
        require(invalid.diagnostics.size() <= 128, "invalid-byte analysis stays within diagnostic budget");
        for (const auto& item : invalid.diagnostics) {
            require(item.span.offset <= source.size(), "invalid-byte diagnostic starts inside source");
            require(item.span.length <= source.size() - std::min(item.span.offset, source.size()), "invalid-byte diagnostic ends inside source");
        }
    }
}
} // namespace

int main() {
    contractFixtures();
    astGoldensAndPrecedence();
    declarationsAndPromotions();
    groupingChains();
    rangesAndLiteralChecks();
    builtinsAndUnsupportedConstructs();
    directivesAndDisplay();
    seededWorkspace();
    clearAndConsoleCommands();
    plottingAndElementaryFunctions();
    figureLayoutAndText();
    nativeOdeSyntax();
    timingSyntax();
    matricesAndRandomFunctions();
    sourceLocationsAndRecovery();
    limitsCancellationAndHostileInput();
    if (failures) { std::cerr << failures << " failures from " << checks << " checks.\n"; return 1; }
    std::cout << "Frontend tests passed: " << checks << " checks, all 23 legacy fixtures, AST/type/display/workspace/resource regressions.\n";
    return 0;
}
