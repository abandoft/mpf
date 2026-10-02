#include "argument_validator_contract.hpp"

#include <utility>

namespace mpf::detail::semantic {
namespace {

std::optional<ValidatorSourceOperand> source_operand(const hir::Expression& expression) {
  if (expression.kind == ExpressionKind::identifier && expression.children.empty())
    return ValidatorSourceOperand{ArgumentValidatorOperandKind::input_argument, expression.value,
                                  expression.id};
  const auto* number = &expression;
  std::string sign;
  if (expression.kind == ExpressionKind::unary && expression.children.size() == 1U &&
      (expression.unary_operation == UnaryOperator::positive ||
       expression.unary_operation == UnaryOperator::negative)) {
    sign = expression.unary_operation == UnaryOperator::positive ? "+" : "-";
    number = &expression.children.front();
  }
  if (number->kind != ExpressionKind::number_literal || !number->children.empty())
    return std::nullopt;
  auto token = sign + number->value;
  if (!valid_argument_numeric_literal(token)) return std::nullopt;
  return ValidatorSourceOperand{ArgumentValidatorOperandKind::numeric_literal, std::move(token),
                                expression.id};
}

std::optional<ArgumentRangeBoundary> range_flag(const hir::Expression& expression) {
  if (expression.kind != ExpressionKind::string_literal || !expression.children.empty())
    return std::nullopt;
  const std::string_view token = expression.value;
  if (token.size() < 2U || (token.front() != '\'' && token.front() != '"') ||
      token.front() != token.back())
    return std::nullopt;
  const auto flag = token.substr(1U, token.size() - 2U);
  if (flag == "inclusive") return ArgumentRangeBoundary::inclusive;
  if (flag == "exclusive") return ArgumentRangeBoundary::exclusive;
  if (flag == "exclude-lower") return ArgumentRangeBoundary::exclude_lower;
  if (flag == "exclude-upper") return ArgumentRangeBoundary::exclude_upper;
  return std::nullopt;
}

}  // namespace

ValidatorCallContract decode_standard_validator_call(const hir::Expression& expression,
                                                     const std::string_view formal,
                                                     const ArgumentValidator validator) {
  ValidatorCallContract result;
  const auto operands = argument_validator_operand_count(validator);
  const auto minimum = 2U + operands.value_or(0U);
  const auto maximum = minimum + (validator == ArgumentValidator::in_range ? 2U : 0U);
  if (!operands.has_value() || expression.kind != ExpressionKind::call ||
      expression.children.size() < minimum || expression.children.size() > maximum) {
    result.error = ValidatorCallError::arity;
    return result;
  }
  const auto& value = expression.children[1];
  if (value.kind != ExpressionKind::identifier || !value.children.empty() ||
      value.value != formal) {
    result.error = ValidatorCallError::validated_argument;
    return result;
  }
  result.operands.reserve(*operands);
  for (std::size_t index = 2U; index < minimum; ++index) {
    auto operand = source_operand(expression.children[index]);
    if (!operand.has_value()) {
      result.error = ValidatorCallError::operand;
      return result;
    }
    result.operands.push_back(std::move(*operand));
  }
  std::uint8_t exclusions = 0U;
  for (auto index = minimum; index < expression.children.size(); ++index) {
    const auto flag = range_flag(expression.children[index]);
    if (!flag.has_value()) {
      result.error = ValidatorCallError::range_flag;
      return result;
    }
    exclusions |= static_cast<std::uint8_t>(*flag);
  }
  result.range_boundary = static_cast<ArgumentRangeBoundary>(exclusions);
  return result;
}

std::string_view validator_call_error_message(const ValidatorCallError error) noexcept {
  switch (error) {
    case ValidatorCallError::none: return {};
    case ValidatorCallError::arity:
      return "Matlab standard validator call has incorrect operand arity";
    case ValidatorCallError::validated_argument:
      return "Matlab standard validator call must name the declared argument first";
    case ValidatorCallError::operand:
      return "Matlab standard validator thresholds must be decimal scalar literals or earlier "
             "scalar inputs; general expressions are not yet supported";
    case ValidatorCallError::range_flag:
      return "Matlab mustBeInRange flags must be literal inclusive, exclusive, exclude-lower, or "
             "exclude-upper text";
  }
  return {};
}

}  // namespace mpf::detail::semantic
