#include "algebra_context.hpp"
#include <cmath>
#include <stdexcept>

namespace penta::algebra {
Context::Context(double gamma) : gamma_(gamma) {
    if (!std::isfinite(gamma) || gamma < 0.0 || gamma > 1.0)
        throw std::invalid_argument("A_gamma_v1 requires a finite gamma in [0,1]");
}
}
