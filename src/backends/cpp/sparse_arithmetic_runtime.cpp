#include "sparse_arithmetic_runtime.hpp"

#include <ostream>

namespace mpf::detail {

void emit_cpp_sparse_arithmetic_runtime(std::ostream& output) {
  output << R"MPF(template <typename Value>
double sparse_arithmetic_number(const Value& value, const std::string& name) {
  static_assert(std::is_arithmetic_v<std::decay_t<Value>>,
                "MPF Matlab sparse arithmetic values must be arithmetic");
  const auto result = static_cast<double>(value);
  if (!std::isfinite(result))
    throw std::invalid_argument("MPF Matlab " + name +
                                " must contain finite real or logical values");
  return result;
}
template <typename Value>
void validate_sparse_arithmetic_operand(
    const sparse_matrix<Value>& matrix, const std::array<std::size_t, 2U>& shape,
    const std::string& name) {
  validate_sparse_csc(matrix, name);
  if (matrix.rows != shape[0] || matrix.columns != shape[1])
    throw std::invalid_argument("MPF Matlab " + name +
                                " disagrees with its static shape contract");
}
template <typename Left, typename Right, std::size_t LeftRank, std::size_t RightRank>
void validate_sparse_arithmetic_plan(
    const std::array<std::size_t, LeftRank>& left_shape,
    const std::array<std::size_t, RightRank>& right_shape,
    const std::array<std::size_t, 2U>& result_shape,
    const std::int64_t operation, const std::int64_t policy,
    const std::int64_t left_storage, const std::int64_t right_storage,
    const std::int64_t result_storage, const std::int64_t expected_operation) {
  constexpr bool left_sparse = is_sparse_matrix<std::decay_t<Left>>::value;
  constexpr bool right_sparse = is_sparse_matrix<std::decay_t<Right>>::value;
  constexpr bool left_dense = is_vector<std::decay_t<Left>>::value;
  constexpr bool right_dense = is_vector<std::decay_t<Right>>::value;
  static_assert(left_sparse || right_sparse,
                "MPF sparse arithmetic requires at least one sparse operand");
  static_assert(left_sparse || left_dense || std::is_arithmetic_v<std::decay_t<Left>>,
                "MPF sparse arithmetic left operand is unsupported");
  static_assert(right_sparse || right_dense || std::is_arithmetic_v<std::decay_t<Right>>,
                "MPF sparse arithmetic right operand is unsupported");
  static_assert((left_sparse || left_dense) ? LeftRank == 2U : LeftRank == 0U,
                "MPF sparse arithmetic left shape rank is invalid");
  static_assert((right_sparse || right_dense) ? RightRank == 2U : RightRank == 0U,
                "MPF sparse arithmetic right shape rank is invalid");
  constexpr std::int64_t expected_left_storage = left_sparse ? 3 : left_dense ? 2 : 0;
  constexpr std::int64_t expected_right_storage = right_sparse ? 3 : right_dense ? 2 : 0;
  constexpr std::int64_t expected_result_storage = left_sparse && right_sparse ? 3 : 2;
  constexpr std::int64_t expected_policy = left_sparse && right_sparse ? 1 : 2;
  if (operation != expected_operation || policy != expected_policy ||
      left_storage != expected_left_storage || right_storage != expected_right_storage ||
      result_storage != expected_result_storage)
    throw std::invalid_argument("MPF Matlab sparse arithmetic storage plan is invalid");
  for (std::size_t axis = 0U; axis < 2U; ++axis) {
    const auto left = [&] {
      if constexpr (LeftRank == 0U) return std::size_t{1U};
      else return left_shape[axis];
    }();
    const auto right = [&] {
      if constexpr (RightRank == 0U) return std::size_t{1U};
      else return right_shape[axis];
    }();
    const auto expected = left == right ? left : left == 1U ? right : left;
    if ((left != right && left != 1U && right != 1U) || result_shape[axis] != expected)
      throw std::invalid_argument("MPF Matlab sparse arithmetic shape mismatch");
  }
  static_cast<void>(shape_size(result_shape));
}
inline void sparse_arithmetic_emit(sparse_matrix<double>& result, const std::size_t row,
                                   const double value) {
  if (!std::isfinite(value))
    throw std::overflow_error("MPF Matlab sparse arithmetic produced a nonfinite value");
  if (value != 0.0) {
    result.row_indices.push_back(row);
    result.values.push_back(value);
  }
}
template <bool Subtract, typename Left, typename Right>
void sparse_arithmetic_sparse_column(
    const sparse_matrix<Left>& left, const sparse_matrix<Right>& right,
    const std::size_t left_column, const std::size_t right_column,
    sparse_matrix<double>& result) {
  auto left_index = left.column_pointers[left_column];
  const auto left_end = left.column_pointers[left_column + 1U];
  auto right_index = right.column_pointers[right_column];
  const auto right_end = right.column_pointers[right_column + 1U];
  const bool left_broadcast = left.rows == 1U && result.rows != 1U;
  const bool right_broadcast = right.rows == 1U && result.rows != 1U;
  const auto left_broadcast_value =
      left_broadcast && left_index < left_end
          ? static_cast<double>(left.values[left_index])
          : 0.0;
  const auto right_broadcast_value =
      right_broadcast && right_index < right_end
          ? static_cast<double>(right.values[right_index])
          : 0.0;
  if (left_broadcast_value != 0.0 || right_broadcast_value != 0.0) {
    for (std::size_t row = 0U; row < result.rows; ++row) {
      auto left_value = left_broadcast_value;
      auto right_value = right_broadcast_value;
      if (!left_broadcast && left_index < left_end && left.row_indices[left_index] == row)
        left_value = static_cast<double>(left.values[left_index++]);
      if (!right_broadcast && right_index < right_end && right.row_indices[right_index] == row)
        right_value = static_cast<double>(right.values[right_index++]);
      sparse_arithmetic_emit(result, row,
                             Subtract ? left_value - right_value
                                      : left_value + right_value);
    }
    return;
  }
  while (left_index < left_end || right_index < right_end) {
    const auto left_row = left_index < left_end ? left.row_indices[left_index] : result.rows;
    const auto right_row = right_index < right_end ? right.row_indices[right_index] : result.rows;
    const auto row = std::min(left_row, right_row);
    const auto left_value = left_row == row
                                ? static_cast<double>(left.values[left_index++])
                                : 0.0;
    const auto right_value = right_row == row
                                 ? static_cast<double>(right.values[right_index++])
                                 : 0.0;
    sparse_arithmetic_emit(result, row,
                           Subtract ? left_value - right_value : left_value + right_value);
  }
}
template <bool Subtract, typename Left, typename Right>
sparse_matrix<double> sparse_arithmetic_preserve(
    const sparse_matrix<Left>& left, const sparse_matrix<Right>& right,
    const std::array<std::size_t, 2U>& left_shape,
    const std::array<std::size_t, 2U>& right_shape,
    const std::array<std::size_t, 2U>& result_shape) {
  validate_sparse_arithmetic_operand(left, left_shape, "left sparse arithmetic operand");
  validate_sparse_arithmetic_operand(right, right_shape, "right sparse arithmetic operand");
  sparse_matrix<double> result;
  result.rows = result_shape[0]; result.columns = result_shape[1];
  result.column_pointers.reserve(result.columns + 1U);
  result.column_pointers.push_back(0U);
  for (std::size_t column = 0U; column < result.columns; ++column) {
    sparse_arithmetic_sparse_column<Subtract>(
        left, right, left.columns == 1U ? 0U : column,
        right.columns == 1U ? 0U : column, result);
    result.column_pointers.push_back(result.values.size());
  }
  validate_sparse_csc(result, "sparse arithmetic result");
  return result;
}
template <typename Full, std::size_t Rank>
std::vector<double> sparse_arithmetic_full_input(
    const Full& value, const std::array<std::size_t, Rank>& shape,
    const std::string& name) {
  if constexpr (Rank == 0U) {
    return {sparse_arithmetic_number(value, name)};
  } else {
    static_assert(Rank == 2U && is_vector<std::decay_t<Full>>::value,
                  "MPF sparse arithmetic full operand must be scalar or rank two");
    const auto flattened = flatten_selector_column_major(value);
    if (flattened.size() != shape_size(shape))
      throw std::invalid_argument("MPF Matlab " + name +
                                  " disagrees with its static shape contract");
    std::vector<double> result;
    result.reserve(flattened.size());
    for (const auto item : flattened) result.push_back(sparse_arithmetic_number(item, name));
    return result;
  }
}
template <bool Subtract, bool SparseLeft, typename SparseValue, typename Full,
          std::size_t FullRank>
std::vector<std::vector<double>> sparse_arithmetic_materialize(
    const sparse_matrix<SparseValue>& sparse, const Full& full,
    const std::array<std::size_t, 2U>& sparse_shape,
    const std::array<std::size_t, FullRank>& full_shape,
    const std::array<std::size_t, 2U>& result_shape) {
  validate_sparse_arithmetic_operand(
      sparse, sparse_shape, SparseLeft ? "left sparse arithmetic operand"
                                       : "right sparse arithmetic operand");
  const auto full_values = sparse_arithmetic_full_input(
      full, full_shape, SparseLeft ? "right full arithmetic operand"
                                   : "left full arithmetic operand");
  auto result = make_nested<0U, 2U, double>(result_shape);
  constexpr double full_sign = SparseLeft && Subtract ? -1.0 : 1.0;
  for (std::size_t column = 0U; column < result_shape[1]; ++column) {
    for (std::size_t row = 0U; row < result_shape[0]; ++row) {
      const auto full_value = [&] {
        if constexpr (FullRank == 0U) {
          return full_values[0];
        } else {
          const auto source_row = full_shape[0] == 1U ? 0U : row;
          const auto source_column = full_shape[1] == 1U ? 0U : column;
          return full_values[source_row + source_column * full_shape[0]];
        }
      }();
      result[row][column] = full_sign * full_value;
    }
  }
  constexpr double sparse_sign = SparseLeft || !Subtract ? 1.0 : -1.0;
  for (std::size_t column = 0U; column < result_shape[1]; ++column) {
    const auto source_column = sparse.columns == 1U ? 0U : column;
    for (auto index = sparse.column_pointers[source_column];
         index < sparse.column_pointers[source_column + 1U]; ++index) {
      const auto stored = sparse_sign * static_cast<double>(sparse.values[index]);
      const auto source_row = sparse.row_indices[index];
      const auto begin = sparse.rows == 1U && result_shape[0] != 1U ? 0U : source_row;
      const auto end = sparse.rows == 1U && result_shape[0] != 1U
                           ? result_shape[0]
                           : source_row + 1U;
      for (auto row = begin; row < end; ++row) {
        const auto updated = result[row][column] + stored;
        if (!std::isfinite(updated))
          throw std::overflow_error("MPF Matlab sparse arithmetic produced a nonfinite value");
        result[row][column] = updated;
      }
    }
  }
  return result;
}
template <bool Subtract, typename Left, typename Right,
          std::size_t LeftRank, std::size_t RightRank>
auto sparse_arithmetic(
    const Left& left, const Right& right,
    const std::array<std::size_t, LeftRank>& left_shape,
    const std::array<std::size_t, RightRank>& right_shape,
    const std::array<std::size_t, 2U>& result_shape,
    const std::int64_t operation, const std::int64_t policy,
    const std::int64_t left_storage, const std::int64_t right_storage,
    const std::int64_t result_storage, const std::int64_t expected_operation) {
  validate_sparse_arithmetic_plan<Left, Right>(
      left_shape, right_shape, result_shape, operation, policy,
      left_storage, right_storage, result_storage, expected_operation);
  constexpr bool left_sparse = is_sparse_matrix<std::decay_t<Left>>::value;
  constexpr bool right_sparse = is_sparse_matrix<std::decay_t<Right>>::value;
  if constexpr (left_sparse && right_sparse) {
    return sparse_arithmetic_preserve<Subtract>(
        left, right, left_shape, right_shape, result_shape);
  } else if constexpr (left_sparse) {
    return sparse_arithmetic_materialize<Subtract, true>(
        left, right, left_shape, right_shape, result_shape);
  } else {
    return sparse_arithmetic_materialize<Subtract, false>(
        right, left, right_shape, left_shape, result_shape);
  }
}
template <typename Left, typename Right, std::size_t LeftRank, std::size_t RightRank>
auto sparse_add(
    const Left& left, const Right& right,
    const std::array<std::size_t, LeftRank>& left_shape,
    const std::array<std::size_t, RightRank>& right_shape,
    const std::array<std::size_t, 2U>& result_shape,
    const std::int64_t operation, const std::int64_t policy,
    const std::int64_t left_storage, const std::int64_t right_storage,
    const std::int64_t result_storage) {
  return sparse_arithmetic<false>(left, right, left_shape, right_shape, result_shape,
                                  operation, policy, left_storage, right_storage,
                                  result_storage, 1);
}
template <typename Left, typename Right, std::size_t LeftRank, std::size_t RightRank>
auto sparse_subtract(
    const Left& left, const Right& right,
    const std::array<std::size_t, LeftRank>& left_shape,
    const std::array<std::size_t, RightRank>& right_shape,
    const std::array<std::size_t, 2U>& result_shape,
    const std::int64_t operation, const std::int64_t policy,
    const std::int64_t left_storage, const std::int64_t right_storage,
    const std::int64_t result_storage) {
  return sparse_arithmetic<true>(left, right, left_shape, right_shape, result_shape,
                                 operation, policy, left_storage, right_storage,
                                 result_storage, 2);
}
)MPF";
}

}  // namespace mpf::detail
