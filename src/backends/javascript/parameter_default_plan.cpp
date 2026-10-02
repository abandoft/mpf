#include "parameter_default_plan.hpp"

namespace mpf::detail::javascript {

std::vector<lir::ParameterDefaultPlan> plan_parameter_defaults(const lir::Statement& statement) {
  std::vector<lir::ParameterDefaultPlan> result(statement.argument_validations.size());
  std::size_t source_index = 0U;
  for (std::size_t validation = 0U; validation < statement.argument_validations.size();
       ++validation) {
    const auto& declaration = statement.argument_validations[validation];
    if (declaration.direction != ArgumentDirection::input ||
        source_index >= statement.source_parameter_defaults.size())
      continue;
    const auto& source = statement.source_parameter_defaults[source_index];
    if (source.parameter != declaration.ordinal) continue;
    result[validation] = {lir::ParameterDefaultForm::undefined_guard, source};
    ++source_index;
  }
  return result;
}

}  // namespace mpf::detail::javascript
