#include "sparse_matrix_runtime.hpp"

#include <ostream>

namespace mpf::detail {

void emit_cpp_sparse_matrix_runtime(std::ostream& output) {
  output << R"MPF(template <typename T> struct sparse_matrix {
  std::size_t rows{};
  std::size_t columns{};
  std::vector<std::size_t> column_pointers;
  std::vector<std::size_t> row_indices;
  std::vector<T> values;
};
template <typename T> void validate_sparse_csc(
    const sparse_matrix<T>& matrix, const std::string& name = "sparse matrix") {
  if (matrix.column_pointers.size() != matrix.columns + 1U ||
      matrix.row_indices.size() != matrix.values.size() || matrix.column_pointers.empty() ||
      matrix.column_pointers.front() != 0U ||
      matrix.column_pointers.back() != matrix.values.size())
    throw std::invalid_argument("MPF Matlab " + name + " has an invalid CSC structure");
  for (std::size_t column = 0; column < matrix.columns; ++column) {
    const auto begin = matrix.column_pointers[column];
    const auto end = matrix.column_pointers[column + 1U];
    if (begin > end || end > matrix.values.size())
      throw std::invalid_argument("MPF Matlab " + name + " has invalid CSC pointers");
    std::size_t previous = 0U; bool has_previous = false;
    for (auto index = begin; index < end; ++index) {
      const auto row = matrix.row_indices[index];
      const auto value = static_cast<double>(matrix.values[index]);
      if (row >= matrix.rows || (has_previous && row <= previous) || !std::isfinite(value) ||
          value == 0.0)
        throw std::invalid_argument("MPF Matlab " + name +
                                    " is not canonical finite-real CSC");
      previous = row; has_previous = true;
    }
  }
}
template <typename T> sparse_matrix<double> sparse(
    const std::vector<std::vector<T>>& values) {
  const auto dense = matlab_dense_matrix(values, "sparse input");
  sparse_matrix<double> result;
  result.rows = dense.size(); result.columns = dense.front().size();
  result.column_pointers.push_back(0U);
  for (std::size_t column = 0; column < result.columns; ++column) {
    for (std::size_t row = 0; row < result.rows; ++row) {
      if (dense[row][column] == 0.0) continue;
      result.row_indices.push_back(row); result.values.push_back(dense[row][column]);
    }
    result.column_pointers.push_back(result.values.size());
  }
  return result;
}
template <typename T> sparse_matrix<double> sparse(const std::vector<T>& values) {
  return sparse(std::vector<std::vector<T>>{values});
}
template <typename T> sparse_matrix<T> sparse(const sparse_matrix<T>& matrix) {
  validate_sparse_csc(matrix); return matrix;
}
template <typename T> std::vector<std::vector<double>> full(const sparse_matrix<T>& value) {
  validate_sparse_csc(value, "full input");
  const auto& matrix = value;
  std::vector<std::vector<double>> result(matrix.rows, std::vector<double>(matrix.columns));
  for (std::size_t column = 0; column < matrix.columns; ++column)
    for (auto index = matrix.column_pointers[column];
         index < matrix.column_pointers[column + 1U]; ++index)
      result[matrix.row_indices[index]][column] = static_cast<double>(matrix.values[index]);
  return result;
}
template <typename T> std::vector<std::vector<T>> full(
    const std::vector<std::vector<T>>& values) { return values; }
template <typename T> std::vector<T> full(const std::vector<T>& values) { return values; }
template <typename T> constexpr bool issparse(const T&) noexcept { return false; }
template <typename T> constexpr bool issparse(const sparse_matrix<T>&) noexcept { return true; }
template <typename T> std::size_t nnz(const sparse_matrix<T>& matrix) {
  validate_sparse_csc(matrix);
  return matrix.values.size();
}
template <typename T> std::size_t nnz(const T& value) {
  if constexpr (is_vector<std::decay_t<T>>::value) {
    std::size_t count = 0U;
    for (const auto& item : value) count += nnz(item);
    return count;
  } else {
    static_assert(std::is_arithmetic_v<std::decay_t<T>>,
                  "MPF Matlab nnz requires real or logical values");
    const auto numeric = static_cast<double>(value);
    if (!std::isfinite(numeric))
      throw std::invalid_argument("MPF Matlab nnz requires finite real values");
    return numeric != 0.0 ? 1U : 0U;
  }
}
template <typename T> sparse_matrix<double> sparse_transpose(const sparse_matrix<T>& value) {
  validate_sparse_csc(value, "transpose operand");
  const auto& matrix = value;
  sparse_matrix<double> result;
  result.rows = matrix.columns; result.columns = matrix.rows;
  result.column_pointers.assign(result.columns + 1U, 0U);
  for (const auto row : matrix.row_indices) ++result.column_pointers[row + 1U];
  for (std::size_t column = 0; column < result.columns; ++column)
    result.column_pointers[column + 1U] += result.column_pointers[column];
  auto next = result.column_pointers;
  result.row_indices.resize(matrix.values.size()); result.values.resize(matrix.values.size());
  for (std::size_t column = 0; column < matrix.columns; ++column)
    for (auto index = matrix.column_pointers[column];
         index < matrix.column_pointers[column + 1U]; ++index) {
      const auto target = next[matrix.row_indices[index]]++;
      result.row_indices[target] = column;
      result.values[target] = static_cast<double>(matrix.values[index]);
    }
  return result;
}
template <typename T> std::ostream& operator<<(std::ostream& output,
                                               const sparse_matrix<T>& matrix) {
  validate_sparse_csc(matrix);
  return output << "sparse(" << matrix.rows << "x" << matrix.columns << ", nnz="
                << matrix.values.size() << ')';
}
struct sparse_tridiagonal_factorization {
  std::vector<double> lower;
  std::vector<double> diagonal;
  std::vector<double> upper;
  bool singular{false};
};
template <typename T> bool sparse_is_tridiagonal(const sparse_matrix<T>& matrix) {
  for (std::size_t column = 0; column < matrix.columns; ++column)
    for (auto index = matrix.column_pointers[column];
         index < matrix.column_pointers[column + 1U]; ++index) {
      const auto row = matrix.row_indices[index];
      if ((row > column ? row - column : column - row) > 1U) return false;
    }
  return true;
}
template <typename T> std::optional<sparse_tridiagonal_factorization>
sparse_tridiagonal_factor(const sparse_matrix<T>& matrix) {
  const auto size = matrix.rows;
  sparse_tridiagonal_factorization factor;
  factor.lower.assign(size > 0U ? size - 1U : 0U, 0.0);
  factor.diagonal.assign(size, 0.0);
  factor.upper.assign(size > 0U ? size - 1U : 0U, 0.0);
  for (std::size_t column = 0; column < size; ++column)
    for (auto index = matrix.column_pointers[column];
         index < matrix.column_pointers[column + 1U]; ++index) {
      const auto row = matrix.row_indices[index];
      const auto value = static_cast<double>(matrix.values[index]);
      if (row == column) factor.diagonal[column] = value;
      else if (row == column + 1U) factor.lower[column] = value;
      else if (column == row + 1U) factor.upper[row] = value;
    }
  for (std::size_t row = 1; row < size; ++row) {
    if (factor.diagonal[row - 1U] == 0.0 || !std::isfinite(factor.diagonal[row - 1U]))
      return std::nullopt;
    factor.lower[row - 1U] /= factor.diagonal[row - 1U];
    factor.diagonal[row] -= factor.lower[row - 1U] * factor.upper[row - 1U];
  }
  if (size != 0U && (factor.diagonal.back() == 0.0 || !std::isfinite(factor.diagonal.back())))
    return std::nullopt;
  return factor;
}
inline std::vector<std::vector<double>> sparse_tridiagonal_apply(
    const sparse_tridiagonal_factorization& factor, std::vector<std::vector<double>> values) {
  const auto size = factor.diagonal.size();
  for (std::size_t row = 1; row < size; ++row)
    for (std::size_t column = 0; column < values.front().size(); ++column)
      values[row][column] -= factor.lower[row - 1U] * values[row - 1U][column];
  for (auto reverse = size; reverse > 0U; --reverse) {
    const auto row = reverse - 1U;
    for (std::size_t column = 0; column < values.front().size(); ++column) {
      if (row + 1U < size) values[row][column] -= factor.upper[row] * values[row + 1U][column];
      values[row][column] /= factor.diagonal[row];
    }
  }
  return values;
}
inline std::vector<std::vector<double>> sparse_tridiagonal_apply_transpose(
    const sparse_tridiagonal_factorization& factor, std::vector<std::vector<double>> values) {
  const auto size = factor.diagonal.size();
  for (std::size_t row = 0; row < size; ++row)
    for (std::size_t column = 0; column < values.front().size(); ++column) {
      if (row != 0U) values[row][column] -= factor.upper[row - 1U] * values[row - 1U][column];
      values[row][column] /= factor.diagonal[row];
    }
  for (auto reverse = size; reverse > 1U; --reverse) {
    const auto row = reverse - 2U;
    for (std::size_t column = 0; column < values.front().size(); ++column)
      values[row][column] -= factor.lower[row] * values[row + 1U][column];
  }
  return values;
}
using sparse_row = std::vector<std::pair<std::size_t, double>>;
inline double sparse_row_get(const sparse_row& row, const std::size_t column) {
  const auto found = std::lower_bound(row.begin(), row.end(), column,
      [](const auto& item, const std::size_t wanted) { return item.first < wanted; });
  return found != row.end() && found->first == column ? found->second : 0.0;
}
inline void sparse_row_set(sparse_row& row, const std::size_t column, const double value) {
  const auto found = std::lower_bound(row.begin(), row.end(), column,
      [](const auto& item, const std::size_t wanted) { return item.first < wanted; });
  if (found != row.end() && found->first == column) {
    if (value == 0.0) row.erase(found); else found->second = value;
  } else if (value != 0.0) row.insert(found, {column, value});
}
struct sparse_row_lu_factorization {
  std::vector<sparse_row> rows;
  std::vector<std::size_t> swaps;
  bool singular{false};
};
template <typename T> sparse_row_lu_factorization sparse_row_lu_factor(
    const sparse_matrix<T>& matrix) {
  const auto size = matrix.rows;
  sparse_row_lu_factorization factor{std::vector<sparse_row>(size),
                                      std::vector<std::size_t>(size), false};
  for (std::size_t column = 0; column < size; ++column)
    for (auto index = matrix.column_pointers[column];
         index < matrix.column_pointers[column + 1U]; ++index)
      factor.rows[matrix.row_indices[index]].push_back(
          {column, static_cast<double>(matrix.values[index])});
  for (std::size_t pivot = 0; pivot < size; ++pivot) {
    auto selected = pivot; auto magnitude = std::fabs(sparse_row_get(factor.rows[pivot], pivot));
    for (std::size_t row = pivot + 1U; row < size; ++row) {
      const auto candidate = std::fabs(sparse_row_get(factor.rows[row], pivot));
      if (candidate > magnitude) { selected = row; magnitude = candidate; }
    }
    factor.swaps[pivot] = selected;
    if (selected != pivot) std::swap(factor.rows[pivot], factor.rows[selected]);
    const auto diagonal = sparse_row_get(factor.rows[pivot], pivot);
    if (diagonal == 0.0 || !std::isfinite(diagonal)) { factor.singular = true; continue; }
    sparse_row pivot_entries;
    for (const auto& entry : factor.rows[pivot]) if (entry.first > pivot)
      pivot_entries.push_back(entry);
    for (std::size_t row = pivot + 1U; row < size; ++row) {
      const auto below = sparse_row_get(factor.rows[row], pivot);
      if (below == 0.0) continue;
      const auto multiplier = below / diagonal;
      sparse_row_set(factor.rows[row], pivot, multiplier);
      for (const auto& entry : pivot_entries)
        sparse_row_set(factor.rows[row], entry.first,
                       sparse_row_get(factor.rows[row], entry.first) -
                           multiplier * entry.second);
    }
  }
  return factor;
}
inline std::vector<std::vector<double>> sparse_row_lu_apply(
    const sparse_row_lu_factorization& factor, std::vector<std::vector<double>> values) {
  const auto size = factor.rows.size();
  for (std::size_t pivot = 0; pivot < size; ++pivot)
    if (factor.swaps[pivot] != pivot) std::swap(values[pivot], values[factor.swaps[pivot]]);
  for (std::size_t row = 0; row < size; ++row)
    for (const auto& entry : factor.rows[row]) {
      if (entry.first >= row) break;
      for (std::size_t rhs = 0; rhs < values.front().size(); ++rhs)
        values[row][rhs] -= entry.second * values[entry.first][rhs];
    }
  for (auto reverse = size; reverse > 0U; --reverse) {
    const auto row = reverse - 1U;
    for (const auto& entry : factor.rows[row]) if (entry.first > row)
      for (std::size_t rhs = 0; rhs < values.front().size(); ++rhs)
        values[row][rhs] -= entry.second * values[entry.first][rhs];
    const auto diagonal = sparse_row_get(factor.rows[row], row);
    for (auto& value : values[row]) value /= diagonal;
  }
  return values;
}
inline std::vector<std::vector<double>> sparse_row_lu_apply_transpose(
    const sparse_row_lu_factorization& factor, std::vector<std::vector<double>> values) {
  const auto size = factor.rows.size();
  for (std::size_t row = 0; row < size; ++row) {
    for (std::size_t inner = 0; inner < row; ++inner) {
      const auto coefficient = sparse_row_get(factor.rows[inner], row);
      if (coefficient != 0.0) for (std::size_t rhs = 0; rhs < values.front().size(); ++rhs)
        values[row][rhs] -= coefficient * values[inner][rhs];
    }
    const auto diagonal = sparse_row_get(factor.rows[row], row);
    for (auto& value : values[row]) value /= diagonal;
  }
  for (auto reverse = size; reverse > 0U; --reverse) {
    const auto row = reverse - 1U;
    for (std::size_t inner = row + 1U; inner < size; ++inner) {
      const auto coefficient = sparse_row_get(factor.rows[inner], row);
      if (coefficient != 0.0) for (std::size_t rhs = 0; rhs < values.front().size(); ++rhs)
        values[row][rhs] -= coefficient * values[inner][rhs];
    }
  }
  for (auto reverse = size; reverse > 0U; --reverse) {
    const auto pivot = reverse - 1U; const auto selected = factor.swaps[pivot];
    if (selected != pivot) std::swap(values[pivot], values[selected]);
  }
  return values;
}
template <typename T> double sparse_one_norm(const sparse_matrix<T>& matrix) {
  double maximum = 0.0;
  for (std::size_t column = 0; column < matrix.columns; ++column) {
    double sum = 0.0;
    for (auto index = matrix.column_pointers[column];
         index < matrix.column_pointers[column + 1U]; ++index)
      sum += std::fabs(static_cast<double>(matrix.values[index]));
    maximum = std::max(maximum, sum);
  }
  return maximum;
}
template <typename Matrix, typename Factor, typename Apply, typename ApplyTranspose>
double sparse_rcond(const Matrix& matrix, const Factor& factor, Apply apply,
                    ApplyTranspose apply_transpose) {
  if (factor.singular) return 0.0;
  const auto norm = sparse_one_norm(matrix); const auto size = matrix.rows;
  if (norm == 0.0 || size == 0U || !std::isfinite(norm)) return 0.0;
  std::vector<std::vector<double>> vector(size, std::vector<double>(1U, 1.0 / size));
  double inverse_norm = 0.0;
  for (std::size_t iteration = 0; iteration < 5U; ++iteration) {
    const auto solved = apply(factor, vector); double estimate = 0.0;
    for (const auto& row : solved) estimate += std::fabs(row.front());
    if (!std::isfinite(estimate)) return 0.0;
    inverse_norm = std::max(inverse_norm, estimate); auto signs = solved;
    for (auto& row : signs) row.front() = row.front() >= 0.0 ? 1.0 : -1.0;
    const auto transposed = apply_transpose(factor, std::move(signs));
    auto selected = std::size_t{0};
    for (std::size_t row = 0; row < size; ++row) {
      if (!std::isfinite(transposed[row].front())) return 0.0;
      if (std::fabs(transposed[row].front()) > std::fabs(transposed[selected].front()))
        selected = row;
    }
    double dual = 0.0;
    for (std::size_t row = 0; row < size; ++row)
      dual += transposed[row].front() * vector[row].front();
    if (std::fabs(transposed[selected].front()) <= dual) break;
    vector.assign(size, std::vector<double>(1U)); vector[selected].front() = 1.0;
  }
  const auto product = norm * inverse_norm;
  return product > 0.0 && std::isfinite(product) ? std::min(1.0, 1.0 / product) : 0.0;
}
template <typename T> std::vector<std::vector<double>> sparse_square_solve_dense(
    const sparse_matrix<T>& coefficients,
    const std::vector<std::vector<double>>& right_hand_side) {
  validate_sparse_csc(coefficients, "sparse coefficient matrix");
  const auto& matrix = coefficients;
  if (matrix.rows == 0U || matrix.rows != matrix.columns || right_hand_side.size() != matrix.rows ||
      right_hand_side.empty() || right_hand_side.front().empty())
    throw std::invalid_argument("MPF Matlab sparse solve shape mismatch");
  if (sparse_is_tridiagonal(matrix)) {
    const auto factor = sparse_tridiagonal_factor(matrix);
    if (factor.has_value()) {
      matlab_warn_square_condition(sparse_rcond(
          matrix, *factor,
          [](const auto& value, auto values) {
            return sparse_tridiagonal_apply(value, std::move(values));
          },
          [](const auto& value, auto values) {
            return sparse_tridiagonal_apply_transpose(value, std::move(values));
          }));
      return sparse_tridiagonal_apply(*factor, right_hand_side);
    }
  }
  const auto factor = sparse_row_lu_factor(matrix);
  matlab_warn_square_condition(sparse_rcond(
      matrix, factor,
      [](const auto& value, auto values) { return sparse_row_lu_apply(value, std::move(values)); },
      [](const auto& value, auto values) {
        return sparse_row_lu_apply_transpose(value, std::move(values));
      }));
  return sparse_row_lu_apply(factor, right_hand_side);
}
template <typename Coefficient, typename Right>
std::vector<std::vector<double>> matlab_mldivide_sparse_real_square(
    const sparse_matrix<Coefficient>& coefficients,
    const std::vector<std::vector<Right>>& right_hand_side) {
  return sparse_square_solve_dense(coefficients,
      matlab_dense_matrix(right_hand_side, "right-hand side"));
}
template <typename Coefficient, typename Right>
sparse_matrix<double> matlab_mldivide_sparse_real_square(
    const sparse_matrix<Coefficient>& coefficients, const sparse_matrix<Right>& right_hand_side) {
  return sparse(sparse_square_solve_dense(coefficients, full(right_hand_side)));
}
template <typename Left, typename Coefficient>
std::vector<std::vector<double>> matlab_mrdivide_sparse_real_square(
    const std::vector<std::vector<Left>>& left, const sparse_matrix<Coefficient>& coefficients) {
  return matlab_matrix_transpose(matlab_mldivide_sparse_real_square(
      sparse_transpose(coefficients),
      matlab_matrix_transpose(matlab_dense_matrix(left, "left operand"))));
}
template <typename Left, typename Coefficient>
sparse_matrix<double> matlab_mrdivide_sparse_real_square(
    const sparse_matrix<Left>& left, const sparse_matrix<Coefficient>& coefficients) {
  return sparse_transpose(matlab_mldivide_sparse_real_square(
      sparse_transpose(coefficients), sparse_transpose(left)));
}
)MPF";
}

}  // namespace mpf::detail
