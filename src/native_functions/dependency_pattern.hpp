#pragma once
#include "native_callback.hpp"
#include <cstdint>

namespace penta::native_functions {
// A conservative structural superset of possible coordinate dependencies.
// Entries are obtained from the checked AST, never from numerical samples.
// Rows/columns/CSR offsets are zero based; the language adapter exposes one based.
struct DependencyPattern {
    std::size_t rows=0,columns=0;
    std::vector<std::size_t> rowOffsets,columnIndices;
    std::string key;
    bool includesControlDependencies=false;
    [[nodiscard]] bool contains(std::size_t row,std::size_t column)const;
};
[[nodiscard]] DependencyPattern dependencyPattern(const language::FunctionDefinition&);
}
