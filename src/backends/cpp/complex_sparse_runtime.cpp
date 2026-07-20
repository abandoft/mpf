#include "complex_sparse_runtime.hpp"

#include <ostream>

namespace mpf::detail {

void emit_cpp_complex_sparse_runtime(std::ostream& output) {
  output << R"MPF(template <typename Values>
sparse_matrix<std::complex<double>> sparse_complex_from_dense(
    const Values& values, const std::array<std::size_t, 2U>& planned_shape) {
  return sparse_from_dense_impl<std::complex<double>>(values, planned_shape);
}
template <typename T>
sparse_matrix<std::complex<double>> sparse_complex_from_dense(
    const sparse_matrix<T>& matrix, const std::array<std::size_t, 2U>& planned_shape) {
  validate_sparse_csc(matrix);
  if (matrix.rows != planned_shape[0] || matrix.columns != planned_shape[1])
    throw std::invalid_argument(
        "MPF Matlab sparse input disagrees with its static shape contract");
  sparse_matrix<std::complex<double>> result;
  result.rows = matrix.rows;
  result.columns = matrix.columns;
  result.column_pointers = matrix.column_pointers;
  result.row_indices = matrix.row_indices;
  result.values.reserve(matrix.values.size());
  for (const auto& value : matrix.values)
    result.values.push_back(sparse_stored_cast<std::complex<double>>(value));
  validate_sparse_csc(result);
  return result;
}
template <typename Row, typename Column, typename Value>
sparse_matrix<std::complex<double>> sparse_complex_sum(
    const Row& rows, const Column& columns, const Value& values) {
  return sparse_from_triplets_impl<std::complex<double>, false>(rows, columns, values);
}
template <typename Row, typename Column, typename Value, typename Rows, typename Columns>
sparse_matrix<std::complex<double>> sparse_complex_sum(
    const Row& row_values, const Column& column_values, const Value& values,
    const Rows& rows_value, const Columns& columns_value) {
  return sparse_from_triplets_impl<std::complex<double>, false>(
      row_values, column_values, values,
      sparse_dimension(rows_value, "row extent", true),
      sparse_dimension(columns_value, "column extent", true));
}
template <typename Row, typename Column, typename Value, typename Rows, typename Columns,
          typename Reserve>
sparse_matrix<std::complex<double>> sparse_complex_sum(
    const Row& row_values, const Column& column_values, const Value& values,
    const Rows& rows_value, const Columns& columns_value, const Reserve& reserve_value) {
  return sparse_from_triplets_impl<std::complex<double>, false>(
      row_values, column_values, values,
      sparse_dimension(rows_value, "row extent", true),
      sparse_dimension(columns_value, "column extent", true),
      sparse_dimension(reserve_value, "nzmax", true));
}
template <typename T> sparse_matrix<T> sparse_ctranspose(const sparse_matrix<T>& value) {
  auto result = sparse_transpose(value);
  for (auto& stored : result.values) stored = conjugate(stored);
  validate_sparse_csc(result, "conjugate-transpose result");
  return result;
}
)MPF";
}

}  // namespace mpf::detail
