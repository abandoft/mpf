#include "argument_output_plan.hpp"

#include <algorithm>
#include <utility>

namespace mpf::detail::cpp {
std::vector<lir::ArgumentOutputPlan> plan_argument_outputs(const lir::Statement& statement) {
  std::vector<lir::ArgumentOutputPlan> result;
  for (std::size_t declaration = 0U; declaration < statement.argument_validations.size();
       ++declaration) {
    const auto& validation = statement.argument_validations[declaration];
    if (validation.direction != ArgumentDirection::output) continue;
    if (result.size() >= statement.source_argument_outputs.size()) return {};
    const auto& source = statement.source_argument_outputs[result.size()];
    lir::ArgumentOutputPlan plan;
    plan.declaration = declaration;
    plan.ordinal = validation.ordinal;
    plan.source = source;
    plan.rank = source.rank;
    plan.dimensions = source.dimensions;
    switch (source.class_constraint) {
      case ArgumentClassConstraint::matlab_double: {
        const auto& numeric = plan.rank == 0U ? statement.return_numeric_types
                                              : statement.return_element_numeric_types;
        const bool complex = plan.ordinal < numeric.size() &&
                             numeric[plan.ordinal].complexity == NumericComplexity::complex;
        plan.form =
            complex ? lir::ArgumentOutputForm::matlab_size : lir::ArgumentOutputForm::matlab_double;
        break;
      }
      case ArgumentClassConstraint::matlab_logical:
        plan.form = lir::ArgumentOutputForm::matlab_logical;
        break;
      case ArgumentClassConstraint::none:
        plan.form = source.dimensions_declared ? lir::ArgumentOutputForm::matlab_size
                                               : lir::ArgumentOutputForm::direct;
        break;
      case ArgumentClassConstraint::matlab_char: break;
    }
    result.push_back(std::move(plan));
  }
  return result;
}

lir::ArgumentExitPlan plan_argument_exit(const lir::Statement& statement) {
  lir::ArgumentExitPlan result;
  if (!statement.source_argument_exit.merge.valid()) return result;
  result.source = statement.source_argument_exit;
  const bool early = std::any_of(result.source.returns.begin(), result.source.returns.end(),
                                 [](const auto& source) { return !source.implicit; });
  result.form = early ? lir::ArgumentExitForm::labeled_scope : lir::ArgumentExitForm::shared_return;
  return result;
}
}  // namespace mpf::detail::cpp
