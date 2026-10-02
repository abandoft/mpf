#include "argument_input_plan.hpp"

#include <algorithm>

namespace mpf::detail::cpp {

bool requires_ordered_matlab_call(const lir::Expression& expression,
                                  const SourceLanguage language) noexcept {
  if (language != SourceLanguage::matlab || expression.kind != ExpressionKind::call ||
      expression.children.size() <= 2U ||
      expression.children.front().binding != BindingKind::function)
    return false;
  // Trivial operands commute. All other operands are conservatively sequenced, including
  // calls, indexing and operations that may throw; no effect inference belongs in the renderer.
  return std::any_of(expression.children.begin() + 1, expression.children.end(),
                     [](const auto& argument) {
                       return argument.kind != ExpressionKind::identifier &&
                              argument.kind != ExpressionKind::number_literal &&
                              argument.kind != ExpressionKind::string_literal &&
                              argument.kind != ExpressionKind::boolean_literal &&
                              argument.kind != ExpressionKind::null_literal &&
                              argument.kind != ExpressionKind::omitted_argument;
                     });
}

bool has_matlab_input_validation(const lir::Statement& statement) noexcept {
  return statement.kind == StatementKind::function &&
         std::any_of(statement.argument_validations.begin(), statement.argument_validations.end(),
                     [](const auto& validation) {
                       return validation.direction == ArgumentDirection::input;
                     });
}

std::vector<lir::ArgumentInputPlan> plan_argument_inputs(const lir::Statement& statement) {
  std::vector<lir::ArgumentInputPlan> result(statement.argument_validations.size());
  for (std::size_t index = 0U; index < statement.argument_validations.size(); ++index) {
    const auto& validation = statement.argument_validations[index];
    if (validation.direction != ArgumentDirection::input ||
        validation.ordinal >= statement.function_abi.parameters.size())
      continue;
    const auto& parameter = statement.function_abi.parameters[validation.ordinal];
    auto& input = result[index];
    input.rank = validation.validated_rank;
    input.dimensions = validation.dimensions;
    input.raw_name = parameter.raw_name;
    input.template_type = parameter.template_parameter;
    input.concrete_type = parameter.concrete_type;
    switch (validation.class_constraint) {
      case ArgumentClassConstraint::matlab_double:
        input.form = lir::ArgumentInputForm::matlab_double;
        break;
      case ArgumentClassConstraint::matlab_logical:
        input.form = lir::ArgumentInputForm::matlab_logical;
        break;
      case ArgumentClassConstraint::none:
        input.form = validation.dimensions_declared ? lir::ArgumentInputForm::matlab_size
                                                    : lir::ArgumentInputForm::direct;
        break;
      case ArgumentClassConstraint::matlab_char: input.form = lir::ArgumentInputForm::direct; break;
    }
  }
  return result;
}

}  // namespace mpf::detail::cpp
