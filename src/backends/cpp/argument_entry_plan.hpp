#pragma once
#include "lir.hpp"
namespace mpf::detail::cpp {
[[nodiscard]] std::vector<lir::ArgumentEntryPlan> plan_argument_entries(
    const lir::Statement& statement);
}
