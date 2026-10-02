#pragma once

#include <string_view>

#include "argument_validation.hpp"
#include "mpf/transpiler.hpp"
#include "statement_kind.hpp"

namespace mpf::detail {

[[nodiscard]] constexpr std::string_view argument_range_flag_name(
    const ArgumentRangeBoundary flag) noexcept {
  switch (flag) {
    case ArgumentRangeBoundary::inclusive: return "inclusive";
    case ArgumentRangeBoundary::exclude_lower: return "exclude-lower";
    case ArgumentRangeBoundary::exclude_upper: return "exclude-upper";
    case ArgumentRangeBoundary::exclusive: return "exclusive";
  }
  return {};
}

// A common contract, not a common AST: lookup adapts arena IDs or owned HIR expressions.
// Compare original numeric tokens here. Finite binary64 normalization belongs to semantic
// analysis, so an overflowing source literal must remain a user error, not an invalid-AST error.
template <typename Expression, typename Lookup>
[[nodiscard]] bool validator_operand_matches(const Expression& expression,
                                             const ArgumentValidatorOperandSyntax& operand,
                                             Lookup lookup) {
  if (operand.kind == ArgumentValidatorOperandKind::input_argument) {
    return expression.kind == ExpressionKind::identifier && expression.children.empty() &&
           expression.value == operand.value;
  }
  if (operand.kind != ArgumentValidatorOperandKind::numeric_literal) return false;
  std::string_view token = operand.value;
  const auto* number = &expression;
  if (expression.kind == ExpressionKind::unary && expression.children.size() == 1U &&
      (expression.unary_operation == UnaryOperator::positive ||
       expression.unary_operation == UnaryOperator::negative)) {
    const auto sign = expression.unary_operation == UnaryOperator::positive ? '+' : '-';
    if (token.empty() || token.front() != sign) return false;
    token.remove_prefix(1U);
    number = lookup(expression.children.front());
  }
  return number != nullptr && number->kind == ExpressionKind::number_literal &&
         number->children.empty() && number->value == token;
}

template <typename Statement, typename Lookup>
[[nodiscard]] bool valid_argument_validator_calls(const Statement& statement,
                                                  const SourceLanguage language, Lookup lookup) {
  if ((!statement.argument_declarations.empty() || !statement.argument_validator_calls.empty()) &&
      (language != SourceLanguage::matlab || statement.kind != StatementKind::function)) {
    return false;
  }
  std::size_t cursor = 0U;
  for (std::size_t declaration_index = 0U;
       declaration_index < statement.argument_declarations.size(); ++declaration_index) {
    const auto& declaration = statement.argument_declarations[declaration_index];
    for (std::size_t validator_index = 0U; validator_index < declaration.validators.size();
         ++validator_index) {
      if (cursor >= statement.argument_validator_calls.size()) return false;
      const auto& invocation = statement.argument_validator_calls[cursor++];
      if (invocation.declaration != declaration_index || invocation.validator != validator_index)
        return false;
      const auto& syntax = declaration.validators[validator_index];
      const auto* call = lookup(invocation.expression);
      if (call == nullptr || call->kind != ExpressionKind::call ||
          call->children.size() != 2U + syntax.operands.size() + syntax.range_flags.size()) {
        return false;
      }
      const auto* callee = lookup(call->children[0]);
      const auto* formal = lookup(call->children[1]);
      if (callee == nullptr || formal == nullptr || callee->kind != ExpressionKind::identifier ||
          !callee->children.empty() || callee->value != argument_validator_name(syntax.validator) ||
          formal->kind != ExpressionKind::identifier || !formal->children.empty() ||
          formal->value != declaration.name) {
        return false;
      }
      for (std::size_t index = 0U; index < syntax.operands.size(); ++index) {
        const auto* operand = lookup(call->children[index + 2U]);
        if (operand == nullptr ||
            !validator_operand_matches(*operand, syntax.operands[index], lookup))
          return false;
      }
      for (std::size_t index = 0U; index < syntax.range_flags.size(); ++index) {
        const auto* flag = lookup(call->children[index + 2U + syntax.operands.size()]);
        if (flag == nullptr || flag->kind != ExpressionKind::string_literal ||
            !flag->children.empty())
          return false;
        const std::string_view spelling = flag->value;
        if (spelling.size() < 2U || (spelling.front() != '\'' && spelling.front() != '"') ||
            spelling.front() != spelling.back() ||
            spelling.substr(1U, spelling.size() - 2U) !=
                argument_range_flag_name(syntax.range_flags[index]))
          return false;
      }
    }
  }
  return cursor == statement.argument_validator_calls.size();
}

}  // namespace mpf::detail
