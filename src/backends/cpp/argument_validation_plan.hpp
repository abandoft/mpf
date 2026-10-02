#pragma once

#include "backends/cpp/lir.hpp"

namespace mpf::detail::cpp {

[[nodiscard]] std::vector<std::vector<lir::ValidatorCallPlan>> plan_argument_validators(
    const lir::Statement& statement);

}  // namespace mpf::detail::cpp
