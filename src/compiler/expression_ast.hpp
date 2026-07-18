#pragma once

#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include "intrinsic.hpp"
#include "mpf/diagnostic.hpp"
#include "numeric_type.hpp"

namespace mpf::detail {

inline constexpr std::size_t dynamic_extent = std::numeric_limits<std::size_t>::max();

enum class ValueType { unknown, integer, real, boolean, string, null_value, list, tuple, function };

enum class BindingKind { unresolved, variable, function, builtin };

enum class ParameterIntent { none, in, out, inout };

enum class ParameterKind { positional_only, positional_or_keyword, keyword_only };

enum class ComparisonOperator : std::uint8_t {
  none,
  equal,
  not_equal,
  less,
  less_equal,
  greater,
  greater_equal,
  identity,
  not_identity,
  contains,
  not_contains
};

enum class BinaryOperator : std::uint8_t {
  none,
  add,
  subtract,
  multiply,
  divide,
  left_divide,
  floor_divide,
  remainder,
  power,
  logical_and,
  logical_or,
  elementwise_logical_and,
  elementwise_logical_or,
  elementwise_multiply,
  elementwise_divide,
  elementwise_left_divide,
  elementwise_power
};

enum class UnaryOperator : std::uint8_t {
  none,
  positive,
  negative,
  logical_not,
  transpose,
  conjugate_transpose
};

[[nodiscard]] constexpr std::string_view binary_operator_spelling(
    const BinaryOperator operation) noexcept {
  switch (operation) {
    case BinaryOperator::none: return {};
    case BinaryOperator::add: return "+";
    case BinaryOperator::subtract: return "-";
    case BinaryOperator::multiply: return "*";
    case BinaryOperator::divide: return "/";
    case BinaryOperator::left_divide: return "\\";
    case BinaryOperator::floor_divide: return "//";
    case BinaryOperator::remainder: return "%";
    case BinaryOperator::power: return "^";
    case BinaryOperator::logical_and: return "&&";
    case BinaryOperator::logical_or: return "||";
    case BinaryOperator::elementwise_logical_and: return "&";
    case BinaryOperator::elementwise_logical_or: return "|";
    case BinaryOperator::elementwise_multiply: return ".*";
    case BinaryOperator::elementwise_divide: return "./";
    case BinaryOperator::elementwise_left_divide: return ".\\";
    case BinaryOperator::elementwise_power: return ".^";
  }
  return {};
}

[[nodiscard]] constexpr std::string_view comparison_spelling(
    const ComparisonOperator operation) noexcept {
  switch (operation) {
    case ComparisonOperator::none: return {};
    case ComparisonOperator::equal: return "==";
    case ComparisonOperator::not_equal: return "!=";
    case ComparisonOperator::less: return "<";
    case ComparisonOperator::less_equal: return "<=";
    case ComparisonOperator::greater: return ">";
    case ComparisonOperator::greater_equal: return ">=";
    case ComparisonOperator::identity: return "is";
    case ComparisonOperator::not_identity: return "is not";
    case ComparisonOperator::contains: return "in";
    case ComparisonOperator::not_contains: return "not in";
  }
  return {};
}

[[nodiscard]] constexpr bool comparison_is_ordering(const ComparisonOperator operation) noexcept {
  return operation == ComparisonOperator::less || operation == ComparisonOperator::less_equal ||
         operation == ComparisonOperator::greater || operation == ComparisonOperator::greater_equal;
}

[[nodiscard]] constexpr bool comparison_is_identity(const ComparisonOperator operation) noexcept {
  return operation == ComparisonOperator::identity || operation == ComparisonOperator::not_identity;
}

[[nodiscard]] constexpr bool comparison_is_membership(const ComparisonOperator operation) noexcept {
  return operation == ComparisonOperator::contains || operation == ComparisonOperator::not_contains;
}

enum class ExpressionKind {
  invalid,
  identifier,
  number_literal,
  string_literal,
  boolean_literal,
  null_literal,
  omitted_argument,
  unary,
  binary,
  comparison_chain,
  conditional,
  call,
  member,
  index,
  slice,
  list,
  tuple,
  end_index
};

struct ValueMetadata {
  ValueType type{ValueType::unknown};
  ValueType element_type{ValueType::unknown};
  NumericType numeric_type{unknown_numeric_type};
  NumericType element_numeric_type{unknown_numeric_type};
  std::vector<std::size_t> shape;
  bool sequence{false};
  bool list_sequence{false};
  std::vector<ValueMetadata> elements;
};

struct Expression {
  ExpressionKind kind{ExpressionKind::invalid};
  SourceLocation location{};
  std::string value;
  UnaryOperator unary_operation{UnaryOperator::none};
  BinaryOperator operation{BinaryOperator::none};
  ComparisonOperator comparison{ComparisonOperator::none};
  std::vector<ComparisonOperator> comparisons;
  std::vector<Expression> children;
  ValueType inferred_type{ValueType::unknown};
  BindingKind binding{BindingKind::unresolved};
  IntrinsicId intrinsic{IntrinsicId::none};
  ValueType element_type{ValueType::unknown};
  NumericType numeric_type{unknown_numeric_type};
  NumericType element_numeric_type{unknown_numeric_type};
  std::vector<std::size_t> shape;
  std::vector<ValueType> tuple_types;
  std::vector<NumericType> tuple_numeric_types;
  std::vector<ValueType> tuple_element_types;
  std::vector<NumericType> tuple_element_numeric_types;
  std::vector<std::vector<std::size_t>> tuple_shapes;
  bool sequence_is_list{false};
  std::vector<ValueMetadata> sequence_elements;
  std::size_t requested_outputs{1};
  bool matlab_multi_output_call{false};
  std::vector<ParameterIntent> argument_intents;
  std::vector<std::string> argument_names;
  std::vector<bool> argument_optional_forward;
  bool procedure_has_result{false};
  std::size_t index_base{0};
  bool allow_negative_index{false};
  bool column_major{false};
  bool slice_stop_inclusive{false};

  [[nodiscard]] bool valid() const noexcept { return kind != ExpressionKind::invalid; }
};

}  // namespace mpf::detail
