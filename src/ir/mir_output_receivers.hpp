#pragma once

#include "mir.hpp"

namespace mpf::detail::mir {

void verify_output_receivers(const Program& program, std::vector<Diagnostic>& diagnostics,
                             std::string_view stage);

}  // namespace mpf::detail::mir
