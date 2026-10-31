#pragma once
#include <string_view>
namespace penta::language {
[[nodiscard]] std::string_view version() noexcept;
[[nodiscard]] constexpr bool staticAnalysisAvailable() noexcept { return true; }
}
