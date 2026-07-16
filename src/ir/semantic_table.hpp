#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "hir.hpp"

namespace mpf::detail::hir {

enum class SemanticNodeKind : std::uint8_t { absent, expression, statement };

struct ExpressionFacts {
  HirNodeId origin{};
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
  std::vector<bool> argument_optional_forward;
  bool procedure_has_result{false};
  std::size_t index_base{0};
  bool allow_negative_index{false};
  bool column_major{false};
  bool slice_stop_inclusive{false};
};

struct StatementFacts {
  HirNodeId origin{};
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
  std::vector<ParameterIntent> parameter_intents;
  std::vector<bool> parameter_optional;
  std::vector<ValueType> parameter_types;
  std::vector<ValueType> parameter_element_types;
  std::vector<std::vector<std::size_t>> parameter_shapes;
  bool has_value_return{false};
  std::vector<ValueType> return_types;
  std::vector<ValueType> return_element_types;
  std::vector<std::vector<std::size_t>> return_shapes;
  bool return_sequence_is_list{false};
  std::vector<ValueMetadata> return_sequence_elements;
  std::vector<ValueType> target_types;
  std::vector<ValueType> target_element_types;
  std::vector<std::vector<std::size_t>> target_shapes;
  std::vector<ValueType> target_previous_types;
  std::vector<ValueType> target_previous_element_types;
};

struct SemanticNodeSlot {
  SemanticNodeKind kind{SemanticNodeKind::absent};
  std::uint32_t offset{0};
};

// Node IDs index this table in O(1). The compact fact arrays avoid paying the much larger
// StatementFacts footprint for expression nodes (and vice versa).
struct SemanticTable {
  std::uint64_t hir_revision{0};
  std::size_t hir_node_count{0};
  std::vector<SemanticNodeSlot> nodes;
  std::vector<ExpressionFacts> expressions;
  std::vector<StatementFacts> statements;

  [[nodiscard]] const ExpressionFacts* expression(HirNodeId id) const noexcept;
  [[nodiscard]] ExpressionFacts* expression(HirNodeId id) noexcept;
  [[nodiscard]] const StatementFacts* statement(HirNodeId id) const noexcept;
  [[nodiscard]] StatementFacts* statement(HirNodeId id) noexcept;
};

// Consumes all Analyzer-owned annotations from HIR. Surface syntax and normalized structure stay
// in HIR; semantic facts have exactly one owner after this call.
[[nodiscard]] SemanticTable extract_semantics(Program& program);
[[nodiscard]] std::vector<Diagnostic> verify_semantics(const Program& program,
                                                       const SemanticTable& table,
                                                       std::string_view stage);

}  // namespace mpf::detail::hir
