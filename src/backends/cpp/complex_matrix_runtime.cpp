#include "complex_matrix_runtime.hpp"

#include <ostream>

namespace mpf::detail {

void emit_cpp_complex_matrix_runtime(std::ostream& output) {
  output << R"MPF(using matlab_complex_matrix = std::vector<std::vector<std::complex<double>>>;
template <typename T> matlab_complex_matrix matlab_complex_dense_matrix(
    const std::vector<std::vector<T>>& values, const std::string& name,
    const bool require_finite = true) {
  if (values.empty() || values.front().empty()) {
    throw std::invalid_argument("MPF Matlab " + name + " must be a non-empty rank-2 matrix");
  }
  const auto columns = values.front().size();
  matlab_complex_matrix result;
  result.reserve(values.size());
  for (const auto& row : values) {
    if (row.size() != columns) {
      throw std::invalid_argument("MPF Matlab " + name + " must be rectangular");
    }
    std::vector<std::complex<double>> converted;
    converted.reserve(columns);
    for (const auto& value : row) {
      const auto numeric = as_complex(value);
      if (require_finite && (!std::isfinite(numeric.real()) || !std::isfinite(numeric.imag()))) {
        throw std::invalid_argument("MPF Matlab " + name + " requires finite numeric values");
      }
      converted.push_back(numeric);
    }
    result.push_back(std::move(converted));
  }
  return result;
}
template <typename T> matlab_complex_matrix matlab_complex_dense_matrix(
    const std::vector<T>& values, const std::string& name,
    const bool require_finite = true) {
  if (values.empty()) {
    throw std::invalid_argument("MPF Matlab " + name + " must be a non-empty row matrix");
  }
  std::vector<std::complex<double>> row;
  row.reserve(values.size());
  for (const auto& value : values) {
    const auto numeric = as_complex(value);
    if (require_finite && (!std::isfinite(numeric.real()) || !std::isfinite(numeric.imag()))) {
      throw std::invalid_argument("MPF Matlab " + name + " requires finite numeric values");
    }
    row.push_back(numeric);
  }
  return {std::move(row)};
}
template <typename Left, typename Right> matlab_complex_matrix matlab_complex_mtimes(
    const std::vector<std::vector<Left>>& left_value,
    const std::vector<std::vector<Right>>& right_value) {
  const auto left = matlab_complex_dense_matrix(left_value, "left matrix operand", false);
  const auto right = matlab_complex_dense_matrix(right_value, "right matrix operand", false);
  const auto inner_extent = left.front().size();
  const auto columns = right.front().size();
  if (inner_extent != right.size()) {
    throw std::invalid_argument("MPF Matlab complex matrix multiplication shape mismatch");
  }
  matlab_complex_matrix result(
      left.size(), std::vector<std::complex<double>>(columns, {0.0, 0.0}));
  for (std::size_t row = 0; row < left.size(); ++row) {
    for (std::size_t column = 0; column < columns; ++column) {
      for (std::size_t inner = 0; inner < inner_extent; ++inner) {
        result[row][column] = complex_add(
            result[row][column], complex_multiply(left[row][inner], right[inner][column]));
      }
    }
  }
  return result;
}
inline matlab_complex_matrix matlab_complex_identity(const std::size_t size) {
  matlab_complex_matrix result(
      size, std::vector<std::complex<double>>(size, {0.0, 0.0}));
  for (std::size_t index = 0; index < size; ++index) result[index][index] = {1.0, 0.0};
  return result;
}
inline matlab_complex_matrix matlab_complex_ctranspose(const matlab_complex_matrix& values) {
  matlab_complex_matrix result(
      values.front().size(), std::vector<std::complex<double>>(values.size()));
  for (std::size_t row = 0; row < values.size(); ++row) {
    for (std::size_t column = 0; column < values.front().size(); ++column) {
      result[column][row] = std::conj(values[row][column]);
    }
  }
  return result;
}
inline double matlab_complex_matrix_one_norm(const matlab_complex_matrix& values) {
  double maximum = 0.0;
  for (std::size_t column = 0; column < values.front().size(); ++column) {
    double sum = 0.0;
    for (const auto& row : values) sum += std::abs(row[column]);
    maximum = std::max(maximum, sum);
  }
  return maximum;
}
struct matlab_complex_lu_factorization {
  matlab_complex_matrix lu;
  std::vector<std::size_t> swaps;
  bool singular{false};
};
inline matlab_complex_lu_factorization matlab_complex_lu_factor(
    const matlab_complex_matrix& coefficients) {
  const auto size = coefficients.size();
  matlab_complex_lu_factorization factor{coefficients, std::vector<std::size_t>(size), false};
  for (std::size_t pivot = 0; pivot < size; ++pivot) {
    auto selected = pivot;
    for (std::size_t row = pivot + 1; row < size; ++row) {
      if (std::abs(factor.lu[row][pivot]) > std::abs(factor.lu[selected][pivot])) selected = row;
    }
    factor.swaps[pivot] = selected;
    if (selected != pivot) std::swap(factor.lu[pivot], factor.lu[selected]);
    const auto pivot_magnitude = std::abs(factor.lu[pivot][pivot]);
    if (pivot_magnitude == 0.0 || !std::isfinite(pivot_magnitude)) {
      factor.singular = true;
      continue;
    }
    for (std::size_t row = pivot + 1; row < size; ++row) {
      factor.lu[row][pivot] = complex_divide(factor.lu[row][pivot], factor.lu[pivot][pivot]);
      for (std::size_t column = pivot + 1; column < size; ++column) {
        factor.lu[row][column] = complex_subtract(
            factor.lu[row][column],
            complex_multiply(factor.lu[row][pivot], factor.lu[pivot][column]));
      }
    }
  }
  return factor;
}
inline matlab_complex_matrix matlab_complex_lu_apply(
    const matlab_complex_lu_factorization& factor, matlab_complex_matrix values) {
  const auto size = factor.lu.size();
  const auto columns = values.front().size();
  for (std::size_t pivot = 0; pivot < size; ++pivot) {
    if (factor.swaps[pivot] != pivot) std::swap(values[pivot], values[factor.swaps[pivot]]);
    for (std::size_t row = pivot + 1; row < size; ++row) {
      for (std::size_t column = 0; column < columns; ++column) {
        values[row][column] = complex_subtract(
            values[row][column],
            complex_multiply(factor.lu[row][pivot], values[pivot][column]));
      }
    }
  }
  for (std::size_t reverse = size; reverse > 0; --reverse) {
    const auto row = reverse - 1;
    for (std::size_t column = 0; column < columns; ++column) {
      auto value = values[row][column];
      for (std::size_t inner = row + 1; inner < size; ++inner) {
        value = complex_subtract(
            value, complex_multiply(factor.lu[row][inner], values[inner][column]));
      }
      values[row][column] = complex_divide(value, factor.lu[row][row]);
    }
  }
  return values;
}
inline double matlab_complex_lu_rcond(
    const matlab_complex_matrix& coefficients,
    const matlab_complex_lu_factorization& factor) {
  if (factor.singular) return 0.0;
  const auto inverse = matlab_complex_lu_apply(
      factor, matlab_complex_identity(coefficients.size()));
  const auto denominator = matlab_complex_matrix_one_norm(coefficients) *
                           matlab_complex_matrix_one_norm(inverse);
  return denominator == 0.0 || !std::isfinite(denominator) ? 0.0 : 1.0 / denominator;
}
inline bool matlab_is_hermitian(const matlab_complex_matrix& values) {
  for (std::size_t row = 0; row < values.size(); ++row) {
    if (values[row][row].imag() != 0.0) return false;
    for (std::size_t column = 0; column < row; ++column) {
      if (values[row][column] != std::conj(values[column][row])) return false;
    }
  }
  return true;
}
struct matlab_complex_cholesky_factorization {
  matlab_complex_matrix lower;
  bool positive_definite{true};
};
inline matlab_complex_cholesky_factorization matlab_complex_cholesky_factor(
    const matlab_complex_matrix& coefficients) {
  const auto size = coefficients.size();
  matlab_complex_cholesky_factorization factor{
      matlab_complex_matrix(
          size, std::vector<std::complex<double>>(size, {0.0, 0.0})),
      true};
  for (std::size_t row = 0; row < size; ++row) {
    for (std::size_t column = 0; column <= row; ++column) {
      auto value = coefficients[row][column];
      for (std::size_t inner = 0; inner < column; ++inner) {
        value = complex_subtract(
            value, complex_multiply(factor.lower[row][inner],
                                    std::conj(factor.lower[column][inner])));
      }
      if (row == column) {
        const auto tolerance = std::numeric_limits<double>::epsilon() *
                               std::max(1.0, std::abs(value.real()));
        if (!(value.real() > 0.0) || std::abs(value.imag()) > tolerance ||
            !std::isfinite(value.real())) {
          factor.positive_definite = false;
          return factor;
        }
        factor.lower[row][column] = {std::sqrt(value.real()), 0.0};
      } else {
        factor.lower[row][column] = complex_divide(value, factor.lower[column][column]);
      }
    }
  }
  return factor;
}
inline matlab_complex_matrix matlab_complex_cholesky_apply(
    const matlab_complex_cholesky_factorization& factor, matlab_complex_matrix values) {
  const auto size = factor.lower.size();
  const auto columns = values.front().size();
  for (std::size_t row = 0; row < size; ++row) {
    for (std::size_t column = 0; column < columns; ++column) {
      auto value = values[row][column];
      for (std::size_t inner = 0; inner < row; ++inner) {
        value = complex_subtract(
            value, complex_multiply(factor.lower[row][inner], values[inner][column]));
      }
      values[row][column] = complex_divide(value, factor.lower[row][row]);
    }
  }
  for (std::size_t reverse = size; reverse > 0; --reverse) {
    const auto row = reverse - 1;
    for (std::size_t column = 0; column < columns; ++column) {
      auto value = values[row][column];
      for (std::size_t inner = row + 1; inner < size; ++inner) {
        value = complex_subtract(
            value, complex_multiply(std::conj(factor.lower[inner][row]), values[inner][column]));
      }
      values[row][column] = complex_divide(value, std::conj(factor.lower[row][row]));
    }
  }
  return values;
}
inline double matlab_complex_cholesky_rcond(
    const matlab_complex_matrix& coefficients,
    const matlab_complex_cholesky_factorization& factor) {
  const auto inverse = matlab_complex_cholesky_apply(
      factor, matlab_complex_identity(coefficients.size()));
  const auto denominator = matlab_complex_matrix_one_norm(coefficients) *
                           matlab_complex_matrix_one_norm(inverse);
  return denominator == 0.0 || !std::isfinite(denominator) ? 0.0 : 1.0 / denominator;
}
inline matlab_complex_matrix matlab_structured_complex_square_solve(
    const matlab_complex_matrix& left, const matlab_complex_matrix& right) {
  if (matlab_is_hermitian(left)) {
    const auto factor = matlab_complex_cholesky_factor(left);
    if (factor.positive_definite) {
      matlab_warn_square_condition(matlab_complex_cholesky_rcond(left, factor));
      return matlab_complex_cholesky_apply(factor, right);
    }
  }
  const auto factor = matlab_complex_lu_factor(left);
  matlab_warn_square_condition(matlab_complex_lu_rcond(left, factor));
  return matlab_complex_lu_apply(factor, right);
}
inline matlab_complex_matrix matlab_complex_square_solve(
    const matlab_complex_matrix& left, const matlab_complex_matrix& right) {
  if (left.size() != left.front().size() || right.size() != left.size()) {
    throw std::invalid_argument(
        "MPF Matlab complex matrix solve disagrees with its static square plan");
  }
  return matlab_structured_complex_square_solve(left, right);
}
template <typename Left, typename Right> matlab_complex_matrix
matlab_mldivide_structured_complex_square(
    const std::vector<std::vector<Left>>& left,
    const std::vector<std::vector<Right>>& right) {
  return matlab_complex_square_solve(
      matlab_complex_dense_matrix(left, "complex coefficient matrix"),
      matlab_complex_dense_matrix(right, "complex right-hand side"));
}
template <typename Left, typename Right> matlab_complex_matrix
matlab_mrdivide_structured_complex_square(const Left& left_value, const Right& right_value) {
  const auto left = matlab_complex_dense_matrix(left_value, "complex left operand");
  const auto right = matlab_complex_dense_matrix(right_value, "complex right divisor");
  return matlab_complex_ctranspose(matlab_complex_square_solve(
      matlab_complex_ctranspose(right), matlab_complex_ctranspose(left)));
}
template <typename Value, typename Exponent> matlab_complex_matrix matlab_complex_mpower(
    const std::vector<std::vector<Value>>& values, const Exponent exponent_value) {
  auto factor = matlab_complex_dense_matrix(values, "complex matrix power base");
  if (factor.size() != factor.front().size()) {
    throw std::invalid_argument("MPF Matlab complex matrix power requires a square matrix");
  }
  const auto numeric_exponent = static_cast<double>(exponent_value);
  constexpr double maximum_safe_integer = 9007199254740991.0;
  if (!std::isfinite(numeric_exponent) || std::trunc(numeric_exponent) != numeric_exponent ||
      std::fabs(numeric_exponent) > maximum_safe_integer) {
    throw std::invalid_argument(
        "MPF Matlab complex matrix power requires a safe integer exponent");
  }
  auto exponent = static_cast<std::int64_t>(numeric_exponent);
  auto result = matlab_complex_identity(factor.size());
  if (exponent < 0) {
    factor = matlab_structured_complex_square_solve(factor, result);
    exponent = -exponent;
  }
  auto remaining = static_cast<std::uint64_t>(exponent);
  while (remaining > 0) {
    if ((remaining & 1U) != 0U) result = matlab_complex_mtimes(result, factor);
    remaining >>= 1U;
    if (remaining > 0) factor = matlab_complex_mtimes(factor, factor);
  }
  return result;
}
)MPF";
}

}  // namespace mpf::detail
