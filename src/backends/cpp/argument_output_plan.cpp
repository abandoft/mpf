#include "argument_output_plan.hpp"

#include <utility>

namespace mpf::detail::cpp {
std::vector<lir::ArgumentOutputPlan> plan_argument_outputs(const lir::Statement& statement) {
  std::vector<lir::ArgumentOutputPlan> result;
  for (std::size_t declaration = 0U; declaration < statement.argument_validations.size();
       ++declaration) {
    const auto& validation = statement.argument_validations[declaration];
    if (validation.direction != ArgumentDirection::output) continue;
    lir::ArgumentOutputPlan plan;
    plan.declaration = declaration;
    plan.ordinal = validation.ordinal;
    plan.rank = validation.validated_rank;
    plan.dimensions = validation.dimensions;
    switch (validation.class_constraint) {
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
        plan.form = validation.dimensions_declared ? lir::ArgumentOutputForm::matlab_size
                                                   : lir::ArgumentOutputForm::direct;
        break;
      case ArgumentClassConstraint::matlab_char: break;
    }
    result.push_back(std::move(plan));
  }
  return result;
}
}  // namespace mpf::detail::cpp
