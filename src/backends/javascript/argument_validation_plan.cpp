#include "backends/javascript/argument_validation_plan.hpp"

#include <utility>

namespace mpf::detail::javascript {

std::vector<std::vector<lir::ValidatorCallPlan>> plan_argument_validators(
    const lir::Statement& statement) {
  std::vector<std::vector<lir::ValidatorCallPlan>> result;
  result.reserve(statement.argument_validations.size());
  std::size_t output = 0U;
  for (const auto& validation : statement.argument_validations) {
    const bool input = validation.direction == ArgumentDirection::input;
    const auto index = input ? validation.ordinal : output++;
    if (input ? index >= statement.source_argument_entries.size()
              : index >= statement.source_argument_outputs.size()) {
      result.emplace_back();
      continue;
    }
    const auto& validators = input ? statement.source_argument_entries[index].validators
                                   : statement.source_argument_outputs[index].validators;
    std::vector<lir::ValidatorCallPlan> calls;
    calls.reserve(validators.size());
    for (const auto& validator : validators) {
      lir::ValidatorCallPlan call;
      call.opcode = static_cast<std::uint8_t>(validator.validator);
      call.range_boundary = static_cast<std::uint8_t>(validator.range_boundary);
      call.source_call = validator.source_call;
      call.source_callee = validator.source_callee;
      call.operands.reserve(validator.operands.size());
      for (const auto& operand : validator.operands) {
        lir::ValidatorOperandPlan planned;
        if (operand.kind == ArgumentValidatorOperandKind::numeric_literal) {
          planned.token = operand.numeric_literal;
        } else {
          planned.form = lir::ValidatorOperandForm::parameter_value;
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

}  // namespace mpf::detail::javascript
