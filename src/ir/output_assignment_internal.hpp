#pragma once

#include "output_assignment.hpp"

namespace mpf::detail::mir::output_assignment_detail {

// Rebuild ownership, binding/storage identity, and exact successful-write sources
// directly from MIR. This does not calculate or trust any assignment states.
[[nodiscard]] OutputAssignmentTable prepare(const Program& program,
                                            const AliasEffectTable& alias_effects);
[[nodiscard]] OutputAssignmentState state_at(const OutputAssignmentTable& table,
                                             MirFunctionId function, std::size_t offset,
                                             std::size_t row, std::size_t output) noexcept;

}  // namespace mpf::detail::mir::output_assignment_detail
