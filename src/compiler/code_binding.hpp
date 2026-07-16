#pragma once

#include <vector>

#include "../ir/mir.hpp"
#include "binding.hpp"

namespace mpf::detail {

[[nodiscard]] std::vector<Diagnostic> validate_code_bindings(const mir::Program& program,
                                                             CodeBindingLookup lookup,
                                                             std::string_view target_name);

}  // namespace mpf::detail
