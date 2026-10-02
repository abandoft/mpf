#include <algorithm>

#include "analyzer_internal.hpp"
#include "argument_validator_contract.hpp"
#include "compiler/argument_validator_catalog.hpp"

namespace mpf::detail::semantic_internal {

void Analyzer::plan_matlab_argument_validators(Statement& function,
                                               const std::size_t declaration_index,
                                               const std::size_t call_offset,
                                               ArgumentValidationPlan& plan) {
  const auto& declaration = function.argument_declarations[declaration_index];
  const auto& facts = semantic(semantics_, function);
  plan.validators.reserve(declaration.validators.size());
  for (std::size_t index = 0U; index < declaration.validators.size(); ++index) {
    const auto call_index = call_offset + index;
    if (call_index >= function.argument_validator_calls.size()) continue;
    const auto& call = function.argument_validator_calls[call_index].expression;
    if (call.children.empty()) continue;
    const auto& callee = call.children.front();
    const auto* use = names_.reference(callee.id);
    const auto* definition = find_argument_validator(callee.value);
    if (use == nullptr || use->binding != BindingKind::builtin ||
        !use->argument_validator.has_value()) {
      if (use != nullptr && use->symbol.valid())
        diagnose(declaration.line, "MPF2062",
                 "Matlab validator '" + callee.value +
                     "' resolves to a source binding; user-defined validator execution is not yet "
                     "supported");
      // Unresolved callee diagnostics are produced by ordinary expression/name analysis.
      continue;
    }
    if (definition == nullptr || *use->argument_validator != definition->validator) continue;
    const auto version = program_.semantics.language_version.automatic()
                             ? LanguageVersion{2024, 2}
                             : program_.semantics.language_version;
    if (version < definition->minimum_version) {
      diagnose(declaration.line, "MPF1201",
               "Matlab validator '" + callee.value + "' requires Matlab " +
                   std::string(definition->minimum_release) + " or newer");
      continue;
    }
    const auto contract =
        semantic::decode_standard_validator_call(call, declaration.name, definition->validator);
    if (contract.error != semantic::ValidatorCallError::none) {
      diagnose(declaration.line, "MPF2060",
               std::string(semantic::validator_call_error_message(contract.error)));
      continue;
    }
    ArgumentValidatorPlan validator;
    validator.validator = definition->validator;
    validator.source_call = call.id;
    validator.source_callee = callee.id;
    validator.range_boundary = contract.range_boundary;
    validator.operands.reserve(contract.operands.size());
    for (const auto& source : contract.operands) {
      ArgumentValidatorOperandPlan operand;
      operand.kind = source.kind;
      if (source.kind == ArgumentValidatorOperandKind::numeric_literal) {
        const auto normalized = normalize_argument_numeric_literal(source.value);
        if (!normalized.has_value())
          diagnose(declaration.line, "MPF2060",
                   "Matlab validator threshold literal must represent a finite binary64 value");
        operand.numeric_literal = normalized.value_or("0.0");
      } else {
        const auto referenced =
            std::find(function.parameters.begin(), function.parameters.end(), source.value);
        const auto ordinal =
            static_cast<std::size_t>(std::distance(function.parameters.begin(), referenced));
        const bool visible =
            ordinal < function.parameters.size() &&
            (declaration.direction == ArgumentDirection::output || ordinal < plan.ordinal);
        const bool scalar_numeric =
            visible && ordinal < facts.parameter_types.size() &&
            ordinal < facts.parameter_shapes.size() &&
            scalar_argument_validator_formal(facts.argument_validations, ordinal,
                                             facts.parameter_types[ordinal],
                                             facts.parameter_shapes[ordinal].empty());
        const auto* reference = names_.reference(source.origin);
        const auto* parameter = names_.use(function.id, NameRole::parameter, ordinal);
        if (!scalar_numeric || reference == nullptr || parameter == nullptr ||
            reference->symbol != parameter->symbol) {
          diagnose(declaration.line, "MPF2060",
                   "parameterized Matlab validator threshold '" + source.value +
                       "' must name an earlier scalar numeric/logical input argument");
          operand.kind = ArgumentValidatorOperandKind::numeric_literal;
          operand.numeric_literal = "0.0";
        } else {
          operand.input_ordinal = ordinal;
        }
      }
      validator.operands.push_back(std::move(operand));
    }
    plan.validators.push_back(std::move(validator));
  }
}

}  // namespace mpf::detail::semantic_internal
