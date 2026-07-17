#pragma once

#include <cstddef>
#include <cstdint>

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
enum class ArrayOperation : std::uint8_t { native, matlab };
enum class MatrixOperation : std::uint8_t {
  none,
  multiply,
  left_divide,
  right_divide,
  integer_power
};
enum class MatrixSolveKind : std::uint8_t { none, square, overdetermined, underdetermined };

[[nodiscard]] constexpr MatrixSolveKind matrix_solve_kind(const std::size_t rows,
                                                          const std::size_t columns) noexcept {
  if (rows == columns) return MatrixSolveKind::square;
  return rows > columns ? MatrixSolveKind::overdetermined : MatrixSolveKind::underdetermined;
}
// Per-subscript execution contract. Keeping selector identity explicit avoids deriving Matlab
// indexing semantics again in each target backend.
enum class IndexSelectorKind : std::uint8_t { scalar, slice, numeric, logical, empty };

[[nodiscard]] constexpr bool selector_preserves_dimension(const IndexSelectorKind kind) noexcept {
  return kind != IndexSelectorKind::scalar;
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
