#include "pentarnion.hpp"

#include <Eigen/SVD>

#include <algorithm>
#include <cmath>
#include <utility>

namespace penta::algebra {
namespace {
using Tensor = std::array<std::array<Coefficients, 5>, 5>;
using EigenMatrix = Eigen::Matrix<double, 5, 5>;
using EigenVector = Eigen::Matrix<double, 5, 1>;

void sameContext(const Pentarnion& left, const Pentarnion& right) {
    if (left.context().gamma() != right.context().gamma())
        throw AlgebraError("R3002", "Cannot mix elements from different A_gamma_v1 contexts.");
}

double checkedDouble(long double value) {
    if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<double>::max())
        throw AlgebraError("R3001", "The algebra result overflows finite binary64 coefficients.");
    const double result = static_cast<double>(value);
    if (value != 0.0L && result == 0.0)
        throw AlgebraError("R3009", "A nonzero algebra result underflows binary64 representation.");
    return result;
}

double maximumCoefficient(const Pentarnion& value) {
    double maximum = 0.0;
    for (const double coefficient : value.coefficients()) maximum = std::max(maximum, std::abs(coefficient));
    return maximum;
}

Tensor productTable(const Context& context) {
    Tensor table{};
    for (std::size_t index = 0; index < 5; ++index) {
        table[0][index][index] = 1.0;
        table[index][0][index] = 1.0;
    }
    for (std::size_t index = 1; index < 5; ++index) table[index][index][0] = -1.0;
    const double gamma = context.gamma();
    const double kappa = 1.0 / std::sqrt(1.0 + gamma * gamma);
    const double sigma = gamma * kappa;
    const double h = 1.0 / std::sqrt(2.0);
    // Exact ordered table from specs/algebras/A_gamma_v1.txt. Coordinates are
    // never pre-scaled by gamma; gamma=0 still has all five basis directions.
    table[1][2] = {0, 0, 0, kappa, sigma};
    table[1][3] = {0, 0, -kappa, 0, -sigma};
    table[1][4] = {0, 0, -h, -h, 0};
    table[2][1] = {0, 0, 0, -kappa, -sigma};
    table[2][3] = {0, kappa, 0, 0, sigma};
    table[2][4] = {0, -h, 0, -h, 0};
    table[3][1] = {0, 0, kappa, 0, sigma};
    table[3][2] = {0, -kappa, 0, 0, -sigma};
    table[3][4] = {0, -h, -h, 0, 0};
    table[4][1] = {0, 0, h, h, 0};
    table[4][2] = {0, h, 0, h, 0};
    table[4][3] = {0, h, h, 0, 0};
    return table;
}

Pentarnion normalized(const Pentarnion& value, double maximum) {
    Coefficients result{};
    if (maximum != 0.0)
        for (std::size_t index = 0; index < 5; ++index)
            result[index] = static_cast<double>(static_cast<long double>(value.coefficients()[index]) / maximum);
    return Pentarnion(value.context(), result);
}

void validateOptions(const DivisionOptions& options) {
    if (!std::isfinite(options.rankRelativeTolerance) || options.rankRelativeTolerance <= 0.0
            || options.rankRelativeTolerance >= 1.0 || !std::isfinite(options.conditionLimit)
            || options.conditionLimit < 1.0 || !std::isfinite(options.residualTolerance)
            || options.residualTolerance <= 0.0 || options.residualTolerance >= 1.0)
        throw AlgebraError("R3010", "Invalid numerical equation-division options.");
}

DivisionResult solve(const Pentarnion& operatorElement, const Pentarnion& rhs,
                     bool left, const DivisionOptions& options) {
    sameContext(operatorElement, rhs);
    validateOptions(options);
    DivisionResult result;
    const double operatorScale = maximumCoefficient(operatorElement);
    const double rhsScale = maximumCoefficient(rhs);
    if (operatorScale == 0.0) {
        result.rank = 0;
        result.relativeResidual = rhsScale == 0.0 ? 0.0 : 1.0;
        result.status = rhsScale == 0.0 ? DivisionStatus::SingularConsistent
                                       : DivisionStatus::SingularInconsistent;
        return result;
    }

    // Scale operator and RHS independently. Neither A nor b/A_scale is ever
    // formed at the original magnitude, so huge/tiny finite systems retain
    // the same numerical rank and conditioning. Recovery of x uses an extended
    // intermediate only after rank/conditioning acceptance.
    const auto reducedElement = normalized(operatorElement, operatorScale);
    const MultiplicationMatrix matrix = left ? leftMatrix(reducedElement) : rightMatrix(reducedElement);
    EigenMatrix a;
    EigenVector b;
    for (std::size_t row = 0; row < 5; ++row) {
        for (std::size_t column = 0; column < 5; ++column)
            a(static_cast<int>(row), static_cast<int>(column)) = matrix[row][column];
        b(static_cast<int>(row)) = rhsScale == 0.0 ? 0.0
            : static_cast<double>(static_cast<long double>(rhs.coefficients()[row]) / rhsScale);
    }
    if (!a.allFinite() || !b.allFinite()) return result;

    Eigen::JacobiSVD<EigenMatrix> decomposition(a, Eigen::ComputeFullU | Eigen::ComputeFullV);
    decomposition.setThreshold(options.rankRelativeTolerance);
    if (decomposition.info() != Eigen::Success) return result;
    result.rank = static_cast<int>(decomposition.rank());
    const double largest = decomposition.singularValues()(0);
    const double smallest = decomposition.singularValues()(4);
    result.condition = smallest > 0.0 ? largest / smallest : std::numeric_limits<double>::infinity();
    const EigenVector reducedSolution = decomposition.solve(b);
    if (!reducedSolution.allFinite()) return result;

    // This candidate is only used for diagnostics until uniqueness and all
    // acceptance checks pass. A rank-deficient pseudoinverse is never exposed.
    const double denominator = largest * reducedSolution.norm() + b.norm();
    const double residual = (a * reducedSolution - b).norm();
    result.relativeResidual = denominator > 0.0 ? residual / denominator : 0.0;
    if (!std::isfinite(result.relativeResidual)) return result;
    if (result.rank != 5) {
        result.status = result.relativeResidual <= options.residualTolerance
            ? DivisionStatus::SingularConsistent : DivisionStatus::SingularInconsistent;
        return result;
    }
    if (!std::isfinite(result.condition) || result.condition > options.conditionLimit) {
        result.status = DivisionStatus::IllConditioned;
        return result;
    }
    if (result.relativeResidual > options.residualTolerance) {
        result.status = DivisionStatus::ResidualRejected;
        return result;
    }

    Coefficients coefficients{};
    try {
        for (std::size_t index = 0; index < 5; ++index) {
            const long double value = rhsScale == 0.0 ? 0.0L
                : static_cast<long double>(reducedSolution(static_cast<int>(index)))
                    * static_cast<long double>(rhsScale) / operatorScale;
            coefficients[index] = checkedDouble(value);
        }
    } catch (const AlgebraError&) {
        result.status = DivisionStatus::NonFinite;
        return result;
    }
    // Validate the rounded binary64 result, rather than reporting only the
    // residual of an unreturned intermediate. All terms remain scale-free.
    long double squaredResidual = 0.0L;
    long double squaredSolution = 0.0L;
    for (std::size_t row = 0; row < 5; ++row) {
        const long double scaledX = rhsScale == 0.0 ? 0.0L
            : static_cast<long double>(coefficients[row]) * operatorScale / rhsScale;
        squaredSolution += scaledX * scaledX;
        long double residualComponent = -static_cast<long double>(b(static_cast<int>(row)));
        for (std::size_t column = 0; column < 5; ++column) {
            const long double scaled = rhsScale == 0.0 ? 0.0L
                : static_cast<long double>(coefficients[column]) * operatorScale / rhsScale;
            residualComponent += static_cast<long double>(matrix[row][column]) * scaled;
        }
        squaredResidual += residualComponent * residualComponent;
    }
    const long double roundedDenominator = static_cast<long double>(largest) * std::sqrt(squaredSolution)
                                         + static_cast<long double>(b.norm());
    result.relativeResidual = roundedDenominator > 0.0L
        ? static_cast<double>(std::sqrt(squaredResidual) / roundedDenominator) : 0.0;
    if (!std::isfinite(result.relativeResidual) || result.relativeResidual > options.residualTolerance) {
        result.status = DivisionStatus::ResidualRejected;
        return result;
    }
    result.status = DivisionStatus::Unique;
    result.solution.emplace(operatorElement.context(), coefficients);
    return result;
}

Pentarnion requireUnique(const DivisionResult& result) {
    if (result.status == DivisionStatus::Unique && result.solution) return *result.solution;
    switch (result.status) {
    case DivisionStatus::IllConditioned:
        throw AlgebraError("R3007", "Equation division is numerically ill-conditioned; no quotient was accepted.");
    case DivisionStatus::SingularConsistent:
        throw AlgebraError("R3005", "Equation division has no numerically unique solution.");
    case DivisionStatus::SingularInconsistent:
        throw AlgebraError("R3006", "Equation division is numerically inconsistent.");
    case DivisionStatus::ResidualRejected:
        throw AlgebraError("R3008", "Equation division failed its relative-residual acceptance test.");
    default:
        throw AlgebraError("R3001", "Equation division produced an unrepresentable or nonfinite result.");
    }
}
}

AlgebraError::AlgebraError(std::string errorCode, const std::string& message)
    : std::runtime_error(message), code(std::move(errorCode)) {}

Pentarnion::Pentarnion(Context context, Coefficients coefficients)
    : context_(context), coefficients_(coefficients) {
    for (const double value : coefficients_)
        if (!std::isfinite(value))
            throw AlgebraError("R3001", "Pentarnion coefficients must be finite real binary64 values.");
}

Pentarnion Pentarnion::basis(const Context& context, std::size_t index) {
    if (index >= 5) throw AlgebraError("R3004", "A basis index must be in 0..4.");
    Coefficients coefficients{};
    coefficients[index] = 1.0;
    return Pentarnion(context, coefficients);
}

Pentarnion Pentarnion::scalar(const Context& context, double value) {
    return Pentarnion(context, {value, 0, 0, 0, 0});
}

Pentarnion add(const Pentarnion& left, const Pentarnion& right) {
    sameContext(left, right);
    Coefficients result{};
    for (std::size_t index = 0; index < 5; ++index)
        result[index] = checkedDouble(static_cast<long double>(left.coefficients()[index]) + right.coefficients()[index]);
    return Pentarnion(left.context(), result);
}

Pentarnion subtract(const Pentarnion& left, const Pentarnion& right) {
    sameContext(left, right);
    Coefficients result{};
    for (std::size_t index = 0; index < 5; ++index)
        result[index] = checkedDouble(static_cast<long double>(left.coefficients()[index]) - right.coefficients()[index]);
    return Pentarnion(left.context(), result);
}

Pentarnion scale(const Pentarnion& value, double factor) {
    if (!std::isfinite(factor)) throw AlgebraError("R3001", "A scaling factor must be finite.");
    Coefficients result{};
    for (std::size_t index = 0; index < 5; ++index)
        result[index] = checkedDouble(static_cast<long double>(value.coefficients()[index]) * factor);
    return Pentarnion(value.context(), result);
}

Pentarnion multiply(const Pentarnion& left, const Pentarnion& right) {
    sameContext(left, right);
    const Tensor table = productTable(left.context());
    std::array<long double, 5> accumulated{};
    // For the supported GCC Windows/Linux builds, long double can hold every
    // product of two finite binary64 coefficients, including cancelling terms.
    for (std::size_t a = 0; a < 5; ++a)
        for (std::size_t b = 0; b < 5; ++b) {
            const long double product = static_cast<long double>(left.coefficients()[a]) * right.coefficients()[b];
            for (std::size_t c = 0; c < 5; ++c)
                accumulated[c] += product * static_cast<long double>(table[a][b][c]);
        }
    Coefficients result{};
    for (std::size_t index = 0; index < 5; ++index) result[index] = checkedDouble(accumulated[index]);
    return Pentarnion(left.context(), result);
}

Pentarnion conjugate(const Pentarnion& value) {
    Coefficients result = value.coefficients();
    for (std::size_t index = 1; index < 5; ++index) result[index] = -result[index];
    return Pentarnion(value.context(), result);
}

double scalarPart(const Pentarnion& value) noexcept { return value.coefficients()[0]; }

Pentarnion purePart(const Pentarnion& value) {
    Coefficients result = value.coefficients();
    result[0] = 0.0;
    return Pentarnion(value.context(), result);
}

double coefficient(const Pentarnion& value, std::size_t index) {
    if (index >= 5) throw AlgebraError("R3004", "A coefficient index must be in 0..4.");
    return value.coefficients()[index];
}

double normSquared(const Pentarnion& value) {
    long double sum = 0.0L;
    for (const double coefficient : value.coefficients())
        sum += static_cast<long double>(coefficient) * coefficient;
    return checkedDouble(sum);
}

Pentarnion inverse(const Pentarnion& value) {
    const double maximum = maximumCoefficient(value);
    if (maximum == 0.0) throw AlgebraError("R3003", "The zero Pentarnion has no canonical reciprocal.");
    std::array<long double, 5> reduced{};
    long double squared = 0.0L;
    for (std::size_t index = 0; index < 5; ++index) {
        reduced[index] = static_cast<long double>(value.coefficients()[index]) / maximum;
        squared += reduced[index] * reduced[index];
    }
    Coefficients result{};
    for (std::size_t index = 0; index < 5; ++index)
        result[index] = checkedDouble((index == 0 ? reduced[index] : -reduced[index]) / squared / maximum);
    return Pentarnion(value.context(), result);
}

Pentarnion power(const Pentarnion& value, std::int64_t exponent) {
    Pentarnion result = Pentarnion::scalar(value.context(), 1.0);
    if (exponent == 0) return result;
    Pentarnion factor = exponent < 0 ? inverse(value) : value;
    std::uint64_t magnitude = exponent < 0
        ? static_cast<std::uint64_t>(-(exponent + 1)) + 1u
        : static_cast<std::uint64_t>(exponent);
    while (magnitude != 0) {
        if ((magnitude & 1u) != 0u) result = multiply(result, factor);
        magnitude >>= 1u;
        if (magnitude != 0) factor = multiply(factor, factor);
    }
    return result;
}

Pentarnion commutator(const Pentarnion& left, const Pentarnion& right) {
    return subtract(multiply(left, right), multiply(right, left));
}

Pentarnion associator(const Pentarnion& left, const Pentarnion& middle, const Pentarnion& right) {
    sameContext(left, middle);
    sameContext(left, right);
    return subtract(multiply(multiply(left, middle), right), multiply(left, multiply(middle, right)));
}

MultiplicationMatrix leftMatrix(const Pentarnion& value) {
    MultiplicationMatrix result{};
    for (std::size_t column = 0; column < 5; ++column) {
        const auto product = multiply(value, Pentarnion::basis(value.context(), column));
        for (std::size_t row = 0; row < 5; ++row) result[row][column] = product.coefficients()[row];
    }
    return result;
}

MultiplicationMatrix rightMatrix(const Pentarnion& value) {
    MultiplicationMatrix result{};
    for (std::size_t column = 0; column < 5; ++column) {
        const auto product = multiply(Pentarnion::basis(value.context(), column), value);
        for (std::size_t row = 0; row < 5; ++row) result[row][column] = product.coefficients()[row];
    }
    return result;
}

DivisionResult rightDivide(const Pentarnion& numerator, const Pentarnion& denominator, DivisionOptions options) {
    return solve(denominator, numerator, false, options);
}

DivisionResult leftDivide(const Pentarnion& left, const Pentarnion& rhs, DivisionOptions options) {
    return solve(left, rhs, true, options);
}

Pentarnion rightQuotient(const Pentarnion& numerator, const Pentarnion& denominator, DivisionOptions options) {
    sameContext(numerator, denominator);
    if (maximumCoefficient(denominator) == 0.0)
        throw AlgebraError("R3003", "Right equation division by the zero Pentarnion is undefined.");
    return requireUnique(rightDivide(numerator, denominator, options));
}

Pentarnion leftQuotient(const Pentarnion& left, const Pentarnion& rhs, DivisionOptions options) {
    sameContext(left, rhs);
    if (maximumCoefficient(left) == 0.0)
        throw AlgebraError("R3003", "Left equation division by the zero Pentarnion is undefined.");
    return requireUnique(leftDivide(left, rhs, options));
}

const char* divisionStatusName(DivisionStatus status) noexcept {
    switch (status) {
    case DivisionStatus::Unique: return "unique";
    case DivisionStatus::IllConditioned: return "ill-conditioned";
    case DivisionStatus::SingularConsistent: return "singular-consistent/nonunique";
    case DivisionStatus::SingularInconsistent: return "singular-inconsistent";
    case DivisionStatus::NonFinite: return "nonfinite/unrepresentable";
    case DivisionStatus::ResidualRejected: return "relative-residual-rejected";
    }
    return "unknown";
}

} // namespace penta::algebra
