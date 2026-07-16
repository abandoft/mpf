#pragma once

#include <vector>

#include "../ir/hir.hpp"

namespace mpf::detail {

[[nodiscard]] std::vector<Diagnostic> analyze_program(hir::Program& program);
[[nodiscard]] const char* to_string(ValueType type) noexcept;

}  // namespace mpf::detail
