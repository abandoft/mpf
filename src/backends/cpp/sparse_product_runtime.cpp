#include "sparse_product_runtime.hpp"

#include <ostream>

namespace mpf::detail {

void emit_cpp_sparse_product_runtime(std::ostream& output) {
  output << R"MPF(inline std::size_t sparse_product_size(
    const std::array<std::size_t, 2U>& shape, const std::string& name) {
  if (shape[0] == std::numeric_limits<std::size_t>::max() ||
      shape[1] == std::numeric_limits<std::size_t>::max() ||
      (shape[0] != 0U && shape[1] > std::numeric_limits<std::size_t>::max() / shape[0]))
    throw std::length_error("MPF Matlab sparse matrix product " + name + " shape is invalid");
  return shape[0] * shape[1];
}
inline void validate_sparse_product_plan(
    const std::array<std::size_t, 2U>& left_shape,
    const std::array<std::size_t, 2U>& right_shape,
    const std::array<std::size_t, 2U>& result_shape) {
  (void)sparse_product_size(left_shape, "left");
  (void)sparse_product_size(right_shape, "right");
  (void)sparse_product_size(result_shape, "result");
  if (left_shape[1] != right_shape[0] || result_shape[0] != left_shape[0] ||
      result_shape[1] != right_shape[1])
    throw std::invalid_argument("MPF Matlab sparse matrix product shape plans are inconsistent");
}
template <typename Left, typename Right>
using sparse_product_result_t = std::decay_t<decltype(
    std::declval<Left>() * std::declval<Right>() + 0.0)>;
template <typename Output>
void validate_sparse_product_domain(const std::int64_t numeric_domain) {
  const auto expected = std::is_arithmetic_v<Output> ? 1 : 2;
  if (numeric_domain != expected)
    throw std::invalid_argument(
        "MPF Matlab sparse matrix product numeric-domain plan is invalid");
}
template <typename Output, typename Left, typename Right>
Output sparse_product_accumulate(const Output& accumulator, const Left& left,
                                 const Right& right) {
  const auto result = accumulator + sparse_stored_cast<Output>(left) *
                                        sparse_stored_cast<Output>(right);
  if (!sparse_value_finite(result))
    throw std::overflow_error("MPF Matlab sparse matrix product produced a nonfinite value");
  return result;
}
template <typename Output, typename Dense>
std::vector<Output> sparse_product_dense_input(
    const Dense& value, const std::array<std::size_t, 2U>& shape,
    const std::string& name) {
  std::vector<std::size_t> actual_shape;
  selector_shape(value, actual_shape);
  const auto flattened = flatten_selector_column_major(value);
  const auto expected = sparse_product_size(shape, name);
  if (flattened.size() != expected ||
      (expected != 0U &&
       actual_shape != std::vector<std::size_t>(shape.begin(), shape.end())))
    throw std::invalid_argument("MPF Matlab " + name +
                                " disagrees with its static shape contract");
  std::vector<Output> result;
  result.reserve(flattened.size());
  for (const auto& item : flattened)
    result.push_back(sparse_stored_cast<Output>(item));
  return result;
}
template <typename Left, typename Right>
sparse_matrix<sparse_product_result_t<Left, Right>> sparse_sparse_mtimes(
    const sparse_matrix<Left>& left, const sparse_matrix<Right>& right,
    const std::array<std::size_t, 2U>& left_shape,
    const std::array<std::size_t, 2U>& right_shape,
    const std::array<std::size_t, 2U>& result_shape,
    const std::int64_t numeric_domain) {
  validate_sparse_product_plan(left_shape, right_shape, result_shape);
  validate_sparse_csc(left, "left matrix product operand");
  validate_sparse_csc(right, "right matrix product operand");
  if (left.rows != left_shape[0] || left.columns != left_shape[1] ||
      right.rows != right_shape[0] || right.columns != right_shape[1])
    throw std::invalid_argument(
        "MPF Matlab sparse matrix operands disagree with their shape plans");
  using Output = sparse_product_result_t<Left, Right>;
  validate_sparse_product_domain<Output>(numeric_domain);
  sparse_matrix<Output> result;
  result.rows = result_shape[0];
  result.columns = result_shape[1];
  result.column_pointers.reserve(result.columns + 1U);
  result.column_pointers.push_back(0U);
  std::vector<std::size_t> marker(left.rows, std::numeric_limits<std::size_t>::max());
  std::vector<Output> accumulator(left.rows);
  std::vector<std::size_t> touched;
  for (std::size_t column = 0U; column < right.columns; ++column) {
    touched.clear();
    for (auto right_index = right.column_pointers[column];
         right_index < right.column_pointers[column + 1U]; ++right_index) {
      const auto inner = right.row_indices[right_index];
      const auto& right_value = right.values[right_index];
      for (auto left_index = left.column_pointers[inner];
           left_index < left.column_pointers[inner + 1U]; ++left_index) {
        const auto row = left.row_indices[left_index];
        const auto initial = marker[row] != column ? Output{} : accumulator[row];
        const auto updated =
            sparse_product_accumulate(initial, left.values[left_index], right_value);
        if (marker[row] != column) {
          marker[row] = column;
          touched.push_back(row);
        }
        accumulator[row] = updated;
      }
    }
    std::sort(touched.begin(), touched.end());
    for (const auto row : touched) {
      const auto value = accumulator[row];
      if (sparse_value_nonzero(value)) {
        result.row_indices.push_back(row);
        result.values.push_back(value);
      }
    }
    result.column_pointers.push_back(result.values.size());
  }
  validate_sparse_csc(result, "matrix product result");
  return result;
}
template <typename Left, typename Dense>
std::vector<std::vector<sparse_product_result_t<Left, scalar_type_t<Dense>>>>
sparse_dense_mtimes(
    const sparse_matrix<Left>& left, const Dense& right_value,
    const std::array<std::size_t, 2U>& left_shape,
    const std::array<std::size_t, 2U>& right_shape,
    const std::array<std::size_t, 2U>& result_shape,
    const std::int64_t numeric_domain) {
  validate_sparse_product_plan(left_shape, right_shape, result_shape);
  validate_sparse_csc(left, "left matrix product operand");
  if (left.rows != left_shape[0] || left.columns != left_shape[1])
    throw std::invalid_argument(
        "MPF Matlab left sparse matrix disagrees with its shape plan");
  using Output = sparse_product_result_t<Left, scalar_type_t<Dense>>;
  validate_sparse_product_domain<Output>(numeric_domain);
  const auto right = sparse_product_dense_input<Output>(
      right_value, right_shape, "right matrix product operand");
  std::vector<std::vector<Output>> result(
      result_shape[0], std::vector<Output>(result_shape[1]));
  for (std::size_t inner = 0U; inner < left.columns; ++inner) {
    for (auto index = left.column_pointers[inner]; index < left.column_pointers[inner + 1U];
         ++index) {
      const auto row = left.row_indices[index];
      for (std::size_t column = 0U; column < result_shape[1]; ++column)
        result[row][column] = sparse_product_accumulate(
            result[row][column], left.values[index],
            right[inner + column * right_shape[0]]);
    }
  }
  return result;
}
template <typename Dense, typename Right>
std::vector<std::vector<sparse_product_result_t<scalar_type_t<Dense>, Right>>>
dense_sparse_mtimes(
    const Dense& left_value, const sparse_matrix<Right>& right,
    const std::array<std::size_t, 2U>& left_shape,
    const std::array<std::size_t, 2U>& right_shape,
    const std::array<std::size_t, 2U>& result_shape,
    const std::int64_t numeric_domain) {
  validate_sparse_product_plan(left_shape, right_shape, result_shape);
  using Output = sparse_product_result_t<scalar_type_t<Dense>, Right>;
  validate_sparse_product_domain<Output>(numeric_domain);
  const auto left = sparse_product_dense_input<Output>(
      left_value, left_shape, "left matrix product operand");
  validate_sparse_csc(right, "right matrix product operand");
  if (right.rows != right_shape[0] || right.columns != right_shape[1])
    throw std::invalid_argument(
        "MPF Matlab right sparse matrix disagrees with its shape plan");
  std::vector<std::vector<Output>> result(
      result_shape[0], std::vector<Output>(result_shape[1]));
  for (std::size_t column = 0U; column < right.columns; ++column) {
    for (auto index = right.column_pointers[column]; index < right.column_pointers[column + 1U];
         ++index) {
      const auto inner = right.row_indices[index];
      for (std::size_t row = 0U; row < result_shape[0]; ++row)
        result[row][column] = sparse_product_accumulate(
            result[row][column], left[row + inner * left_shape[0]], right.values[index]);
    }
  }
  return result;
}
)MPF";
}

}  // namespace mpf::detail
