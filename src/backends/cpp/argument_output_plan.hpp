#pragma once

#include "lir.hpp"

namespace mpf::detail::cpp {
[[nodiscard]] std::vector<lir::ArgumentOutputPlan> plan_argument_outputs(
    const lir::Statement& statement);
}  // namespace mpf::detail::cpp
