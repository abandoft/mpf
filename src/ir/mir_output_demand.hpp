#pragma once

#include <string_view>
#include <vector>

#include "mir.hpp"

namespace mpf::detail::mir {

void verify_output_demands(const Program& program, std::vector<Diagnostic>& diagnostics,
                           std::string_view stage);

}  // namespace mpf::detail::mir
