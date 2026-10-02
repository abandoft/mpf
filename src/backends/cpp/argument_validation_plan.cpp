#include "backends/cpp/argument_validation_plan.hpp"

#include <utility>

namespace mpf::detail::cpp {

std::vector<std::vector<lir::ValidatorCallPlan>> plan_argument_validators(
    const lir::Statement& statement) {
  std::vector<std::vector<lir::ValidatorCallPlan>> result;
  result.reserve(statement.argument_validations.size());
  for (const auto& validation : statement.argument_validations) {
    std::vector<lir::ValidatorCallPlan> calls;
    calls.reserve(validation.validators.size());
    for (const auto& validator : validation.validators) {
      lir::ValidatorCallPlan call;
      call.opcode = static_cast<std::uint8_t>(validator.validator);
      call.range_boundary = static_cast<std::uint8_t>(validator.range_boundary);
      call.operands.reserve(validator.operands.size());
      for (const auto& operand : validator.operands) {
        lir::ValidatorOperandPlan planned;
        if (operand.kind == ArgumentValidatorOperandKind::numeric_literal) {
          planned.token = operand.numeric_literal;
        } else {
          planned.form = operand.input_ordinal < statement.function_abi.parameters.size() &&
                                 statement.function_abi.parameters[operand.input_ordinal].passing ==
                                     lir::ParameterPassing::optional_reference
                             ? lir::ValidatorOperandForm::optional_parameter_value
                             : lir::ValidatorOperandForm::parameter_value;
          if (validator.validator == ArgumentValidator::in_range) {
            planned.form = planned.form == lir::ValidatorOperandForm::optional_parameter_value
                               ? lir::ValidatorOperandForm::optional_parameter_real_component
                               : lir::ValidatorOperandForm::parameter_real_component;
          }
          if (operand.input_ordinal < statement.parameters.size())
            planned.token = statement.parameters[operand.input_ordinal];
          if (operand.input_ordinal < statement.parameter_symbols.size())
            planned.symbol = statement.parameter_symbols[operand.input_ordinal];
        }
        call.operands.push_back(std::move(planned));
      }
      calls.push_back(std::move(call));
    }
    result.push_back(std::move(calls));
  }
  return result;
}

}  // namespace mpf::detail::cpp
