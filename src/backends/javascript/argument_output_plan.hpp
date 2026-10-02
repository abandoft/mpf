#pragma once

#include "lir.hpp"

namespace mpf::detail::javascript {
[[nodiscard]] std::vector<lir::ArgumentOutputPlan> plan_argument_outputs(
    const lir::Statement& statement);
[[nodiscard]] lir::ArgumentExitPlan plan_argument_exit(const lir::Statement& statement);
}  // namespace mpf::detail::javascript
