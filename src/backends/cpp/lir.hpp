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
#include "compiler/function_graph.hpp"
#include "ir/ids.hpp"
#include "ir/semantics.hpp"

namespace mpf::detail::cpp::lir {

enum class RuntimeFeature : std::uint8_t {
  dynamic_values,
  arrays,
  character_case,
  reference_arguments,
  optional_arguments,
  scalar_division,
  complex_numbers,
  complex_matrices,
  sparse_matrices,
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
  bool dynamic_truthiness{false};
  bool matlab_truthiness{false};
  bool operand_logical_result{false};
  semantic::Division division{semantic::Division::native};
  semantic::DivisionByZero division_by_zero{semantic::DivisionByZero::target_native};
  bool lexical_block_scopes{false};
  bool resizable_sections{false};
  bool padded_character_selection{false};
  bool module_top_level{true};
  bool entry_function_top_level{false};
};

enum class ParameterPassing : std::uint8_t {
  value,
  const_reference,
  mutable_reference,
  optional_reference
};

struct ParameterAbi {
  ParameterPassing passing{ParameterPassing::value};
  std::string concrete_type;
  std::string template_parameter;
};

struct FunctionAbi {
  bool valid{false};
  bool recursive{false};
  bool forward_declarable{false};
  std::string return_type;
  std::vector<ParameterAbi> parameters;
};

enum class TemporaryRole : std::uint8_t {
  comparison_operand,
  section_argument,
  call_result,
  select_value,
  assignment_value,
  loop_completed,
  range_start,
  range_stop,
  range_step,
  range_first,
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

enum class DeclarationTypeKind : std::uint8_t { concrete, decay_expression };

struct DeclarationPlan {
  std::string name;
  SymbolId symbol_id{};
  DeclarationTypeKind type_kind{DeclarationTypeKind::concrete};
  std::string concrete_type;
  LirNodeId type_probe{};
  std::vector<AssignmentAccess> probe_path;
  std::size_t tuple_index{dynamic_extent};
  bool probe_sequence_list{false};
  std::vector<std::size_t> fixed_shape;
  std::vector<std::string> fixed_nested_types;
};

struct ScopePlan {
  bool valid{false};
  std::vector<DeclarationPlan> declarations;
};

enum class ExpressionForm : std::uint8_t {
  invalid,
  omitted,
  variable,
  target_symbol,
  scalar_literal,
  string_literal,
  null_literal,
  unary_operator,
  unary_truthiness,
  matlab_logical_not,
  binary_operator,
  binary_lazy_and,
  binary_lazy_or,
  matlab_logical_operation,
  matlab_short_circuit_and,
  matlab_short_circuit_or,
  binary_power,
  binary_comparison,
  comparison_chain,
  conditional,
  call,
  runtime_extent,
  index,
  slice,
  member,
  list,
  tuple,
  matlab_array_operation,
  matlab_transpose,
  matlab_sparse_transpose,
  matlab_sparse_index,
  binary_runtime_call,
  binary_reverse_runtime_call
};

enum class ComparisonForm : std::uint8_t {
  infix,
  dynamic_compare,
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
  reshape,
  matlab_sparse_reshape,
  matlab_sparse,
  matlab_full,
  matlab_is_sparse,
  matlab_nonzero_count
};

enum class CallArgumentForm : std::uint8_t { value, forward_optional, copy_section };

enum class EvaluationForm : std::uint8_t {
  direct,
  binary_comparison_reference_lambda_iife,
  comparison_reference_lambda_iife,
  lazy_reference_lambda_thunks,
  copy_call_reference_lambda_iife
};

enum class CallValueForm : std::uint8_t { direct, first_tuple_result };

enum class CallOutcomeForm : std::uint8_t { discard, value };

enum class WritebackForm : std::uint8_t { none, section };

struct CallArgumentPlan {
  CallArgumentForm form{CallArgumentForm::value};
  WritebackForm writeback{WritebackForm::none};
};

enum class IndexForm : std::uint8_t {
  none,
  nested,
  matrix_linear,
  linear_element,
  slice,
  row_slice,
  column,
  block,
  section_nd,
  linear_section
};

enum class VariableAccess : std::uint8_t { direct, optional_value };

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

struct SparseElementwisePlan {
  semantic::SparseElementwiseOperation operation{semantic::SparseElementwiseOperation::none};
  semantic::SparseElementwiseStoragePolicy storage_policy{
      semantic::SparseElementwiseStoragePolicy::none};
  semantic::BroadcastShapeSource shape_source{semantic::BroadcastShapeSource::static_extents};
  ArrayStorageFormat left_storage{ArrayStorageFormat::none};
  ArrayStorageFormat right_storage{ArrayStorageFormat::none};
  ArrayStorageFormat result_storage{ArrayStorageFormat::none};
  std::vector<std::size_t> left_shape;
  std::vector<std::size_t> right_shape;
  std::vector<std::size_t> result_shape;
  std::vector<semantic::BroadcastAxis> axes;

  [[nodiscard]] bool valid() const noexcept {
    return operation != semantic::SparseElementwiseOperation::none;
  }
};

struct SparseLogicalPlan {
  semantic::SparseLogicalOperation operation{semantic::SparseLogicalOperation::none};
  semantic::SparseLogicalStoragePolicy storage_policy{semantic::SparseLogicalStoragePolicy::none};
  semantic::BroadcastShapeSource shape_source{semantic::BroadcastShapeSource::static_extents};
  ArrayStorageFormat left_storage{ArrayStorageFormat::none};
  ArrayStorageFormat right_storage{ArrayStorageFormat::none};
  ArrayStorageFormat result_storage{ArrayStorageFormat::none};
  std::vector<std::size_t> left_shape;
  std::vector<std::size_t> right_shape;
  std::vector<std::size_t> result_shape;
  std::vector<semantic::BroadcastAxis> axes;

  [[nodiscard]] bool valid() const noexcept {
    return operation != semantic::SparseLogicalOperation::none;
  }
};

struct MatrixOperationPlan {
  semantic::MatrixOperation operation{semantic::MatrixOperation::none};
  semantic::MatrixSolveKind solve{semantic::MatrixSolveKind::none};
  semantic::MatrixNumericDomain numeric_domain{semantic::MatrixNumericDomain::none};
  semantic::MatrixConditionPolicy condition_policy{semantic::MatrixConditionPolicy::none};
  semantic::MatrixFactorizationPolicy factorization_policy{
      semantic::MatrixFactorizationPolicy::none};
  semantic::MatrixStructurePolicy structure_policy{semantic::MatrixStructurePolicy::none};
  semantic::MatrixStoragePolicy storage_policy{semantic::MatrixStoragePolicy::none};
  ArrayStorageFormat left_storage{ArrayStorageFormat::none};
  ArrayStorageFormat right_storage{ArrayStorageFormat::none};
  ArrayStorageFormat result_storage{ArrayStorageFormat::none};
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
  semantic::ReductionStoragePolicy storage_policy{semantic::ReductionStoragePolicy::none};
  ArrayStorageFormat input_storage{ArrayStorageFormat::none};
  ArrayStorageFormat result_storage{ArrayStorageFormat::none};

  [[nodiscard]] bool valid() const noexcept {
    return operation != semantic::ReductionOperation::none;
  }
};

struct SparseConstructionPlan {
  semantic::SparseConstructionKind kind{semantic::SparseConstructionKind::none};
  std::vector<std::size_t> result_shape;
  std::vector<std::size_t> triplet_element_counts;
  std::size_t reserve_hint{0U};
  semantic::SparseValueDomain value_domain{semantic::SparseValueDomain::none};
  semantic::SparseDuplicatePolicy duplicate_policy{semantic::SparseDuplicatePolicy::none};

  [[nodiscard]] bool valid() const noexcept {
    return kind != semantic::SparseConstructionKind::none;
  }
};

struct SparseIndexPlan {
  semantic::SparseIndexKind kind{semantic::SparseIndexKind::none};
  ArrayStorageFormat source_storage{ArrayStorageFormat::none};
  ArrayStorageFormat result_storage{ArrayStorageFormat::none};
  std::vector<std::size_t> input_shape;
  std::vector<std::size_t> result_shape;

  [[nodiscard]] bool valid() const noexcept { return kind != semantic::SparseIndexKind::none; }
};

struct SparseReshapePlan {
  semantic::SparseReshapeKind kind{semantic::SparseReshapeKind::none};
  semantic::SparseReshapeDimensionForm dimension_form{semantic::SparseReshapeDimensionForm::none};
  semantic::SparseReshapeInference inference{semantic::SparseReshapeInference::none};
  std::size_t inferred_axis{0U};
  ArrayStorageFormat source_storage{ArrayStorageFormat::none};
  ArrayStorageFormat result_storage{ArrayStorageFormat::none};
  std::vector<std::size_t> input_shape;
  std::vector<std::size_t> requested_shape;
  std::vector<std::size_t> result_shape;

  [[nodiscard]] bool valid() const noexcept { return kind != semantic::SparseReshapeKind::none; }
};

struct SparseMutationPlan {
  semantic::SparseMutationKind kind{semantic::SparseMutationKind::none};
  semantic::SparseReplacementKind replacement{semantic::SparseReplacementKind::none};
  semantic::SparseDuplicateWritePolicy duplicate_policy{semantic::SparseDuplicateWritePolicy::none};
  semantic::SparseZeroWritePolicy zero_policy{semantic::SparseZeroWritePolicy::none};
  ArrayStorageFormat source_storage{ArrayStorageFormat::none};
  ArrayStorageFormat replacement_storage{ArrayStorageFormat::none};
  ArrayStorageFormat result_storage{ArrayStorageFormat::none};
  std::vector<std::size_t> input_shape;
  std::vector<std::size_t> selection_shape;
  std::vector<std::size_t> replacement_shape;
  std::vector<std::size_t> result_shape;

  [[nodiscard]] bool valid() const noexcept { return kind != semantic::SparseMutationKind::none; }
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
  SparseElementwisePlan sparse_elementwise;
  SparseLogicalPlan sparse_logical;
  std::vector<std::vector<std::size_t>> runtime_shape_arguments;
  std::vector<std::int64_t> runtime_integer_arguments;
  ReductionPlan reduction;
  SparseIndexPlan sparse_index;
  SparseReshapePlan sparse_reshape;
  CallForm call{CallForm::none};
  EvaluationForm evaluation{EvaluationForm::direct};
  CallValueForm call_value{CallValueForm::direct};
  CallOutcomeForm call_outcome{CallOutcomeForm::discard};
  std::vector<CallArgumentPlan> call_arguments;
  IndexForm index{IndexForm::none};
  std::vector<semantic::IndexSelectorKind> index_selectors;
  std::vector<semantic::IndexExtentSource> index_extents;
  VariableAccess variable_access{VariableAccess::direct};
  std::size_t index_base{0};
  bool allow_negative_index{false};
  bool column_major{false};
  bool inclusive_slice_stop{false};
  bool flatten_base{false};
  bool string_value{false};
  std::string concrete_type;
  std::vector<bool> widen_children;
  std::vector<bool> complex_children;
  std::vector<std::size_t> input_shape;
  std::vector<std::size_t> result_shape;
  ArrayLiteralPlan array_literal;
};

enum class ConditionForm : std::uint8_t { direct, runtime_truthy, matlab_all_nonzero };

enum class StatementForm : std::uint8_t {
  discard,
  declaration_initializer,
  assignment,
  multi_pattern,
  multi_tuple,
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
  std::string concrete_type;
  bool widen_elements{false};
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
  bool flatten_replacement{false};
  bool character_selector{false};
  semantic::IndexedMutationContract indexed_mutation;
  std::vector<std::size_t> mutation_input_shape;
  std::vector<std::size_t> mutation_result_shape;
  SparseMutationPlan sparse_mutation;
  std::vector<std::string> targets;
  std::vector<VariableAccess> target_accesses;
  std::vector<AssignmentLeafPlan> assignment_leaves;
  std::vector<SelectorForm> selectors;
  std::vector<std::string> return_names;
};

enum class RuntimeFragment : std::uint8_t {
  core,
  dynamic_values,
  scalar_division,
  complex_numbers,
  complex_matrices,
  sparse_matrices
};
enum class EntryErrorPolicy : std::uint8_t { none, report_standard_exception };

struct TranslationUnitPlan {
  bool valid{false};
  bool emit_banner{false};
  std::string banner;
  std::vector<std::string> standard_headers;
  std::vector<RuntimeFragment> runtime_fragments;
  std::string runtime_namespace;
  std::string generated_namespace;
  std::vector<std::size_t> forward_declarations;
  std::vector<std::size_t> definitions;
  std::vector<std::size_t> entry_statements;
  bool emit_module_scope{false};
  bool entry_owns_program_scope{false};
  bool emit_entry_function{false};
  bool emit_main{false};
  EntryErrorPolicy entry_error_policy{EntryErrorPolicy::none};
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
  NumericType numeric_type{unknown_numeric_type};
  BindingKind binding{BindingKind::unresolved};
  IntrinsicId intrinsic{IntrinsicId::none};
  CodeBinding target_binding{};
  ValueType element_type{ValueType::unknown};
  NumericType element_numeric_type{unknown_numeric_type};
  ArrayStorageFormat array_storage{ArrayStorageFormat::none};
  std::vector<std::size_t> shape;
  semantic::LogicalEvaluation logical_evaluation{semantic::LogicalEvaluation::none};
  semantic::ArrayOperation array_operation{semantic::ArrayOperation::native};
  BroadcastPlan broadcast;
  SparseElementwisePlan sparse_elementwise;
  SparseLogicalPlan sparse_logical;
  MatrixOperationPlan matrix_operation;
  ReductionPlan reduction;
  SparseConstructionPlan sparse_construction;
  SparseIndexPlan sparse_index;
  SparseReshapePlan sparse_reshape;
  std::vector<ValueType> tuple_types;
  std::vector<NumericType> tuple_numeric_types;
  std::vector<ValueType> tuple_element_types;
  std::vector<NumericType> tuple_element_numeric_types;
  std::vector<ArrayStorageFormat> tuple_array_storage;
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
  NumericType declared_numeric_type{unknown_numeric_type};
  ValueType element_type{ValueType::unknown};
  NumericType element_numeric_type{unknown_numeric_type};
  ArrayStorageFormat array_storage{ArrayStorageFormat::none};
  ValueType previous_type{ValueType::unknown};
  NumericType previous_numeric_type{unknown_numeric_type};
  ValueType previous_element_type{ValueType::unknown};
  NumericType previous_element_numeric_type{unknown_numeric_type};
  ArrayStorageFormat previous_array_storage{ArrayStorageFormat::none};
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
  std::vector<NumericType> parameter_numeric_types;
  std::vector<ValueType> parameter_element_types;
  std::vector<NumericType> parameter_element_numeric_types;
  std::vector<ArrayStorageFormat> parameter_array_storage;
  std::vector<std::vector<std::size_t>> parameter_shapes;
  std::vector<std::string> return_names;
  std::vector<SymbolId> return_symbols;
  bool has_value_return{false};
  std::vector<ValueType> return_types;
  std::vector<NumericType> return_numeric_types;
  std::vector<ValueType> return_element_types;
  std::vector<NumericType> return_element_numeric_types;
  std::vector<ArrayStorageFormat> return_array_storage;
  std::vector<std::vector<std::size_t>> return_shapes;
  bool return_sequence_is_list{false};
  std::vector<ValueMetadata> return_sequence_elements;
  std::vector<std::string> target_names;
  std::vector<SymbolId> target_symbols;
  AssignmentPattern target_pattern;
  bool has_target_pattern{false};
  std::vector<ValueType> target_types;
  std::vector<NumericType> target_numeric_types;
  std::vector<ValueType> target_element_types;
  std::vector<NumericType> target_element_numeric_types;
  std::vector<ArrayStorageFormat> target_array_storage;
  std::vector<std::vector<std::size_t>> target_shapes;
  std::vector<ValueType> target_previous_types;
  std::vector<NumericType> target_previous_numeric_types;
  std::vector<ValueType> target_previous_element_types;
  std::vector<NumericType> target_previous_element_numeric_types;
  std::vector<ArrayStorageFormat> target_previous_array_storage;
  semantic::IndexedMutationContract indexed_mutation;
  std::vector<std::size_t> mutation_input_shape;
  std::vector<std::size_t> mutation_result_shape;
  SparseMutationPlan sparse_mutation;
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
  TranslationUnitPlan translation_unit;
  std::vector<std::string_view> dependencies;
  std::vector<Statement> statements;
  FunctionDependencyGraph function_graph;
  std::size_t node_count{0};
  std::uint64_t revision{0};
};

struct Program final : BackendArtifact {
  [[nodiscard]] TargetLanguage target() const noexcept override { return TargetLanguage::cpp; }
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

}  // namespace mpf::detail::cpp::lir
