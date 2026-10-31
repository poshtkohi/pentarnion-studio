#pragma once
#include "algebra/universal.hpp"
#include <filesystem>
#include <string_view>

namespace penta::language {
// Both arguments are exact, already captured snapshots. No file is opened and
// no user source or formula is evaluated by this conversion.
[[nodiscard]] algebra::universal::DefinitionPtr algebraDefinitionFromTable(
    std::string_view metadata,std::string_view tableBytes,const std::filesystem::path& tableFormat,
    const std::string& sheet,const algebra::universal::ImportOptions& options={});
}
