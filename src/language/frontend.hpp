#pragma once

#include "lexer.hpp"
#include "algebra/universal.hpp"

#include <atomic>
#include <array>
#include <memory>
#include <optional>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace penta::language {

enum class Type { Invalid, Integer, Real, Pentarnion, IntegerRange, RealRange, String, Void,
                  OdeModel, Solution, RealSeries, PentarnionSeries, RealMatrix, PentarnionMatrix, Function,
                  Complex, Record, Tuple, LogicalMatrix, Algebra, Element, ElementSeries, ElementMatrix, ArraySeries, Tensor };
enum class Severity { Error, Warning };

struct SourceOrigin {
    std::string identity;
    std::shared_ptr<const std::string> source;
};
struct DiagnosticFrame {
    std::string function,sourceIdentity,sourceHash;
    SourceSpan span{};
    std::shared_ptr<const std::string> source;
};

struct Diagnostic {
    Severity severity = Severity::Error;
    std::string code;
    std::string message;
    SourceSpan span;
    std::vector<DiagnosticFrame> frames{};
};

enum class ExprKind {
    IntegerLiteral, RealLiteral, StringLiteral, Reference, Unary, Binary,
    Call, PentConstructor, Range, Group, OdeSolve, Member, MatrixLiteral, Index, Transpose, AllIndices, EndIndex,
    FunctionHandle, Lambda, Invoke, TupleLiteral
};

struct Expr;
struct OdeDefinition;
struct FunctionDefinition;
struct ValueSchema;
struct Program;
using ExprPtr = std::shared_ptr<Expr>;

struct Expr {
    ExprKind kind = ExprKind::IntegerLiteral;
    Type type = Type::Invalid;
    SourceSpan span;
    // Literal numeric spelling, decoded string contents, reference/call name,
    // or unary/binary operator. Group/constructor/range do not need text.
    std::string text;
    std::vector<ExprPtr> children;
    std::vector<std::string> argumentNames; // OdeSolve: named runtime bindings/options.
    std::shared_ptr<const OdeDefinition> ode; // Checked model or solution schema.
    std::size_t rows = 0, columns = 0; // MatrixLiteral: syntactic cell grid.
    std::vector<std::size_t> rowWidths; // Per-row syntactic cells before block/range expansion.
    std::shared_ptr<const FunctionDefinition> function;
    std::shared_ptr<const ValueSchema> schema;
};

struct OdeVariable { std::string name; Type type = Type::Real; SourceSpan span; std::shared_ptr<const ValueSchema> schema{}; };
struct OdeDefinition {
    std::string name;
    std::string timeName;
    std::vector<OdeVariable> states;
    std::vector<OdeVariable> parameters;
    std::vector<ExprPtr> derivatives; // One expression per state, in declared order.
    SourceSpan span;
    std::shared_ptr<const SourceOrigin> origin{};
};

enum class StatementKind {
    Directive, Declaration, Assignment, Expression, Display, Clear, ClearConsole,
    Figure, Plot, ClearFigure, CloseFigure, CloseAllFigures, DockFigure, UndockFigure,
    TileFigures, SetTitle, SetXLabel, SetYLabel, SetZLabel, SetCaption, OdeDefinition, Subplot, Tic, Toc,
    IndexedAssignment, If, While, For, Break, Continue, Return, FunctionDefinition,
    Destructure, Import
};

struct Statement {
    StatementKind kind = StatementKind::Expression;
    SourceSpan span;
    SourceSpan nameSpan;
    std::string name; // Declared/assigned name, or algebra alias for directive.
    Type declaredType = Type::Invalid;
    ExprPtr expression; // Initializer, assignment RHS, expression, or gamma literal.
    std::vector<ExprPtr> arguments; // Display arguments, in original source order.
    std::vector<std::string> names; // Clear targets; empty means all user bindings.
    int dimensions = 0; // Plot: 2 or 3. arguments contains only vector expressions.
    std::array<int, 3> axes{{1, 2, 3}}; // Canonical (1,i,j,k,ell) component indices.
    std::optional<std::string> figureName;
    std::optional<bool> numberTitle;
    std::string text;
    bool latex = true;
    std::shared_ptr<OdeDefinition> ode;
    std::vector<Statement> body, alternative;
    std::shared_ptr<FunctionDefinition> function;
    std::vector<Type> declaredTypes;
    std::shared_ptr<const Program> module;
    std::string moduleId;
    std::shared_ptr<const ValueSchema> declaredSchema;
};

struct FunctionDefinition {
    std::string name;
    Type result = Type::Void;
    std::vector<OdeVariable> parameters;
    std::vector<Statement> body;
    SourceSpan span;
    bool native = false;
    std::vector<Type> results; // Empty retains the original single result.
    std::vector<std::string> captureNames;
    std::vector<std::shared_ptr<const ValueSchema>> parameterSchemas;
    std::vector<ExprPtr> parameterDefaults;
    std::shared_ptr<const ValueSchema> resultSchema;
    std::shared_ptr<const SourceOrigin> origin{};
};

struct SchemaField {std::string name;Type type=Type::Invalid;std::shared_ptr<const ValueSchema> schema;};
struct ValueSchema {
    std::vector<SchemaField> fields;
    std::vector<SchemaField> elements;
    std::shared_ptr<const FunctionDefinition> function;
    algebra::universal::DefinitionPtr algebra;
    Type seriesElementType=Type::Invalid; // Distinguishes element<C> from legacy Complex series.
    std::vector<std::size_t> shape; // Explicit fixed shapes; empty retains ordinary dynamic array semantics.
    Type tensorEntryType=Type::Real;
};

// Real-coordinate packing for checked scalar model/callback types. A zero
// result means an unsupported or unresolved descriptor, never a guessed width.
[[nodiscard]] inline std::size_t coordinateWidth(Type type,const std::shared_ptr<const ValueSchema>& schema={}) {
    if(type==Type::Integer||type==Type::Real)return 1;
    if(type==Type::Pentarnion)return 5;
    if(type==Type::Complex)return 2;
    if(type==Type::Element&&schema&&schema->algebra)return schema->algebra->dimension();
    if(type==Type::Tensor&&schema&&!schema->shape.empty()&&schema->shape.size()<=8){
        const auto entry=schema->tensorEntryType;
        std::size_t width=entry==Type::Real?1:entry==Type::Complex?2:entry==Type::Pentarnion?5:entry==Type::Element&&schema->algebra?schema->algebra->dimension():0;
        if(!width||width>64)return 0;
        for(const auto extent:schema->shape){if(!extent||extent>8000000/width)return 0;width*=extent;}
        return width;
    }
    if((type==Type::RealMatrix||type==Type::PentarnionMatrix||type==Type::ElementMatrix)&&schema&&schema->shape.size()==2){
        const auto rows=schema->shape[0],columns=schema->shape[1];
        const auto components=type==Type::PentarnionMatrix?5:type==Type::ElementMatrix?(schema->algebra?schema->algebra->dimension():0):1;
        if(!components||!rows||!columns||rows>8000000/columns/components)return 0;
        return rows*columns*components;
    }
    return 0;
}

struct Program {
    std::vector<Statement> statements;
    std::string algebraId = "A_gamma_v1";
    double gamma = 1.0;
    std::string languageVersion = "0.2";
};

struct Symbol {
    std::string name;
    Type type = Type::Invalid;
    SourceSpan declarationSpan;
    std::shared_ptr<const OdeDefinition> ode{}; // Models and solutions retain typed state schema.
    std::shared_ptr<const FunctionDefinition> function{};
    std::shared_ptr<const ValueSchema> schema{};
};

struct ModuleSource {std::string identity;std::string source;};
struct ImportBudget {std::size_t bytes=0;};
struct ModuleDependency {std::string identity;std::shared_ptr<const std::string> source;};

struct AnalysisOptions {
    std::string algebraId = "A_gamma_v1";
    double gamma = 1.0;
    std::string languageVersion = "0.2";
    std::size_t maximumSourceBytes = 4 * 1024 * 1024;
    std::size_t maximumTokens = 200000;
    std::size_t maximumNodes = 200000;
    std::size_t maximumStatements = 50000;
    std::size_t maximumNesting = 128;
    std::size_t maximumDiagnostics = 128;
    std::shared_ptr<std::atomic_bool> cancellation;
    // Typed workspace names only; no runtime values enter static analysis.
    std::vector<Symbol> initialSymbols;
    std::filesystem::path sourceDirectory;
    std::string sourceIdentity; // File/command identity for retained, navigable call frames.
    std::function<std::optional<ModuleSource>(const std::string&,const std::filesystem::path&)> moduleResolver;
    std::vector<std::string> importStack;
    std::size_t maximumImportDepth=16;
    std::shared_ptr<ImportBudget> importBudget;
    std::size_t maximumImportBytes=8*1024*1024;
};

struct AnalysisResult {
    Program program;
    std::vector<ModuleDependency> dependencies; // Exact saved source bytes used for recursive import analysis.
    std::vector<Diagnostic> diagnostics;
    std::vector<Symbol> symbols;
    bool cancelled = false;
    [[nodiscard]] bool ok() const noexcept;
};

struct SourceLocation {
    std::size_t line = 1;
    std::size_t column = 1; // One-based Unicode-scalar column, CRLF counts once.
};

// AST retains operand order and explicit Groups. Analysis never evaluates source.
// The only out-of-int64 literal admitted is magnitude 9223372036854775808 as the
// immediate child of unary '-'; evaluators must handle that node before its child.
[[nodiscard]] AnalysisResult analyse(std::string_view source,
                                     const AnalysisOptions& options = {});
[[nodiscard]] SourceLocation sourceLocation(std::string_view source, std::size_t byteOffset) noexcept;
[[nodiscard]] std::string_view typeName(Type type) noexcept;

} // namespace penta::language
