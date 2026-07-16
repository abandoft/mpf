#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "../compiler/assignment_pattern.hpp"
#include "../compiler/ir.hpp"
#include "ids.hpp"
#include "semantics.hpp"

namespace mpf::detail::hir {

struct Expression {
  HirNodeId id{};
  SourceLocation location{};
  ExpressionKind kind{ExpressionKind::invalid};
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
  bool multi_output_call{false};
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

struct CaseSelector {
  Expression lower;
  bool has_lower{false};
  Expression upper;
  bool has_upper{false};
  bool range{false};
};

struct Statement {
  HirNodeId id{};
  StatementKind kind{StatementKind::expression};
  std::size_t line{1};
  std::string name;
  Expression expression;
  bool has_expression{false};
  bool procedure_call{false};
  Expression secondary_expression;
  bool has_secondary_expression{false};
  Expression tertiary_expression;
  bool has_tertiary_expression{false};
  bool inclusive_stop{false};
  bool retain_last_loop_value{true};
  ValueType declared_type{ValueType::unknown};
  ValueType element_type{ValueType::unknown};
  ValueType previous_type{ValueType::unknown};
  ValueType previous_element_type{ValueType::unknown};
  ParameterIntent parameter_intent{ParameterIntent::none};
  bool optional_parameter{false};
  bool dummy_parameter{false};
  std::vector<std::size_t> shape;
  std::size_t index_base{0};
  bool allow_negative_index{false};
  Expression target_expression;
  bool has_target_expression{false};
  std::vector<std::string> parameters;
  std::vector<ParameterKind> parameter_kinds;
  std::vector<Expression> parameter_defaults;
  std::vector<ParameterIntent> parameter_intents;
  std::vector<bool> parameter_optional;
  std::vector<ValueType> parameter_types;
  std::vector<ValueType> parameter_element_types;
  std::vector<std::vector<std::size_t>> parameter_shapes;
  std::vector<std::string> return_names;
  bool has_value_return{false};
  std::vector<ValueType> return_types;
  std::vector<ValueType> return_element_types;
  std::vector<std::vector<std::size_t>> return_shapes;
  bool return_sequence_is_list{false};
  std::vector<ValueMetadata> return_sequence_elements;
  std::vector<std::string> target_names;
  AssignmentPattern target_pattern;
  bool has_target_pattern{false};
  std::vector<ValueType> target_types;
  std::vector<ValueType> target_element_types;
  std::vector<std::vector<std::size_t>> target_shapes;
  std::vector<ValueType> target_previous_types;
  std::vector<ValueType> target_previous_element_types;
  std::vector<CaseSelector> case_selectors;
  bool default_case{false};
  std::vector<Statement> body;
  std::vector<Statement> alternative;
};

struct Program {
  SourceLanguage language{SourceLanguage::automatic};
  semantic::Profile semantics{};
  std::vector<Statement> statements;
  std::size_t node_count{0};
  std::uint64_t revision{0};
};

struct LoweringResult {
  Program program;
  std::vector<Diagnostic> diagnostics;
};

[[nodiscard]] LoweringResult lower_from_syntax(mpf::detail::Program&& program);
void reindex(Program& program) noexcept;
[[nodiscard]] std::vector<Diagnostic> verify(const Program& program, std::string_view stage);

}  // namespace mpf::detail::hir
