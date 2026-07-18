#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

namespace mpf::detail::semantic {

enum class Truthiness { native, dynamic };
enum class LogicalResult { boolean, operand };
enum class Equality { native, structural };
enum class Division { native, real_quotient };
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
// Matlab's square solver continues after LU condition warnings, while rectangular division returns
// a pivoted basic least-squares solution and warns below full numerical rank.
enum class MatrixConditionPolicy : std::uint8_t {
  none,
  lu_continue_with_warning,
  basic_solution_with_warning
};

[[nodiscard]] constexpr MatrixSolveKind matrix_solve_kind(const std::size_t rows,
                                                          const std::size_t columns) noexcept {
  if (rows == columns) return MatrixSolveKind::square;
  return rows > columns ? MatrixSolveKind::overdetermined : MatrixSolveKind::underdetermined;
}

[[nodiscard]] constexpr MatrixConditionPolicy matrix_condition_policy(
    const MatrixSolveKind solve) noexcept {
  switch (solve) {
    case MatrixSolveKind::none: return MatrixConditionPolicy::none;
    case MatrixSolveKind::square: return MatrixConditionPolicy::lu_continue_with_warning;
    case MatrixSolveKind::overdetermined:
    case MatrixSolveKind::underdetermined:
      return MatrixConditionPolicy::basic_solution_with_warning;
  }
  return MatrixConditionPolicy::none;
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
  IndexLayout layout{IndexLayout::row_major};
  TopLevelStorage top_level_storage{TopLevelStorage::module};
  ExportPolicy export_policy{ExportPolicy::all_top_level};
  ScopeModel scope_model{ScopeModel::function};
  bool resizable_sections{false};
  bool emit_parameter_defaults{false};
};

}  // namespace mpf::detail::semantic
