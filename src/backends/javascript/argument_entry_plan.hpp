#pragma once
#include "lir.hpp"
namespace mpf::detail::javascript {
[[nodiscard]] std::vector<lir::ArgumentEntryPlan> plan_argument_entries(
    const lir::Statement& statement);
}
