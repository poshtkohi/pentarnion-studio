#pragma once
#include <string_view>

namespace penta::algebra {
// Immutable metadata only in S0/S1. No numerical product is implemented here.
class Context final {
public:
    explicit Context(double gamma = 1.0);
    [[nodiscard]] double gamma() const noexcept { return gamma_; }
    [[nodiscard]] static constexpr std::string_view id() noexcept { return "A_gamma_v1"; }
    [[nodiscard]] static constexpr int dimension() noexcept { return 5; }
private:
    double gamma_;
};
}
