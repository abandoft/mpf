#pragma once

#include "mir.hpp"

namespace mpf::detail::mir {

void verify_parameter_defaults(const Program& program,
                               const std::vector<InstructionId>& presence_instructions,
                               std::vector<Diagnostic>& diagnostics, std::string_view stage);

}  // namespace mpf::detail::mir
