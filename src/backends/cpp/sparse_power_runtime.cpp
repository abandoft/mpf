#include "sparse_power_runtime.hpp"

#include <ostream>

namespace mpf::detail {

void emit_cpp_sparse_power_runtime(std::ostream& output) {
  output << R"MPF(inline void validate_sparse_power_plan(
    const std::array<std::size_t, 2U>& input_shape,
    const std::array<std::size_t, 2U>& result_shape,
    const std::int64_t exponent_policy, const std::int64_t input_storage,
    const std::int64_t result_storage) {
  (void)sparse_product_size(input_shape, "power input");
  (void)sparse_product_size(result_shape, "power result");
  if (exponent_policy != 2 || input_storage != 3 || result_storage != 3 ||
      input_shape[0] != input_shape[1] || result_shape != input_shape)
    throw std::invalid_argument("MPF Matlab sparse matrix power plan is inconsistent");
}
template <typename Exponent>
std::uint64_t sparse_power_exponent(const Exponent& value) {
  const auto numeric = static_cast<double>(value);
  constexpr double maximum_safe_integer = 9007199254740991.0;
  if (!std::isfinite(numeric) || numeric < 0.0 || numeric > maximum_safe_integer ||
      std::trunc(numeric) != numeric)
    throw std::invalid_argument(
        "MPF Matlab sparse matrix power requires a nonnegative safe integer exponent");
  return static_cast<std::uint64_t>(numeric);
}
template <typename Value>
sparse_matrix<double> sparse_power_double(const sparse_matrix<Value>& input) {
  sparse_matrix<double> result;
  result.rows = input.rows;
  result.columns = input.columns;
  result.column_pointers = input.column_pointers;
  result.row_indices = input.row_indices;
  result.values.reserve(input.values.size());
  for (const auto& value : input.values) result.values.push_back(static_cast<double>(value));
  validate_sparse_csc(result, "matrix power base");
  return result;
}
inline sparse_matrix<double> sparse_power_identity(const std::size_t size) {
  sparse_matrix<double> result;
  result.rows = size;
  result.columns = size;
  result.column_pointers.reserve(size + 1U);
  result.row_indices.reserve(size);
  result.values.reserve(size);
  result.column_pointers.push_back(0U);
  for (std::size_t column = 0U; column < size; ++column) {
    result.row_indices.push_back(column);
    result.values.push_back(1.0);
    result.column_pointers.push_back(result.values.size());
  }
  validate_sparse_csc(result, "matrix power identity");
  return result;
}
template <typename Value, typename Exponent>
sparse_matrix<double> sparse_mpower(
    const sparse_matrix<Value>& value, const Exponent& exponent,
    const std::array<std::size_t, 2U>& input_shape,
    const std::array<std::size_t, 2U>& result_shape,
    const std::int64_t exponent_policy, const std::int64_t input_storage,
    const std::int64_t result_storage) {
  validate_sparse_power_plan(
      input_shape, result_shape, exponent_policy, input_storage, result_storage);
  validate_sparse_csc(value, "matrix power base");
  if (value.rows != input_shape[0] || value.columns != input_shape[1])
    throw std::invalid_argument(
        "MPF Matlab sparse matrix power base disagrees with its shape plan");
  auto remaining = sparse_power_exponent(exponent);
  if (remaining == 0U) return sparse_power_identity(value.rows);
  auto factor = sparse_power_double(value);
  sparse_matrix<double> result;
  bool has_result = false;
  while (remaining != 0U) {
    if ((remaining & 1U) != 0U) {
      if (has_result) {
        result = sparse_sparse_mtimes(result, factor, result_shape, input_shape, result_shape);
      } else {
        result = factor;
        has_result = true;
      }
    }
    remaining >>= 1U;
    if (remaining != 0U) {
      factor = sparse_sparse_mtimes(factor, factor, input_shape, input_shape, input_shape);
    }
  }
  return result;
}
)MPF";
}

}  // namespace mpf::detail
