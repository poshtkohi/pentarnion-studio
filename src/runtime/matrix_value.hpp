#pragma once
#include "algebra/pentarnion.hpp"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <random>
#include <vector>

namespace penta::runtime::detail {
// Immutable values make workspace snapshots and cancellation rollback cheap.
// Scalars stay distinct from 1x1 arrays. Storage is row-major; language linear
// indices are explicitly converted from MATLAB's column-major convention.
struct DenseMatrix {
    std::size_t rows = 0, columns = 0;
    bool pentarnion = false;
    bool logical = false; // Explicit masks are distinct from ordinary numeric index lists.
    std::vector<algebra::Pentarnion> values;
    std::vector<std::size_t> fixedShape{}; // Explicit contracts only; ordinary arrays remain dynamic.
};
using MatrixValue = std::shared_ptr<const DenseMatrix>;
struct RandomState {
    static constexpr std::uint64_t defaultSeed = 5489;
    std::mt19937_64 engine{defaultSeed};
    double uniform() { return static_cast<double>(engine() >> 11) * 0x1.0p-53; }
};
}
