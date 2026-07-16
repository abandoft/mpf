#pragma once

#include <limits>
#include <string>
#include <vector>

#include "intrinsic.hpp"
#include "mpf/diagnostic.hpp"

namespace mpf::detail {

inline constexpr std::size_t dynamic_extent = std::numeric_limits<std::size_t>::max();

enum class ValueType { unknown, integer, real, boolean, string, null_value, list, tuple, function };

enum class BindingKind { unresolved, variable, function, builtin };

enum class ParameterIntent { none, in, out, inout };

enum class ParameterKind { positional_only, positional_or_keyword, keyword_only };

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
  tuple
};

struct ValueMetadata {
  ValueType type{ValueType::unknown};
  ValueType element_type{ValueType::unknown};
  std::vector<std::size_t> shape;
  bool sequence{false};
  bool list_sequence{false};
  std::vector<ValueMetadata> elements;
};

struct Expression {
  ExpressionKind kind{ExpressionKind::invalid};
  SourceLocation location{};
  std::string value;
  std::vector<std::string> operators;
  std::vector<Expression> children;
  ValueType inferred_type{ValueType::unknown};
  BindingKind binding{BindingKind::unresolved};
  IntrinsicId intrinsic{IntrinsicId::none};
  ValueType element_type{ValueType::unknown};
  std::vector<std::size_t> shape;
  std::vector<ValueType> tuple_types;
  std::vector<ValueType> tuple_element_types;
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
