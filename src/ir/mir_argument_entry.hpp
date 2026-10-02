#pragma once

#include "mir.hpp"

namespace mpf::detail::mir {

[[nodiscard]] const ArgumentOperation* argument_operation(const Program& program,
                                                          InstructionId instruction) noexcept;
void verify_argument_entries(const Program& program, std::vector<Diagnostic>& diagnostics,
                             std::string_view stage);

}  // namespace mpf::detail::mir
