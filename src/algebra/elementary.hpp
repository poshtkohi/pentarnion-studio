#pragma once
#include "pentarnion.hpp"

namespace penta::algebra {
// Functions of one algebra element, defined by their real-coefficient power
// series. The quadratic identity reduces evaluation to span{1, pure(value)}.
[[nodiscard]] Pentarnion sine(const Pentarnion& value);
[[nodiscard]] Pentarnion cosine(const Pentarnion& value);
[[nodiscard]] Pentarnion exponential(const Pentarnion& value);
[[nodiscard]] Pentarnion hyperbolicSine(const Pentarnion& value);
[[nodiscard]] Pentarnion hyperbolicCosine(const Pentarnion& value);
}
