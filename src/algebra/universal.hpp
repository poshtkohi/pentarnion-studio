#pragma once

#include <cstddef>
#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace penta::algebra::universal {

class Error final : public std::runtime_error {
public:
    Error(std::string code, const std::string& message);
    std::string code;
};

enum class Evidence { Unknown, Declared, ProvedExact, NumericalEvidence, Failed };
[[nodiscard]] std::string_view evidenceName(Evidence) noexcept;
struct Property {
    bool value=false;
    Evidence evidence=Evidence::Unknown;
    std::string detail;
};
struct Properties {
    Property unital, commutative, associative, alternative, powerAssociative;
    Property conjugation, positiveMetric, division;
};
struct Term {
    std::size_t left=0,right=0,output=0;
    double coefficient=0;
    std::string exact; // Canonical rational, or empty for explicitly approximate data.
};
struct ImportOptions {
    std::size_t maxBytes=8*1024*1024;
    std::size_t maxDimension=64;
    std::size_t maxTerms=262144;
    std::size_t maxIdentityOperations=10000000;
    std::size_t maxRationalBits=4096;
    double identityTolerance=1e-12;
    std::shared_ptr<std::atomic_bool> cancellation;
};
struct BuiltinOptions { unsigned positive=3,negative=0; };

class Definition final {
public:
    struct Data;
    [[nodiscard]] const std::string& name() const noexcept;
    [[nodiscard]] const std::string& id() const noexcept;
    [[nodiscard]] const std::string& hash() const noexcept;
    [[nodiscard]] const std::string& sourceHash() const noexcept;
    [[nodiscard]] const std::string& sourceJson() const noexcept; // Exact imported bytes; empty for trusted builtins.
    [[nodiscard]] const std::string& builtinKind() const noexcept;
    [[nodiscard]] const std::string& convention() const noexcept;
    [[nodiscard]] const std::string& coefficientDomain() const noexcept;
    [[nodiscard]] std::size_t dimension() const noexcept;
    [[nodiscard]] const std::vector<std::string>& basis() const noexcept;
    [[nodiscard]] const std::vector<Term>& terms() const noexcept;
    [[nodiscard]] const std::vector<double>& unit() const noexcept;
    [[nodiscard]] const std::vector<double>& conjugation() const noexcept; // Row major.
    [[nodiscard]] const std::vector<double>& metric() const noexcept; // Row major.
    [[nodiscard]] const Properties& properties() const noexcept;
    [[nodiscard]] std::size_t retainedBytes() const noexcept; // Owned storage, excluding allocator bookkeeping.
    [[nodiscard]] double coefficient(std::size_t left,std::size_t right,std::size_t output) const;
    [[nodiscard]] std::string json() const;
    // Public only to allow native implementation units to construct immutable data.
    explicit Definition(std::shared_ptr<const Data>);
private:
    std::shared_ptr<const Data> data_;
};
using DefinitionPtr=std::shared_ptr<const Definition>;

[[nodiscard]] DefinitionPtr builtin(std::string_view name,const BuiltinOptions& options={});
[[nodiscard]] DefinitionPtr fromJson(std::string_view bytes,const ImportOptions& options={});
[[nodiscard]] std::vector<std::string> builtinNames();

class Element final {
public:
    explicit Element(DefinitionPtr definition,std::vector<double> coefficients);
    [[nodiscard]] const DefinitionPtr& definition() const noexcept { return definition_; }
    [[nodiscard]] const std::vector<double>& coefficients() const noexcept { return coefficients_; }
    [[nodiscard]] static Element scalar(DefinitionPtr,double);
    [[nodiscard]] static Element basis(DefinitionPtr,std::size_t index);
private:
    DefinitionPtr definition_;
    std::vector<double> coefficients_;
};
[[nodiscard]] Element add(const Element&,const Element&);
[[nodiscard]] Element subtract(const Element&,const Element&);
[[nodiscard]] Element scale(const Element&,double);
// Divide coordinates directly; a finite quotient need not have a finite
// binary64 reciprocal of its real divisor.
[[nodiscard]] Element divideByReal(const Element&,double);
[[nodiscard]] Element multiply(const Element&,const Element&);
[[nodiscard]] Element conjugate(const Element&);
[[nodiscard]] Element commutator(const Element&,const Element&);
[[nodiscard]] Element associator(const Element&,const Element&,const Element&);
[[nodiscard]] Element power(const Element&,std::int64_t exponent);
// Whole-element real power-series functions. Canonical quadratic presets use
// their exact one-element identity; other algebras require a unit and an exact
// power-associativity certificate. Unknown/declared/numerical claims do not qualify.
[[nodiscard]] Element sine(const Element&);
[[nodiscard]] Element cosine(const Element&);
[[nodiscard]] Element exponential(const Element&);
[[nodiscard]] Element hyperbolicSine(const Element&);
[[nodiscard]] Element hyperbolicCosine(const Element&);
[[nodiscard]] double coordinateNorm(const Element&);
[[nodiscard]] double metricNormSquared(const Element&);
[[nodiscard]] std::vector<double> leftMatrix(const Element&); // Row major, L(a)*x=a*x.
[[nodiscard]] std::vector<double> rightMatrix(const Element&); // Row major, R(a)*x=x*a.

enum class SolveStatus { Unique,IllConditioned,SingularConsistent,SingularInconsistent,NonFinite,ResidualRejected };
[[nodiscard]] std::string_view solveStatusName(SolveStatus) noexcept;
struct SolveOptions {
    double rankRelativeTolerance=1e-13;
    double conditionLimit=1e12;
    double residualTolerance=1e-11;
};
struct SolveResult {
    SolveStatus status=SolveStatus::NonFinite;
    std::optional<Element> solution; // Present only for accepted Unique equation solutions.
    std::size_t rank=0;
    double condition=0,relativeResidual=0;
};
[[nodiscard]] SolveResult solveLeft(const Element& a,const Element& b,const SolveOptions& options={});
[[nodiscard]] SolveResult solveRight(const Element& a,const Element& b,const SolveOptions& options={});
// A verified two-sided reciprocal is distinct from unique equation division.
// In particular, nonzero sedenions have canonical reciprocals despite zero divisors.
[[nodiscard]] Element inverse(const Element&,const SolveOptions& options={});

} // namespace penta::algebra::universal
