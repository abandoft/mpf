#pragma once

#include "lir.hpp"

namespace mpf::detail::javascript {

std::vector<lir::ParameterDefaultPlan> plan_parameter_defaults(const lir::Statement& statement);

}  // namespace mpf::detail::javascript
