#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

#include "compiler/array_storage.hpp"
#include "mpf/transpiler.hpp"

namespace mpf::detail::semantic {

enum class Truthiness { native, dynamic, matlab_all_nonzero, matlab_scalar };
enum class LogicalResult { boolean, operand };
enum class LogicalEvaluation : std::uint8_t {
  none,
  eager_elementwise,
  short_circuit_boolean,
  short_circuit_operand
};

// Logical reductions are source-language operations rather than aliases for the target
// language's container helpers.  The axis policy remains explicit through target lowering so
// neither renderer has to rediscover Matlab's first-nonsingleton or all-dimensions rules.
enum class ReductionOperation : std::uint8_t { none, logical_all, logical_any };
enum class ReductionAxisPolicy : std::uint8_t {
  none,
  first_nonsingleton,
  explicit_dimensions,
  all_dimensions
};
enum class ReductionShapeSource : std::uint8_t { static_extents, runtime_operand };

template <typename Extents, typename Axes>
[[nodiscard]] bool valid_logical_reduction_contract(const ReductionOperation operation,
                                                    const ReductionAxisPolicy axis_policy,
                                                    const ReductionShapeSource shape_source,
                                                    const Extents& input, const Extents& result,
                                                    const Extents& output, const Axes& axes,
                                                    const bool scalar_result) noexcept {
  if (operation == ReductionOperation::none || axis_policy == ReductionAxisPolicy::none) {
    return false;
  }
  if (shape_source == ReductionShapeSource::runtime_operand) {
    return axis_policy == ReductionAxisPolicy::all_dimensions && input.empty() && result.empty() &&
           output.empty() && axes.empty() && scalar_result;
  }
  if (input.size() != result.size()) return false;
  if (input.empty()) {
    return axes.empty() && result.empty() && output.empty() && scalar_result;
  }
  std::size_t previous = 0U;
  bool has_previous = false;
  for (const auto axis : axes) {
    if (axis >= input.size() || (has_previous && axis <= previous)) return false;
    previous = axis;
    has_previous = true;
  }
  if (axis_policy == ReductionAxisPolicy::all_dimensions) {
    if (axes.size() != input.size()) return false;
    for (std::size_t axis = 0; axis < axes.size(); ++axis) {
      if (axes[axis] != axis) return false;
    }
  } else if (axis_policy == ReductionAxisPolicy::first_nonsingleton) {
    const bool empty_matrix = input.size() == 2U && input[0] == 0U && input[1] == 0U;
    if ((!empty_matrix && axes.size() != 1U) || (empty_matrix && axes.size() != 2U)) return false;
  }
  for (std::size_t axis = 0; axis < input.size(); ++axis) {
    bool reduced = false;
    for (const auto candidate : axes) reduced = reduced || candidate == axis;
    if (result[axis] != (reduced ? 1U : input[axis])) return false;
  }
  bool expected_scalar = true;
  for (const auto extent : result) expected_scalar = expected_scalar && extent == 1U;
  if (scalar_result != expected_scalar || (scalar_result && !output.empty())) return false;
  if (!scalar_result && output.empty()) return false;
  std::size_t result_size = 1U;
  for (const auto extent : result) {
    if (extent != 0U && result_size > std::numeric_limits<std::size_t>::max() / extent) {
      return false;
    }
    result_size *= extent;
  }
  std::size_t output_size = 1U;
  for (const auto extent : output) {
    if (extent != 0U && output_size > std::numeric_limits<std::size_t>::max() / extent) {
      return false;
    }
    output_size *= extent;
  }
  return scalar_result || result_size == output_size;
}

[[nodiscard]] constexpr bool short_circuits(const LogicalEvaluation evaluation) noexcept {
  return evaluation == LogicalEvaluation::short_circuit_boolean ||
         evaluation == LogicalEvaluation::short_circuit_operand;
}
enum class Equality { native, structural };
enum class Division { native, real_quotient };
// Division result representation and zero-denominator behavior are independent source-language
// contracts.  Keeping the latter explicit prevents a target backend from inferring Python
// exceptions or IEEE-754 results from an operator token.
enum class DivisionByZero : std::uint8_t { target_native, ieee754, exception };

[[nodiscard]] constexpr bool valid_division_contract(
    const Division division, const DivisionByZero division_by_zero) noexcept {
  return (division == Division::native) == (division_by_zero == DivisionByZero::target_native);
}
enum class IndexLayout { row_major, column_major };
enum class TopLevelStorage { module, entry_function };
enum class ExportPolicy { all_top_level, explicit_only };
enum class ScopeModel { function, lexical_blocks };

// Per-axis lowering contract for Matlab compatible-size array operations. Matlab aligns
// dimensions from the first axis and treats missing trailing dimensions as singleton axes.
enum class BroadcastAxis : std::uint8_t { match, expand_left, expand_right, runtime };
// Whether a compatible-size operation can use analyzer-owned extents directly or must derive
// operand shapes after evaluating the two runtime values. The latter also represents unknown rank.
enum class BroadcastShapeSource : std::uint8_t { static_extents, runtime_operands };
enum class ArrayOperation : std::uint8_t { native, matlab };

// Sparse element-wise arithmetic is not a matrix operation. Keep its source operation and
// storage propagation independent so target lowering never aliases Matlab `.*` to `*` or infers
// sparse preservation from a helper name. The current finite-real CSC contract deliberately
// requires analyzer-owned extents; dynamic rank, empty dimensions, and complex sparse values fail
// closed until their own typed contracts are implemented.
enum class SparseElementwiseOperation : std::uint8_t { none, multiply };
enum class SparseElementwiseStoragePolicy : std::uint8_t { none, preserve_sparse };

[[nodiscard]] constexpr ArrayStorageFormat sparse_elementwise_result_storage(
    const ArrayStorageFormat left, const ArrayStorageFormat right) noexcept {
  const bool left_sparse = left == ArrayStorageFormat::sparse_csc;
  const bool right_sparse = right == ArrayStorageFormat::sparse_csc;
  const bool left_supported =
      left == ArrayStorageFormat::none || left == ArrayStorageFormat::dense || left_sparse;
  const bool right_supported =
      right == ArrayStorageFormat::none || right == ArrayStorageFormat::dense || right_sparse;
  return left_supported && right_supported && (left_sparse || right_sparse)
             ? ArrayStorageFormat::sparse_csc
             : ArrayStorageFormat::none;
}

template <typename Shape, typename Axes>
[[nodiscard]] bool valid_sparse_elementwise_contract(
    const SparseElementwiseOperation operation, const SparseElementwiseStoragePolicy policy,
    const BroadcastShapeSource shape_source, const ArrayStorageFormat left_storage,
    const ArrayStorageFormat right_storage, const ArrayStorageFormat result_storage,
    const Shape& left_shape, const Shape& right_shape, const Shape& result_shape,
    const Axes& axes) noexcept {
  if (operation != SparseElementwiseOperation::multiply ||
      policy != SparseElementwiseStoragePolicy::preserve_sparse ||
      shape_source != BroadcastShapeSource::static_extents ||
      result_storage != sparse_elementwise_result_storage(left_storage, right_storage) ||
      result_storage != ArrayStorageFormat::sparse_csc || result_shape.size() != 2U ||
      axes.size() != result_shape.size()) {
    return false;
  }
  const auto valid_operand = [](const ArrayStorageFormat storage, const Shape& shape) {
    if (storage == ArrayStorageFormat::none) return shape.empty();
    if (storage != ArrayStorageFormat::dense && storage != ArrayStorageFormat::sparse_csc) {
      return false;
    }
    return shape.size() == 2U && shape[0] != 0U && shape[1] != 0U;
  };
  if (!valid_operand(left_storage, left_shape) ||
      !valid_operand(right_storage, right_shape) || result_shape[0] == 0U ||
      result_shape[1] == 0U) {
    return false;
  }
  for (std::size_t axis = 0U; axis < result_shape.size(); ++axis) {
    const auto left_extent = axis < left_shape.size() ? left_shape[axis] : 1U;
    const auto right_extent = axis < right_shape.size() ? right_shape[axis] : 1U;
    const auto expected_axis =
        left_extent == right_extent
            ? BroadcastAxis::match
            : left_extent == 1U ? BroadcastAxis::expand_left
                                : right_extent == 1U ? BroadcastAxis::expand_right
                                                    : BroadcastAxis::runtime;
    const auto expected_extent = left_extent > right_extent ? left_extent : right_extent;
    if (expected_axis == BroadcastAxis::runtime || axes[axis] != expected_axis ||
        result_shape[axis] != expected_extent) {
      return false;
    }
  }
  return true;
}

enum class MatrixOperation : std::uint8_t {
  none,
  multiply,
  left_divide,
  right_divide,
  integer_power
};
enum class MatrixSolveKind : std::uint8_t { none, square, overdetermined, underdetermined };
// Numerical conditioning is part of the source-language contract, not a target-runtime guess.
// Matlab's square solver continues after structure-specific condition warnings, while rectangular
// division returns a pivoted basic least-squares solution and warns below full numerical rank.
enum class MatrixConditionPolicy : std::uint8_t {
  none,
  square_continue_with_warning,
  basic_solution_with_warning
};
// Rectangular division requires an explicit rank-revealing factorization contract. Keeping the
// algorithm family separate from square-matrix structure classification lets new numeric domains
// share the same source semantics without selecting a target helper by spelling.
enum class MatrixFactorizationPolicy : std::uint8_t {
  none,
  rank_revealing_column_pivoted_qr,
  sparse_row_pivoted_lu
};
// Matrix arithmetic must not infer its numeric domain from a target representation. The semantic
// plan fixes whether a kernel consumes real or complex values before target-specific lowering.
enum class MatrixNumericDomain : std::uint8_t { none, real, complex };
// Square Matlab division examines runtime coefficient values before selecting a numerical kernel.
// Keeping this policy explicit prevents target lowering from silently adding or removing structure
// detection based on helper spelling. Sparse representations require a separate storage policy.
enum class MatrixStructurePolicy : std::uint8_t {
  none,
  classify_real_square,
  classify_complex_square,
  classify_sparse_real_square
};
// Operand storage participates in Matlab matrix dispatch independently of numeric domain and
// mathematical structure. A sparse coefficient selects a CSC-aware solver; target code must not
// infer this from a runtime object tag or helper spelling.
enum class MatrixStoragePolicy : std::uint8_t {
  none,
  dense,
  sparse_csc_coefficient,
  sparse_csc_multiply,
  sparse_csc_scale
};

[[nodiscard]] constexpr ArrayStorageFormat matrix_multiply_result_storage(
    const ArrayStorageFormat left, const ArrayStorageFormat right) noexcept {
  if ((left == ArrayStorageFormat::sparse_csc && right == ArrayStorageFormat::none) ||
      (left == ArrayStorageFormat::none && right == ArrayStorageFormat::sparse_csc)) {
    return ArrayStorageFormat::sparse_csc;
  }
  if (!array_storage_known(left) || !array_storage_known(right)) {
    return ArrayStorageFormat::none;
  }
  return left == ArrayStorageFormat::sparse_csc && right == ArrayStorageFormat::sparse_csc
             ? ArrayStorageFormat::sparse_csc
             : ArrayStorageFormat::dense;
}

[[nodiscard]] constexpr bool valid_matrix_multiply_storage_contract(
    const MatrixStoragePolicy policy, const ArrayStorageFormat left, const ArrayStorageFormat right,
    const ArrayStorageFormat result) noexcept {
  const auto expected_result = matrix_multiply_result_storage(left, right);
  if (expected_result == ArrayStorageFormat::none || result != expected_result) return false;
  const bool sparse_scale =
      (left == ArrayStorageFormat::sparse_csc && right == ArrayStorageFormat::none) ||
      (left == ArrayStorageFormat::none && right == ArrayStorageFormat::sparse_csc);
  if (sparse_scale) return policy == MatrixStoragePolicy::sparse_csc_scale;
  const bool has_sparse =
      left == ArrayStorageFormat::sparse_csc || right == ArrayStorageFormat::sparse_csc;
  return policy ==
         (has_sparse ? MatrixStoragePolicy::sparse_csc_multiply : MatrixStoragePolicy::dense);
}

// Matlab sparse construction has several observably different source forms.  Preserve the
// selected form through every IR layer so target planning never has to recover it from a callee
// spelling or argument count.
enum class SparseConstructionKind : std::uint8_t {
  none,
  dense_conversion,
  zero_matrix,
  triplets_inferred,
  triplets_sized,
  triplets_reserved
};

// Sparse indexing has distinct scalar and storage-preserving result contracts.  The selected
// source form remains explicit so neither target backend infers linearization or Cartesian
// submatrix semantics from selector count.
enum class SparseIndexKind : std::uint8_t {
  none,
  linear_element,
  subscript_element,
  linear_selection,
  submatrix_selection
};

// Matlab sparse reshape preserves column-major element order but always produces a two-dimensional
// sparse matrix.  A size vector and a comma-separated dimension list have different syntax-level
// contracts because only the latter can contain one inferred `[]` dimension.
enum class SparseReshapeKind : std::uint8_t { none, column_major_2d };
enum class SparseReshapeDimensionForm : std::uint8_t { none, size_vector, dimension_list };
enum class SparseReshapeInference : std::uint8_t { none, one_dimension };

template <typename Shape>
[[nodiscard]] bool valid_sparse_reshape_contract(
    const SparseReshapeKind kind, const SparseReshapeDimensionForm dimension_form,
    const SparseReshapeInference inference, const std::size_t inferred_axis,
    const ArrayStorageFormat source_storage, const ArrayStorageFormat result_storage,
    const Shape& input_shape, const Shape& requested_shape, const Shape& result_shape) noexcept {
  if (kind != SparseReshapeKind::column_major_2d ||
      dimension_form == SparseReshapeDimensionForm::none ||
      source_storage != ArrayStorageFormat::sparse_csc ||
      result_storage != ArrayStorageFormat::sparse_csc || input_shape.size() != 2U ||
      requested_shape.size() < 2U || result_shape.size() != 2U || input_shape[0] == 0U ||
      input_shape[1] == 0U || result_shape[0] == 0U || result_shape[1] == 0U) {
    return false;
  }
  constexpr auto maximum = std::numeric_limits<std::size_t>::max();
  const auto checked_product = [](const Shape& shape, const std::size_t begin,
                                  std::size_t& product) {
    product = 1U;
    for (std::size_t axis = begin; axis < shape.size(); ++axis) {
      const auto extent = shape[axis];
      if (extent == 0U || extent == maximum || product > maximum / extent) return false;
      product *= extent;
    }
    return true;
  };
  std::size_t input_count = 0U;
  std::size_t requested_count = 0U;
  std::size_t folded_columns = 0U;
  if (!checked_product(input_shape, 0U, input_count) ||
      !checked_product(requested_shape, 0U, requested_count) ||
      !checked_product(requested_shape, 1U, folded_columns) || input_count != requested_count ||
      result_shape[0] != requested_shape[0] || result_shape[1] != folded_columns) {
    return false;
  }
  if (inference == SparseReshapeInference::none) return inferred_axis == 0U;
  return inference == SparseReshapeInference::one_dimension &&
         dimension_form == SparseReshapeDimensionForm::dimension_list &&
         inferred_axis < requested_shape.size();
}

// Sparse writes require a storage-aware contract in addition to the generic indexed-mutation
// shape contract.  Assignment order and explicit-zero handling are observable Matlab semantics,
// so neither target is allowed to infer them from helper names or CSC representation details.
enum class SparseMutationKind : std::uint8_t {
  none,
  linear_assignment,
  subscript_assignment,
  linear_deletion,
  axis_deletion
};
enum class SparseReplacementKind : std::uint8_t { none, scalar_expansion, elementwise };
enum class SparseDuplicateWritePolicy : std::uint8_t { none, last_write_wins, erase_once };
enum class SparseZeroWritePolicy : std::uint8_t { none, erase_entry };

[[nodiscard]] constexpr bool sparse_mutation_is_assignment(const SparseMutationKind kind) noexcept {
  return kind == SparseMutationKind::linear_assignment ||
         kind == SparseMutationKind::subscript_assignment;
}

[[nodiscard]] constexpr bool sparse_mutation_is_deletion(const SparseMutationKind kind) noexcept {
  return kind == SparseMutationKind::linear_deletion || kind == SparseMutationKind::axis_deletion;
}

[[nodiscard]] constexpr bool sparse_mutation_is_linear(const SparseMutationKind kind) noexcept {
  return kind == SparseMutationKind::linear_assignment ||
         kind == SparseMutationKind::linear_deletion;
}

[[nodiscard]] constexpr bool sparse_index_returns_scalar(const SparseIndexKind kind) noexcept {
  return kind == SparseIndexKind::linear_element || kind == SparseIndexKind::subscript_element;
}

template <typename Shape>
[[nodiscard]] bool valid_sparse_index_contract(const SparseIndexKind kind,
                                               const ArrayStorageFormat source_storage,
                                               const ArrayStorageFormat result_storage,
                                               const Shape& input_shape, const Shape& result_shape,
                                               const std::size_t selector_count) noexcept {
  if (kind == SparseIndexKind::none || source_storage != ArrayStorageFormat::sparse_csc ||
      input_shape.size() != 2U || input_shape[0] == 0U || input_shape[1] == 0U ||
      input_shape[0] == std::numeric_limits<std::size_t>::max() ||
      input_shape[1] == std::numeric_limits<std::size_t>::max()) {
    return false;
  }
  const bool linear =
      kind == SparseIndexKind::linear_element || kind == SparseIndexKind::linear_selection;
  if (selector_count != (linear ? 1U : 2U)) return false;
  if (sparse_index_returns_scalar(kind)) {
    return result_storage == ArrayStorageFormat::none && result_shape.empty();
  }
  return result_storage == ArrayStorageFormat::sparse_csc && result_shape.size() == 2U;
}

[[nodiscard]] constexpr MatrixSolveKind matrix_solve_kind(const std::size_t rows,
                                                          const std::size_t columns) noexcept {
  if (rows == columns) return MatrixSolveKind::square;
  return rows > columns ? MatrixSolveKind::overdetermined : MatrixSolveKind::underdetermined;
}

[[nodiscard]] constexpr MatrixConditionPolicy matrix_condition_policy(
    const MatrixSolveKind solve) noexcept {
  switch (solve) {
    case MatrixSolveKind::none: return MatrixConditionPolicy::none;
    case MatrixSolveKind::square: return MatrixConditionPolicy::square_continue_with_warning;
    case MatrixSolveKind::overdetermined:
    case MatrixSolveKind::underdetermined:
      return MatrixConditionPolicy::basic_solution_with_warning;
  }
  return MatrixConditionPolicy::none;
}

[[nodiscard]] constexpr MatrixFactorizationPolicy matrix_factorization_policy(
    const MatrixSolveKind solve,
    const MatrixStoragePolicy storage = MatrixStoragePolicy::dense) noexcept {
  if (storage == MatrixStoragePolicy::sparse_csc_coefficient) {
    return solve == MatrixSolveKind::square ? MatrixFactorizationPolicy::sparse_row_pivoted_lu
                                            : MatrixFactorizationPolicy::none;
  }
  if (storage == MatrixStoragePolicy::sparse_csc_multiply) {
    return MatrixFactorizationPolicy::none;
  }
  if (storage == MatrixStoragePolicy::sparse_csc_scale) {
    return MatrixFactorizationPolicy::none;
  }
  switch (solve) {
    case MatrixSolveKind::overdetermined:
    case MatrixSolveKind::underdetermined:
      return MatrixFactorizationPolicy::rank_revealing_column_pivoted_qr;
    case MatrixSolveKind::none:
    case MatrixSolveKind::square: return MatrixFactorizationPolicy::none;
  }
  return MatrixFactorizationPolicy::none;
}

[[nodiscard]] constexpr MatrixStructurePolicy matrix_structure_policy(
    const MatrixSolveKind solve, const MatrixNumericDomain domain,
    const MatrixStoragePolicy storage = MatrixStoragePolicy::dense) noexcept {
  if (solve != MatrixSolveKind::square) return MatrixStructurePolicy::none;
  if (storage == MatrixStoragePolicy::sparse_csc_coefficient) {
    return domain == MatrixNumericDomain::real ? MatrixStructurePolicy::classify_sparse_real_square
                                               : MatrixStructurePolicy::none;
  }
  if (domain == MatrixNumericDomain::real) return MatrixStructurePolicy::classify_real_square;
  if (domain == MatrixNumericDomain::complex) {
    return MatrixStructurePolicy::classify_complex_square;
  }
  return MatrixStructurePolicy::none;
}

[[nodiscard]] constexpr MatrixStoragePolicy matrix_storage_policy(
    const MatrixOperation operation, const ArrayStorageFormat left,
    const ArrayStorageFormat right) noexcept {
  if (operation == MatrixOperation::left_divide && left == ArrayStorageFormat::sparse_csc) {
    return MatrixStoragePolicy::sparse_csc_coefficient;
  }
  if (operation == MatrixOperation::right_divide && right == ArrayStorageFormat::sparse_csc) {
    return MatrixStoragePolicy::sparse_csc_coefficient;
  }
  if (operation == MatrixOperation::multiply &&
      ((left == ArrayStorageFormat::sparse_csc && right == ArrayStorageFormat::none) ||
       (left == ArrayStorageFormat::none && right == ArrayStorageFormat::sparse_csc))) {
    return MatrixStoragePolicy::sparse_csc_scale;
  }
  if (operation == MatrixOperation::multiply && array_storage_known(left) &&
      array_storage_known(right) &&
      (left == ArrayStorageFormat::sparse_csc || right == ArrayStorageFormat::sparse_csc)) {
    return MatrixStoragePolicy::sparse_csc_multiply;
  }
  if (operation != MatrixOperation::none && array_storage_known(left) &&
      (right == ArrayStorageFormat::none || array_storage_known(right))) {
    return MatrixStoragePolicy::dense;
  }
  return MatrixStoragePolicy::none;
}
// Per-subscript execution contract. Keeping selector identity explicit avoids deriving Matlab
// indexing semantics again in each target backend.
enum class IndexSelectorKind : std::uint8_t { scalar, slice, numeric, logical, empty };

// Runtime source for Matlab's contextual `end` value. An axis extent is resolved against the
// current selector dimension; a linear extent is resolved against the column-major element count.
enum class IndexExtentSource : std::uint8_t { none, runtime_axis, runtime_linear };

// Analyzer-owned contract for a write through an index expression. Shape-changing writes are
// represented explicitly so MIR, target LIRs, and emitters never have to rediscover source
// language mutation semantics from syntax.
enum class IndexedMutationKind : std::uint8_t { none, overwrite, resize, grow, erase };
enum class IndexedMutationShapeSource : std::uint8_t {
  preserve,
  static_extents,
  runtime_selectors
};

struct IndexedMutationContract {
  IndexedMutationKind kind{IndexedMutationKind::none};
  IndexedMutationShapeSource shape_source{IndexedMutationShapeSource::preserve};
  bool linear{false};
  std::size_t axis{std::numeric_limits<std::size_t>::max()};

  [[nodiscard]] constexpr bool valid() const noexcept { return kind != IndexedMutationKind::none; }
  [[nodiscard]] constexpr bool changes_shape() const noexcept {
    return kind == IndexedMutationKind::resize || kind == IndexedMutationKind::grow ||
           kind == IndexedMutationKind::erase;
  }
};

template <typename Extents>
[[nodiscard]] bool valid_indexed_mutation_shapes(const IndexedMutationContract& contract,
                                                 const Extents& input,
                                                 const Extents& result) noexcept {
  if (!contract.valid() || input.empty() || input.size() != result.size()) return false;
  constexpr auto dynamic = std::numeric_limits<std::size_t>::max();
  if (contract.kind == IndexedMutationKind::overwrite) {
    return contract.shape_source == IndexedMutationShapeSource::preserve && input == result;
  }
  if (contract.kind == IndexedMutationKind::resize) {
    return contract.shape_source == IndexedMutationShapeSource::runtime_selectors;
  }
  if (contract.shape_source == IndexedMutationShapeSource::preserve) return false;
  if (contract.kind == IndexedMutationKind::grow) {
    bool changed = contract.shape_source == IndexedMutationShapeSource::runtime_selectors;
    for (std::size_t axis = 0; axis < input.size(); ++axis) {
      if (input[axis] != dynamic && result[axis] != dynamic && result[axis] < input[axis]) {
        return false;
      }
      changed = changed || input[axis] != result[axis];
    }
    return changed;
  }
  if (contract.kind == IndexedMutationKind::erase) {
    if (contract.axis >= input.size()) return false;
    bool changed = contract.shape_source == IndexedMutationShapeSource::runtime_selectors;
    for (std::size_t axis = 0; axis < input.size(); ++axis) {
      if (axis != contract.axis && input[axis] != result[axis]) return false;
      if (axis == contract.axis) {
        if (input[axis] != dynamic && result[axis] != dynamic && result[axis] > input[axis]) {
          return false;
        }
        changed = changed || input[axis] != result[axis];
      }
    }
    return changed;
  }
  return false;
}

template <typename Shape>
[[nodiscard]] bool valid_sparse_mutation_contract(
    const SparseMutationKind kind, const SparseReplacementKind replacement,
    const SparseDuplicateWritePolicy duplicate_policy, const SparseZeroWritePolicy zero_policy,
    const ArrayStorageFormat source_storage, const ArrayStorageFormat replacement_storage,
    const ArrayStorageFormat result_storage, const Shape& input_shape, const Shape& selection_shape,
    const Shape& replacement_shape, const Shape& result_shape, const std::size_t selector_count,
    const IndexedMutationContract& mutation) noexcept {
  constexpr auto dynamic = std::numeric_limits<std::size_t>::max();
  if (kind == SparseMutationKind::none || source_storage != ArrayStorageFormat::sparse_csc ||
      result_storage != ArrayStorageFormat::sparse_csc || input_shape.size() != 2U ||
      result_shape.size() != 2U || input_shape[0] == 0U || input_shape[1] == 0U ||
      input_shape[0] == dynamic || input_shape[1] == dynamic ||
      selector_count != (sparse_mutation_is_linear(kind) ? 1U : 2U) ||
      mutation.linear != sparse_mutation_is_linear(kind) ||
      !valid_indexed_mutation_shapes(mutation, input_shape, result_shape) ||
      (!selection_shape.empty() && selection_shape.size() != 2U)) {
    return false;
  }
  if (sparse_mutation_is_assignment(kind)) {
    if (mutation.kind != IndexedMutationKind::overwrite &&
        mutation.kind != IndexedMutationKind::grow) {
      return false;
    }
    if (replacement == SparseReplacementKind::none ||
        duplicate_policy != SparseDuplicateWritePolicy::last_write_wins ||
        zero_policy != SparseZeroWritePolicy::erase_entry ||
        (replacement_storage != ArrayStorageFormat::none &&
         replacement_storage != ArrayStorageFormat::dense &&
         replacement_storage != ArrayStorageFormat::sparse_csc)) {
      return false;
    }
    if (replacement_storage == ArrayStorageFormat::none) return replacement_shape.empty();
    if (replacement_shape.empty()) return false;
    return replacement_storage != ArrayStorageFormat::sparse_csc || replacement_shape.size() == 2U;
  }
  return sparse_mutation_is_deletion(kind) && mutation.kind == IndexedMutationKind::erase &&
         replacement == SparseReplacementKind::none &&
         duplicate_policy == SparseDuplicateWritePolicy::erase_once &&
         zero_policy == SparseZeroWritePolicy::none &&
         replacement_storage == ArrayStorageFormat::none && replacement_shape.empty();
}

[[nodiscard]] constexpr bool selector_preserves_dimension(const IndexSelectorKind kind) noexcept {
  return kind != IndexSelectorKind::scalar;
}

[[nodiscard]] constexpr bool requires_runtime_extent(const IndexExtentSource source) noexcept {
  return source != IndexExtentSource::none;
}

struct Profile {
  Truthiness truthiness{Truthiness::native};
  LogicalResult logical_result{LogicalResult::boolean};
  Equality equality{Equality::native};
  Division division{Division::native};
  DivisionByZero division_by_zero{DivisionByZero::target_native};
  IndexLayout layout{IndexLayout::row_major};
  TopLevelStorage top_level_storage{TopLevelStorage::module};
  ExportPolicy export_policy{ExportPolicy::all_top_level};
  ScopeModel scope_model{ScopeModel::function};
  bool resizable_sections{false};
  bool emit_parameter_defaults{false};
};

struct SourceDivisionContract {
  Division division{Division::native};
  DivisionByZero division_by_zero{DivisionByZero::target_native};
};

[[nodiscard]] constexpr SourceDivisionContract source_division_contract(
    const SourceLanguage language) noexcept {
  switch (language) {
    case SourceLanguage::python: return {Division::real_quotient, DivisionByZero::exception};
    case SourceLanguage::matlab:
    case SourceLanguage::typescript: return {Division::real_quotient, DivisionByZero::ieee754};
    case SourceLanguage::automatic:
    case SourceLanguage::fortran: return {};
  }
  return {};
}

[[nodiscard]] constexpr bool source_division_contract_matches(
    const SourceLanguage language, const Division division,
    const DivisionByZero division_by_zero) noexcept {
  const auto expected = source_division_contract(language);
  return valid_division_contract(division, division_by_zero) && division == expected.division &&
         division_by_zero == expected.division_by_zero;
}

[[nodiscard]] constexpr bool source_division_contract_matches(const SourceLanguage language,
                                                              const Profile& profile) noexcept {
  return source_division_contract_matches(language, profile.division, profile.division_by_zero);
}

}  // namespace mpf::detail::semantic
