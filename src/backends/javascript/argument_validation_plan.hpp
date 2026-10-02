#pragma once

#include "backends/javascript/lir.hpp"

namespace mpf::detail::javascript {

[[nodiscard]] std::vector<std::vector<lir::ValidatorCallPlan>> plan_argument_validators(
    const lir::Statement& statement);

}  // namespace mpf::detail::javascript
