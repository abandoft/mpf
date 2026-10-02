#pragma once

#include "lir.hpp"

namespace mpf::detail::cpp {

bool has_matlab_input_validation(const lir::Statement& statement) noexcept;
std::vector<lir::ArgumentInputPlan> plan_argument_inputs(const lir::Statement& statement);
[[nodiscard]] bool requires_ordered_matlab_call(const lir::Expression& expression,
                                                SourceLanguage language) noexcept;

}  // namespace mpf::detail::cpp
