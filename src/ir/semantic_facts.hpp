#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "compiler/assignment_pattern.hpp"
#include "ids.hpp"
#include "semantics.hpp"
#include "storage_region.hpp"

namespace mpf::detail::hir {

enum class SemanticNodeKind : std::uint8_t { absent, expression, statement };

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

struct ExpressionFacts {
  HirNodeId origin{};
  ValueType inferred_type{ValueType::unknown};
  NumericType numeric_type{unknown_numeric_type};
  BindingKind binding{BindingKind::unresolved};
  IntrinsicId intrinsic{IntrinsicId::none};
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
  std::vector<ParameterIntent> argument_intents;
  std::vector<std::string> argument_names;
  std::vector<bool> argument_optional_forward;
  bool procedure_has_result{false};
  std::size_t index_base{0};
  bool allow_negative_index{false};
  bool column_major{false};
  bool slice_stop_inclusive{false};
  semantic::IndexExtentSource index_extent{semantic::IndexExtentSource::none};
  std::vector<semantic::IndexSelectorKind> index_selectors;
  std::vector<semantic::IndexExtentSource> index_extents;
  StorageRegion storage_region;
};

struct StatementFacts {
  HirNodeId origin{};
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
  bool exported{false};
  std::vector<std::size_t> shape;
  std::size_t index_base{0};
  bool allow_negative_index{false};
  std::vector<ParameterIntent> parameter_intents;
  std::vector<bool> parameter_optional;
  std::vector<ValueType> parameter_types;
  std::vector<NumericType> parameter_numeric_types;
  std::vector<ValueType> parameter_element_types;
  std::vector<NumericType> parameter_element_numeric_types;
  std::vector<ArrayStorageFormat> parameter_array_storage;
  std::vector<std::vector<std::size_t>> parameter_shapes;
  bool has_value_return{false};
  std::vector<ValueType> return_types;
  std::vector<NumericType> return_numeric_types;
  std::vector<ValueType> return_element_types;
  std::vector<NumericType> return_element_numeric_types;
  std::vector<ArrayStorageFormat> return_array_storage;
  std::vector<std::vector<std::size_t>> return_shapes;
  bool return_sequence_is_list{false};
  std::vector<ValueMetadata> return_sequence_elements;
  AssignmentPattern target_pattern;
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
};

struct SemanticNodeSlot {
  SemanticNodeKind kind{SemanticNodeKind::absent};
  std::uint32_t offset{0};
};

// Revision-bound dense semantic side table. Frontends seed it while creating HIR identities;
// Analyzer is the sole mutable consumer after the AST-to-HIR ownership transfer.
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

}  // namespace mpf::detail::hir
