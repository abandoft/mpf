#pragma once

#include "lir.hpp"

namespace mpf::detail::cpp {

std::vector<lir::ParameterDefaultPlan> plan_parameter_defaults(const lir::Statement& statement);

}  // namespace mpf::detail::cpp
