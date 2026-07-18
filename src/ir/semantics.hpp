#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

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
// Square Matlab division examines runtime real coefficient values before selecting a numerical
// kernel. Keeping this policy explicit prevents target lowering from silently adding or removing
// structure detection based on helper spelling. Complex and sparse representations require their
// own policies once those value/storage contracts exist.
enum class MatrixStructurePolicy : std::uint8_t { none, classify_real_square };

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

[[nodiscard]] constexpr MatrixStructurePolicy matrix_structure_policy(
    const MatrixSolveKind solve) noexcept {
  return solve == MatrixSolveKind::square ? MatrixStructurePolicy::classify_real_square
                                          : MatrixStructurePolicy::none;
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
