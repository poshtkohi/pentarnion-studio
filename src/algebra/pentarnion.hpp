#pragma once

#include "algebra_context.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>

namespace penta::algebra {

class AlgebraError final : public std::runtime_error {
public:
    AlgebraError(std::string errorCode, const std::string& message);
    std::string code;
};

using Coefficients = std::array<double, 5>;
using MultiplicationMatrix = std::array<std::array<double, 5>, 5>; // [row][column]

class Pentarnion final {
public:
    explicit Pentarnion(Context context = Context{}, Coefficients coefficients = {});
    [[nodiscard]] const Context& context() const noexcept { return context_; }
    [[nodiscard]] const Coefficients& coefficients() const noexcept { return coefficients_; }
    [[nodiscard]] static Pentarnion basis(const Context& context, std::size_t index);
    [[nodiscard]] static Pentarnion scalar(const Context& context, double value);
private:
    Context context_;
    Coefficients coefficients_;
};

[[nodiscard]] Pentarnion add(const Pentarnion& left, const Pentarnion& right);
[[nodiscard]] Pentarnion subtract(const Pentarnion& left, const Pentarnion& right);
[[nodiscard]] Pentarnion scale(const Pentarnion& value, double factor);
[[nodiscard]] Pentarnion multiply(const Pentarnion& left, const Pentarnion& right);
[[nodiscard]] Pentarnion conjugate(const Pentarnion& value);
[[nodiscard]] double scalarPart(const Pentarnion& value) noexcept;
[[nodiscard]] Pentarnion purePart(const Pentarnion& value);
[[nodiscard]] double coefficient(const Pentarnion& value, std::size_t index);
[[nodiscard]] double normSquared(const Pentarnion& value);
[[nodiscard]] Pentarnion inverse(const Pentarnion& value);
[[nodiscard]] Pentarnion power(const Pentarnion& value, std::int64_t exponent);
[[nodiscard]] Pentarnion commutator(const Pentarnion& left, const Pentarnion& right);
[[nodiscard]] Pentarnion associator(const Pentarnion& left, const Pentarnion& middle,
                                   const Pentarnion& right);
[[nodiscard]] MultiplicationMatrix leftMatrix(const Pentarnion& value);
[[nodiscard]] MultiplicationMatrix rightMatrix(const Pentarnion& value);

enum class DivisionStatus {
    Unique, IllConditioned, SingularConsistent, SingularInconsistent,
    NonFinite, ResidualRejected
};

struct DivisionOptions {
    // Rank is estimated on A/max(abs(A)), relative to its largest singular value.
    double rankRelativeTolerance = 64.0 * std::numeric_limits<double>::epsilon();
    double conditionLimit = 67108864.0; // 1/sqrt(binary64 epsilon)
    double residualTolerance = 512.0 * std::numeric_limits<double>::epsilon();
};

struct DivisionResult {
    DivisionStatus status = DivisionStatus::NonFinite;
    std::optional<Pentarnion> solution; // present only for an accepted Unique solve
    int rank = 0;
    double relativeResidual = std::numeric_limits<double>::infinity();
    double condition = std::numeric_limits<double>::infinity();
};

// Right division solves x*denominator=numerator; left division solves left*x=rhs.
// These never implement equation division by multiplying a canonical reciprocal.
[[nodiscard]] DivisionResult rightDivide(const Pentarnion& numerator,
                                         const Pentarnion& denominator,
                                         DivisionOptions options = {});
[[nodiscard]] DivisionResult leftDivide(const Pentarnion& left, const Pentarnion& rhs,
                                        DivisionOptions options = {});
[[nodiscard]] Pentarnion rightQuotient(const Pentarnion& numerator,
                                      const Pentarnion& denominator,
                                      DivisionOptions options = {});
[[nodiscard]] Pentarnion leftQuotient(const Pentarnion& left, const Pentarnion& rhs,
                                     DivisionOptions options = {});

[[nodiscard]] const char* divisionStatusName(DivisionStatus status) noexcept;

} // namespace penta::algebra
