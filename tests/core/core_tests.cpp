#include "algebra/algebra_context.hpp"
#include "language/language_version.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

int main() {
    using penta::algebra::Context;
    const Context quaternionSector{0.0}, middle{0.5}, pentarnion{1.0};
    if (quaternionSector.gamma() != 0.0 || middle.gamma() != 0.5 ||
        pentarnion.gamma() != 1.0 || Context::dimension() != 5 ||
        Context::id() != "A_gamma_v1") return 1;
    for (const double invalid : {-0.1, 1.1, std::numeric_limits<double>::infinity(),
                                 std::numeric_limits<double>::quiet_NaN()}) {
        try { const Context unexpected{invalid}; (void)unexpected; return 2; }
        catch (const std::invalid_argument&) { }
    }
    if (penta::language::version() != "0.2" ||
        !penta::language::staticAnalysisAvailable()) return 3;
    std::cout << "Headless metadata contract passed.\n";
    return 0;
}
