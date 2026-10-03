#pragma once

#include <string_view>
#include <vector>

#include "mir.hpp"

namespace mpf::detail::mir {

// Resolve storage-version phi contracts from actual SSA definitions, never from
// the storage's first declaration. This does not alter the source workspace ABI.
void normalize_control_value_joins(Program& program);

void verify_control_value_joins(const Program& program, std::vector<Diagnostic>& diagnostics,
                                std::string_view stage);

}  // namespace mpf::detail::mir
