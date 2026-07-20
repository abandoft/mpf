#include "sparse_reduction_runtime.hpp"

#include <ostream>

namespace mpf::detail {

void emit_cpp_sparse_reduction_runtime(std::ostream& output) {
  output << R"MPF(template <typename Value>
sparse_matrix<bool> sparse_logical_rows(const sparse_matrix<Value>& value,
                                        const bool all_mode) {
  auto sorted_rows = value.row_indices;
  std::sort(sorted_rows.begin(), sorted_rows.end());
  sparse_matrix<bool> result;
  result.rows = value.rows; result.columns = 1U; result.column_pointers = {0U};
  if (all_mode && value.columns == 0U) {
    result.row_indices.reserve(value.rows); result.values.reserve(value.rows);
    for (std::size_t row = 0U; row < value.rows; ++row) {
      result.row_indices.push_back(row); result.values.push_back(true);
    }
  } else {
    for (std::size_t index = 0U; index < sorted_rows.size();) {
      const auto row = sorted_rows[index]; auto end = index + 1U;
      while (end < sorted_rows.size() && sorted_rows[end] == row) ++end;
      if (!all_mode || end - index == value.columns) {
        result.row_indices.push_back(row); result.values.push_back(true);
      }
      index = end;
    }
  }
  result.column_pointers.push_back(result.values.size());
  validate_sparse_csc(result, "sparse logical row reduction result");
  return result;
}
template <typename Value>
sparse_matrix<bool> sparse_logical_columns(const sparse_matrix<Value>& value,
                                           const bool all_mode) {
  sparse_matrix<bool> result;
  result.rows = 1U; result.columns = value.columns;
  result.column_pointers.reserve(result.columns + 1U); result.column_pointers.push_back(0U);
  for (std::size_t column = 0U; column < value.columns; ++column) {
    const auto count = value.column_pointers[column + 1U] - value.column_pointers[column];
    if (all_mode ? count == value.rows : count != 0U) {
      result.row_indices.push_back(0U); result.values.push_back(true);
    }
    result.column_pointers.push_back(result.values.size());
  }
  validate_sparse_csc(result, "sparse logical column reduction result");
  return result;
}
template <typename Value>
sparse_matrix<bool> sparse_logical_copy(const sparse_matrix<Value>& value) {
  sparse_matrix<bool> result;
  result.rows = value.rows; result.columns = value.columns;
  result.column_pointers = value.column_pointers; result.row_indices = value.row_indices;
  result.values.assign(value.values.size(), true);
  validate_sparse_csc(result, "sparse logical reduction result");
  return result;
}
template <typename Value, std::size_t AxisCount>
bool sparse_logical_reduction_scalar(
    const sparse_matrix<Value>& value, const std::array<std::size_t, AxisCount>& axes,
    const bool all_mode, const std::size_t input_size) {
  if (axes.empty() || axes.size() == 2U)
    return all_mode ? value.values.size() == input_size : !value.values.empty();
  if (axes[0] == 0U) {
    const auto count = value.column_pointers[1U] - value.column_pointers[0U];
    return all_mode ? count == value.rows : count != 0U;
  }
  return all_mode ? value.values.size() == value.columns : !value.values.empty();
}
template <bool AllMode, std::size_t OutputRank, typename Value, std::size_t InputRank,
          std::size_t AxisCount>
auto sparse_logical_reduce(
    const sparse_matrix<Value>& value,
    const std::array<std::size_t, InputRank>& input_shape,
    const std::array<std::size_t, AxisCount>& axes,
    const std::array<std::size_t, InputRank>& result_shape,
    const std::array<std::size_t, OutputRank>& output_shape,
    const std::int64_t operation, const std::int64_t policy,
    const std::int64_t input_storage, const std::int64_t result_storage) {
  static_assert(InputRank == 2U, "MPF sparse logical reduction input must be rank two");
  static_assert(OutputRank == 0U || OutputRank == 2U,
                "MPF sparse logical reduction output rank is invalid");
  validate_sparse_csc(value, "sparse logical reduction operand");
  if (value.rows != input_shape[0] || value.columns != input_shape[1])
    throw std::invalid_argument(
        "MPF Matlab sparse logical reduction operand disagrees with its shape plan");
  std::size_t previous = 0U; bool has_previous = false;
  for (const auto axis : axes) {
    if (axis >= InputRank || (has_previous && axis <= previous))
      throw std::invalid_argument("MPF Matlab sparse logical reduction axes are invalid");
    previous = axis; has_previous = true;
  }
  const auto reduced_axis = [&](const std::size_t wanted) {
    return std::find(axes.begin(), axes.end(), wanted) != axes.end();
  };
  for (std::size_t axis = 0U; axis < InputRank; ++axis) {
    const auto expected = reduced_axis(axis) ? 1U : input_shape[axis];
    if (result_shape[axis] != expected)
      throw std::invalid_argument(
          "MPF Matlab sparse logical reduction result shape is invalid");
  }
  constexpr auto expected_policy = OutputRank == 0U ? 3 : 2;
  constexpr auto expected_result_storage = OutputRank == 0U ? 0 : 3;
  if (operation != (AllMode ? 1 : 2) || policy != expected_policy || input_storage != 3 ||
      result_storage != expected_result_storage)
    throw std::invalid_argument(
        "MPF Matlab sparse logical reduction storage plan is invalid");
  const auto input_size = shape_size(input_shape);
  const auto result_size = shape_size(result_shape);
  if constexpr (OutputRank == 0U) {
    if (result_size != 1U)
      throw std::invalid_argument(
          "MPF Matlab sparse logical scalar reduction shape is invalid");
    return sparse_logical_reduction_scalar(value, axes, AllMode, input_size);
  } else {
    if (output_shape != result_shape || result_size == 1U)
      throw std::invalid_argument(
          "MPF Matlab sparse logical array reduction shape is invalid");
    if constexpr (AxisCount == 0U) return sparse_logical_copy(value);
    if constexpr (AxisCount == 1U) {
      if (axes[0] == 0U) return sparse_logical_columns(value, AllMode);
      return sparse_logical_rows(value, AllMode);
    }
    throw std::invalid_argument(
        "MPF Matlab nonscalar sparse logical reduction axes are invalid");
  }
}
)MPF";
}

}  // namespace mpf::detail
