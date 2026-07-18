#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "backends/common/artifact.hpp"
#include "backends/common/identifier_mangler.hpp"
#include "compiler/assignment_pattern.hpp"
#include "compiler/binding.hpp"
#include "compiler/call_contract.hpp"
#include "ir/ids.hpp"
#include "ir/semantics.hpp"

namespace mpf::detail::javascript::lir {

enum class RuntimeFeature : std::uint8_t {
  dynamic_values,
  arrays,
  character_case,
  reference_arguments,
  scalar_division,
  count
};

struct RuntimeRequirements {
  std::uint32_t bits{0};

  void require(const RuntimeFeature feature) noexcept {
    bits |= 1U << static_cast<std::uint32_t>(feature);
  }
  [[nodiscard]] bool contains(const RuntimeFeature feature) const noexcept {
    return (bits & (1U << static_cast<std::uint32_t>(feature))) != 0;
  }
};

struct EmissionPlan {
  enum class ModuleFormat : std::uint8_t { strict_script, esm };

  bool dynamic_truthiness{false};
  bool matlab_truthiness{false};
  bool operand_logical_result{false};
  semantic::Division division{semantic::Division::native};
  semantic::DivisionByZero division_by_zero{semantic::DivisionByZero::target_native};
  bool explicit_exports_only{false};
  bool lexical_block_scopes{false};
  bool structural_equality{false};
  bool resizable_sections{false};
  bool emit_parameter_defaults{false};
  bool padded_character_selection{false};
  ModuleFormat module{ModuleFormat::strict_script};
};

enum class ParameterPassing : std::uint8_t { value, reference_box };

struct FunctionAbi {
  bool valid{false};
  bool exported{false};
  std::vector<ParameterPassing> parameters;
};

enum class TemporaryRole : std::uint8_t {
  comparison_operand,
  reference_argument,
  call_result,
  select_value,
  assignment_value,
  loop_completed,
  range_start,
  range_stop,
  range_step,
  range_cursor
};

struct TemporarySlot {
  TemporaryRole role{TemporaryRole::comparison_operand};
  std::uint32_t ordinal{0};
  std::string name;
};

struct TemporaryPlan {
  std::vector<std::uint32_t> offsets;
  std::vector<TemporarySlot> slots;

  [[nodiscard]] const std::string* find(const LirNodeId node, const TemporaryRole role,
                                        const std::size_t ordinal = 0) const noexcept {
    if (!node.valid() || node.value() + 1U >= offsets.size()) return nullptr;
    for (std::size_t index = offsets[node.value()]; index < offsets[node.value() + 1U]; ++index) {
      const auto& slot = slots[index];
      if (slot.role == role && slot.ordinal == ordinal) return &slot.name;
    }
    return nullptr;
  }
};

struct ScopePlan {
  bool valid{false};
  std::vector<IdentifierReference> declarations;
};

enum class ExpressionForm : std::uint8_t {
  invalid,
  omitted,
  variable,
  target_symbol,
  literal,
  unary_operator,
  unary_truthiness,
  matlab_logical_not,
  binary_operator,
  binary_lazy_and,
  binary_lazy_or,
  matlab_logical_operation,
  matlab_short_circuit_and,
  matlab_short_circuit_or,
  binary_comparison,
  comparison_chain,
  conditional,
  call,
  runtime_extent,
  index,
  slice,
  member,
  array,
  tuple,
  binary_reverse_divide,
  matlab_array_operation,
  matlab_transpose,
  binary_runtime_call
};

enum class ComparisonForm : std::uint8_t {
  infix,
  structural_equal,
  structural_not_equal,
  identity,
  not_identity,
  membership,
  not_membership
};

enum class CallForm : std::uint8_t {
  none,
  direct,
  python_float,
  python_length,
  matlab_length,
  element_count,
  matlab_all,
  matlab_any,
  sum,
  present,
  reshape
};

enum class CallArgumentForm : std::uint8_t {
  value,
  forward_optional,
  reference_box,
  reference_box_uninitialized
};

enum class EvaluationForm : std::uint8_t {
  direct,
  comparison_arrow_iife,
  lazy_arrow_thunks,
  writable_call_arrow_iife
};

enum class CallValueForm : std::uint8_t { direct, first_result };

enum class WritebackForm : std::uint8_t { none, direct, element, section };

struct CallArgumentPlan {
  CallArgumentForm form{CallArgumentForm::value};
  WritebackForm writeback{WritebackForm::none};
};

enum class IndexForm : std::uint8_t { none, element, section };

enum class VariableAccess : std::uint8_t { direct, reference_box_value };

struct ComparisonPlan {
  ComparisonForm form{ComparisonForm::infix};
  std::string token;
};

struct BroadcastPlan {
  bool valid{false};
  semantic::BroadcastShapeSource shape_source{semantic::BroadcastShapeSource::static_extents};
  std::vector<std::size_t> left_shape;
  std::vector<std::size_t> right_shape;
  std::vector<std::size_t> result_shape;
  std::vector<semantic::BroadcastAxis> axes;
};

struct MatrixOperationPlan {
  semantic::MatrixOperation operation{semantic::MatrixOperation::none};
  semantic::MatrixSolveKind solve{semantic::MatrixSolveKind::none};
  semantic::MatrixConditionPolicy condition_policy{semantic::MatrixConditionPolicy::none};
  semantic::MatrixStructurePolicy structure_policy{semantic::MatrixStructurePolicy::none};
  std::vector<std::size_t> left_shape;
  std::vector<std::size_t> right_shape;
  std::vector<std::size_t> result_shape;

  [[nodiscard]] bool valid() const noexcept { return operation != semantic::MatrixOperation::none; }
};

struct ReductionPlan {
  semantic::ReductionOperation operation{semantic::ReductionOperation::none};
  semantic::ReductionAxisPolicy axis_policy{semantic::ReductionAxisPolicy::none};
  semantic::ReductionShapeSource shape_source{semantic::ReductionShapeSource::static_extents};
  std::vector<std::size_t> input_shape;
  std::vector<std::size_t> result_shape;
  std::vector<std::size_t> output_shape;
  std::vector<std::size_t> axes;
  bool scalar_result{false};

  [[nodiscard]] bool valid() const noexcept {
    return operation != semantic::ReductionOperation::none;
  }
};

enum class ArrayLiteralForm : std::uint8_t { none, direct, shaped_empty };

struct ArrayLiteralPlan {
  ArrayLiteralForm form{ArrayLiteralForm::none};
  std::vector<std::size_t> shape;
};

struct ExpressionPlan {
  bool valid{false};
  ExpressionForm form{ExpressionForm::invalid};
  int precedence{10};
  std::string token;
  std::vector<ComparisonPlan> comparisons;
  BroadcastPlan broadcast;
  ReductionPlan reduction;
  CallForm call{CallForm::none};
  EvaluationForm evaluation{EvaluationForm::direct};
  CallValueForm call_value{CallValueForm::direct};
  std::vector<CallArgumentPlan> call_arguments;
  IndexForm index{IndexForm::none};
  std::vector<semantic::IndexSelectorKind> index_selectors;
  std::vector<semantic::IndexExtentSource> index_extents;
  VariableAccess variable_access{VariableAccess::direct};
  std::size_t index_base{0};
  bool allow_negative_index{false};
  bool column_major{false};
  bool inclusive_slice_stop{false};
  bool string_value{false};
  std::vector<std::size_t> result_shape;
  ArrayLiteralPlan array_literal;
};

enum class ConditionForm : std::uint8_t { direct, runtime_truthy, matlab_all_nonzero };

enum class StatementForm : std::uint8_t {
  discard,
  declaration_initializer,
  declaration_array,
  assignment,
  multi_pattern,
  multi_destructure,
  indexed_element_assignment,
  indexed_section_assignment,
  print_empty,
  print_value,
  print_tuple,
  return_void,
  return_value,
  break_loop,
  continue_loop,
  expression,
  conditional,
  selection,
  case_clause,
  while_loop,
  range_loop,
  for_loop,
  function
};

enum class SelectorForm : std::uint8_t { value, closed_range, lower_bound, upper_bound };

struct AssignmentLeafPlan {
  std::string name;
  VariableAccess access{VariableAccess::direct};
  bool captured_sequence{false};
  std::vector<AssignmentAccess> access_path;
  std::vector<std::vector<AssignmentAccess>> captured_paths;
};

struct StatementPlan {
  bool valid{false};
  StatementForm form{StatementForm::discard};
  ConditionForm condition{ConditionForm::direct};
  VariableAccess target_access{VariableAccess::direct};
  bool has_alternative{false};
  bool range_has_step{false};
  bool retain_loop_value{false};
  bool inclusive_stop{false};
  bool resizable_section{false};
  bool character_selector{false};
  semantic::IndexedMutationContract indexed_mutation;
  std::vector<std::size_t> mutation_input_shape;
  std::vector<std::size_t> mutation_result_shape;
  std::string array_default;
  std::vector<std::size_t> array_shape;
  std::vector<std::string> targets;
  std::vector<VariableAccess> target_accesses;
  std::vector<AssignmentLeafPlan> assignment_leaves;
  std::vector<SelectorForm> selectors;
  std::vector<bool> parameter_defaults;
  std::vector<std::string> return_names;
};

enum class RuntimeFragment : std::uint8_t {
  dynamic_values,
  character_case,
  arrays,
  scalar_division
};

struct ModulePlan {
  bool valid{false};
  bool emit_banner{false};
  std::string banner;
  std::vector<std::string> directives;
  std::vector<RuntimeFragment> runtime_fragments;
  std::vector<std::size_t> body_order;
};

struct Expression {
  LirNodeId id{};
  HirNodeId origin{};
  SourceLocation location{};
  ExpressionKind kind{ExpressionKind::invalid};
  std::string value;
  UnaryOperator unary_operation{UnaryOperator::none};
  SymbolId symbol_id{};
  BinaryOperator operation{BinaryOperator::none};
  ComparisonOperator comparison{ComparisonOperator::none};
  std::vector<ComparisonOperator> comparisons;
  std::vector<Expression> children;
  ValueType inferred_type{ValueType::unknown};
  BindingKind binding{BindingKind::unresolved};
  IntrinsicId intrinsic{IntrinsicId::none};
  CodeBinding target_binding{};
  ValueType element_type{ValueType::unknown};
  std::vector<std::size_t> shape;
  semantic::LogicalEvaluation logical_evaluation{semantic::LogicalEvaluation::none};
  semantic::ArrayOperation array_operation{semantic::ArrayOperation::native};
  BroadcastPlan broadcast;
  MatrixOperationPlan matrix_operation;
  ReductionPlan reduction;
  std::vector<ValueType> tuple_types;
  std::vector<ValueType> tuple_element_types;
  std::vector<std::vector<std::size_t>> tuple_shapes;
  bool sequence_is_list{false};
  std::vector<ValueMetadata> sequence_elements;
  std::size_t requested_outputs{1};
  bool multi_output_call{false};
  std::vector<ArgumentTransfer> argument_transfers;
  std::vector<std::string> argument_names;
  bool procedure_has_result{false};
  std::size_t index_base{0};
  bool allow_negative_index{false};
  bool column_major{false};
  bool slice_stop_inclusive{false};
  semantic::IndexExtentSource index_extent{semantic::IndexExtentSource::none};
  std::vector<semantic::IndexSelectorKind> index_selectors;
  std::vector<semantic::IndexExtentSource> index_extents;
  ExpressionPlan plan;

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
  LirNodeId id{};
  HirNodeId origin{};
  StatementKind kind{StatementKind::expression};
  std::size_t line{1};
  std::string name;
  SymbolId symbol_id{};
  Expression expression;
  bool has_expression{false};
  bool procedure_call{false};
  Expression secondary_expression;
  bool has_secondary_expression{false};
  Expression tertiary_expression;
  bool has_tertiary_expression{false};
  bool inclusive_stop{false};
  bool retain_last_loop_value{true};
  bool source_exported{false};
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
  std::vector<SymbolId> parameter_symbols;
  std::vector<ParameterKind> parameter_kinds;
  std::vector<Expression> parameter_defaults;
  std::vector<ParameterIntent> parameter_intents;
  std::vector<bool> parameter_optional;
  std::vector<ValueType> parameter_types;
  std::vector<ValueType> parameter_element_types;
  std::vector<std::vector<std::size_t>> parameter_shapes;
  std::vector<std::string> return_names;
  std::vector<SymbolId> return_symbols;
  bool has_value_return{false};
  std::vector<ValueType> return_types;
  std::vector<ValueType> return_element_types;
  std::vector<std::vector<std::size_t>> return_shapes;
  bool return_sequence_is_list{false};
  std::vector<ValueMetadata> return_sequence_elements;
  std::vector<std::string> target_names;
  std::vector<SymbolId> target_symbols;
  AssignmentPattern target_pattern;
  bool has_target_pattern{false};
  std::vector<ValueType> target_types;
  std::vector<ValueType> target_element_types;
  std::vector<std::vector<std::size_t>> target_shapes;
  std::vector<ValueType> target_previous_types;
  std::vector<ValueType> target_previous_element_types;
  semantic::IndexedMutationContract indexed_mutation;
  std::vector<std::size_t> mutation_input_shape;
  std::vector<std::size_t> mutation_result_shape;
  std::vector<CaseSelector> case_selectors;
  bool default_case{false};
  FunctionAbi function_abi;
  ScopePlan function_scope;
  ScopePlan statement_scope;
  ScopePlan body_scope;
  ScopePlan alternative_scope;
  StatementPlan plan;
  std::vector<Statement> body;
  std::vector<Statement> alternative;
};

struct SemanticProgram {
  SourceLanguage source_language{SourceLanguage::automatic};
  EmissionPlan emission;
  RuntimeRequirements runtime;
  IdentifierPlan identifiers;
  TemporaryPlan temporaries;
  SourceSegmentPlan source_segments;
  ScopePlan program_scope;
  ModulePlan module;
  std::vector<std::string_view> dependencies;
  std::vector<Statement> statements;
  std::size_t node_count{0};
  std::uint64_t revision{0};
};

struct Program final : BackendArtifact {
  [[nodiscard]] TargetLanguage target() const noexcept override {
    return TargetLanguage::javascript;
  }
  [[nodiscard]] std::size_t node_count_hint() const noexcept override { return node_count; }
  [[nodiscard]] const std::vector<SerializedChunk>& serialized_chunks() const noexcept override {
    return chunks;
  }
  [[nodiscard]] const std::vector<std::string_view>& dependencies() const noexcept override {
    return dependency_names;
  }
  [[nodiscard]] std::string debug_dump() const override { return semantic_dump; }

  std::vector<std::string_view> dependency_names;
  std::vector<SerializedChunk> chunks;
  std::string semantic_dump;
  std::size_t node_count{0};
  std::uint64_t revision{0};
};

[[nodiscard]] std::string dump(const SemanticProgram& program);
[[nodiscard]] std::string dump(const Program& program);

}  // namespace mpf::detail::javascript::lir
