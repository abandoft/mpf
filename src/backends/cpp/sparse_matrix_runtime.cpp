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
template <typename T> std::size_t sparse_dimension(const T& value, const std::string& name,
                                                   const bool allow_zero = false) {
  static_assert(std::is_arithmetic_v<std::decay_t<T>>,
                "MPF Matlab sparse dimensions must be numeric scalars");
  const auto numeric = static_cast<long double>(value);
  if (!std::isfinite(numeric) || std::trunc(numeric) != numeric ||
      (allow_zero ? numeric < 0.0L : numeric <= 0.0L) ||
      numeric > static_cast<long double>(std::numeric_limits<std::size_t>::max()) ||
      numeric > 9007199254740991.0L)
    throw std::invalid_argument("MPF Matlab " + name + " must be a " +
                                (allow_zero ? "nonnegative" : "positive") +
                                " safe integer");
  return static_cast<std::size_t>(numeric);
}
struct sparse_triplet_entry {
  std::size_t row{};
  std::size_t column{};
  double value{};
};
template <typename Argument, typename Values>
const auto& sparse_triplet_value_at(const Values& values, const std::size_t index) {
  if constexpr (is_vector<std::decay_t<Argument>>::value) return values.at(index);
  else return values.front();
}
template <typename RowArgument, typename ColumnArgument, typename ValueArgument>
sparse_matrix<double> sparse_from_triplets(
    const RowArgument& row_argument, const ColumnArgument& column_argument,
    const ValueArgument& value_argument, const std::optional<std::size_t> explicit_rows = {},
    const std::optional<std::size_t> explicit_columns = {},
    const std::optional<std::size_t> reserve_hint = {}) {
  const auto row_values = flatten_selector_column_major(row_argument);
  const auto column_values = flatten_selector_column_major(column_argument);
  const auto stored_values = flatten_selector_column_major(value_argument);
  std::optional<std::size_t> sequence_count;
  const auto accept_count = [&](const bool sequence, const std::size_t count) {
    if (!sequence) return;
    if (sequence_count.has_value() && *sequence_count != count)
      throw std::invalid_argument(
          "MPF Matlab nonscalar sparse triplets must have equal element counts");
    sequence_count = count;
  };
  accept_count(is_vector<std::decay_t<RowArgument>>::value, row_values.size());
  accept_count(is_vector<std::decay_t<ColumnArgument>>::value, column_values.size());
  accept_count(is_vector<std::decay_t<ValueArgument>>::value, stored_values.size());
  const auto count = sequence_count.value_or(1U);
  std::vector<sparse_triplet_entry> entries;
  entries.reserve(count);
  std::size_t inferred_rows = 0U; std::size_t inferred_columns = 0U;
  for (std::size_t index = 0; index < count; ++index) {
    const auto row_numeric = static_cast<double>(
        sparse_triplet_value_at<RowArgument>(row_values, index));
    const auto column_numeric = static_cast<double>(
        sparse_triplet_value_at<ColumnArgument>(column_values, index));
    const auto stored_numeric = static_cast<double>(
        sparse_triplet_value_at<ValueArgument>(stored_values, index));
    if (!std::isfinite(row_numeric) || std::trunc(row_numeric) != row_numeric ||
        row_numeric <= 0.0 || row_numeric > 9007199254740991.0 ||
        !std::isfinite(column_numeric) || std::trunc(column_numeric) != column_numeric ||
        column_numeric <= 0.0 || column_numeric > 9007199254740991.0)
      throw std::invalid_argument(
          "MPF Matlab sparse triplet indices must be positive safe integers");
    if (!std::isfinite(stored_numeric))
      throw std::invalid_argument("MPF Matlab sparse stored values must be finite real values");
    const auto row = static_cast<std::size_t>(row_numeric);
    const auto column = static_cast<std::size_t>(column_numeric);
    inferred_rows = std::max(inferred_rows, row);
    inferred_columns = std::max(inferred_columns, column);
    entries.push_back({row - 1U, column - 1U, stored_numeric});
  }
  const auto rows = explicit_rows.has_value()
                        ? *explicit_rows
                        : sparse_dimension(inferred_rows, "inferred row extent");
  const auto columns = explicit_columns.has_value()
                           ? *explicit_columns
                           : sparse_dimension(inferred_columns, "inferred column extent");
  for (const auto& entry : entries)
    if (entry.row >= rows || entry.column >= columns)
      throw std::invalid_argument(
          "MPF Matlab sparse triplet index exceeds the requested dimensions");
  std::stable_sort(entries.begin(), entries.end(), [](const auto& left, const auto& right) {
    return left.column < right.column ||
           (left.column == right.column && left.row < right.row);
  });
  sparse_matrix<double> result;
  result.rows = rows; result.columns = columns;
  const auto capacity = std::max(count, reserve_hint.value_or(count));
  result.row_indices.reserve(capacity); result.values.reserve(capacity);
  result.column_pointers.reserve(columns + 1U); result.column_pointers.push_back(0U);
  std::size_t entry = 0U;
  for (std::size_t column = 0; column < columns; ++column) {
    while (entry < entries.size() && entries[entry].column == column) {
      const auto row = entries[entry].row; double sum = 0.0;
      while (entry < entries.size() && entries[entry].column == column &&
             entries[entry].row == row)
        sum += entries[entry++].value;
      if (!std::isfinite(sum))
        throw std::invalid_argument(
            "MPF Matlab sparse duplicate accumulation is not finite");
      if (sum != 0.0) { result.row_indices.push_back(row); result.values.push_back(sum); }
    }
    result.column_pointers.push_back(result.values.size());
  }
  validate_sparse_csc(result);
  return result;
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
template <typename Rows, typename Columns>
sparse_matrix<double> sparse(const Rows& rows_value, const Columns& columns_value) {
  const auto rows = sparse_dimension(rows_value, "row extent");
  const auto columns = sparse_dimension(columns_value, "column extent");
  sparse_matrix<double> result;
  result.rows = rows; result.columns = columns;
  result.column_pointers.assign(columns + 1U, 0U);
  return result;
}
template <typename Row, typename Column, typename Value>
sparse_matrix<double> sparse(const Row& rows, const Column& columns, const Value& values) {
  return sparse_from_triplets(rows, columns, values);
}
template <typename Row, typename Column, typename Value, typename Rows, typename Columns>
sparse_matrix<double> sparse(const Row& row_values, const Column& column_values,
                             const Value& values, const Rows& rows_value,
                             const Columns& columns_value) {
  return sparse_from_triplets(row_values, column_values, values,
                              sparse_dimension(rows_value, "row extent"),
                              sparse_dimension(columns_value, "column extent"));
}
template <typename Row, typename Column, typename Value, typename Rows, typename Columns,
          typename Reserve>
sparse_matrix<double> sparse(const Row& row_values, const Column& column_values,
                             const Value& values, const Rows& rows_value,
                             const Columns& columns_value, const Reserve& reserve_value) {
  return sparse_from_triplets(row_values, column_values, values,
                              sparse_dimension(rows_value, "row extent"),
                              sparse_dimension(columns_value, "column extent"),
                              sparse_dimension(reserve_value, "nzmax", true));
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
template <typename T> double sparse_value_at(const sparse_matrix<T>& matrix,
                                             const std::size_t row,
                                             const std::size_t column) {
  const auto first = matrix.row_indices.begin() +
                     static_cast<std::ptrdiff_t>(matrix.column_pointers[column]);
  const auto last = matrix.row_indices.begin() +
                    static_cast<std::ptrdiff_t>(matrix.column_pointers[column + 1U]);
  const auto found = std::lower_bound(first, last, row);
  if (found == last || *found != row) return 0.0;
  const auto offset = static_cast<std::size_t>(found - matrix.row_indices.begin());
  return static_cast<double>(matrix.values[offset]);
}
inline std::size_t sparse_element_count(const std::size_t rows, const std::size_t columns) {
  if (rows != 0U && columns > std::numeric_limits<std::size_t>::max() / rows)
    throw std::length_error("MPF Matlab sparse index extent exceeds size limits");
  return rows * columns;
}
inline std::pair<std::size_t, std::size_t> sparse_linear_result_shape(
    std::optional<std::size_t> rows, std::optional<std::size_t> columns,
    const std::size_t count) {
  if (!rows.has_value() && !columns.has_value())
    throw std::invalid_argument("MPF Matlab sparse linear result shape is underdetermined");
  if (!rows.has_value()) {
    if (*columns == 0U ? count != 0U : count % *columns != 0U)
      throw std::invalid_argument("MPF Matlab sparse linear result shape is inconsistent");
    rows = *columns == 0U ? 0U : count / *columns;
  }
  if (!columns.has_value()) {
    if (*rows == 0U ? count != 0U : count % *rows != 0U)
      throw std::invalid_argument("MPF Matlab sparse linear result shape is inconsistent");
    columns = *rows == 0U ? 0U : count / *rows;
  }
  if (sparse_element_count(*rows, *columns) != count)
    throw std::invalid_argument("MPF Matlab sparse linear result shape is inconsistent");
  return {*rows, *columns};
}
inline std::pair<std::size_t, std::size_t> sparse_submatrix_result_shape(
    const std::optional<std::size_t> planned_rows,
    const std::optional<std::size_t> planned_columns, const std::size_t rows,
    const std::size_t columns) {
  if ((planned_rows.has_value() && *planned_rows != rows) ||
      (planned_columns.has_value() && *planned_columns != columns))
    throw std::invalid_argument("MPF Matlab sparse submatrix result shape is inconsistent");
  return {rows, columns};
}
template <typename Selector> bool sparse_is_full_slice(const Selector&) { return false; }
inline bool sparse_is_full_slice(const slice_selector& selector) {
  return !selector.start.has_value() && !selector.stop.has_value() &&
         !selector.step.has_value() && selector.inclusive;
}
template <typename T, typename Selector> double sparse_linear_element(
    const sparse_matrix<T>& value, const Selector& selector, const std::size_t base = 1U) {
  validate_sparse_csc(value, "linear index operand");
  const auto size = sparse_element_count(value.rows, value.columns);
  const auto resolved = resolve_selector_extent(selector, size);
  const auto indices = selector_indices(size, resolved, base, false);
  if (indices.size() != 1U)
    throw std::invalid_argument(
        "MPF Matlab sparse scalar linear index selected multiple elements");
  const auto linear = indices.front();
  return sparse_value_at(value, linear % value.rows, linear / value.rows);
}
template <typename T, typename RowSelector, typename ColumnSelector>
double sparse_subscript_element(const sparse_matrix<T>& value,
                                const RowSelector& row_selector,
                                const ColumnSelector& column_selector,
                                const std::size_t base = 1U) {
  validate_sparse_csc(value, "subscript operand");
  const auto rows = selector_indices(
      value.rows, resolve_selector_extent(row_selector, value.rows), base, false);
  const auto columns = selector_indices(
      value.columns, resolve_selector_extent(column_selector, value.columns), base, false);
  if (rows.size() != 1U || columns.size() != 1U)
    throw std::invalid_argument("MPF Matlab sparse scalar subscript selected multiple elements");
  return sparse_value_at(value, rows.front(), columns.front());
}
template <typename T, typename Selector> sparse_matrix<double> sparse_linear_selection(
    const sparse_matrix<T>& value, const Selector& selector,
    const std::optional<std::size_t> planned_rows,
    const std::optional<std::size_t> planned_columns, const std::size_t base = 1U) {
  validate_sparse_csc(value, "linear selection operand");
  const auto size = sparse_element_count(value.rows, value.columns);
  const auto resolved = resolve_selector_extent(selector, size);
  if (sparse_is_full_slice(resolved)) {
    const auto [rows, columns] = sparse_linear_result_shape(planned_rows, planned_columns, size);
    if (rows != size || columns != 1U)
      throw std::invalid_argument("MPF Matlab sparse full-colon result shape is inconsistent");
    sparse_matrix<double> result;
    result.rows = rows; result.columns = columns;
    result.column_pointers = {0U, value.values.size()};
    result.row_indices.reserve(value.row_indices.size());
    result.values.reserve(value.values.size());
    for (std::size_t column = 0U; column < value.columns; ++column) {
      const auto offset = column * value.rows;
      for (auto stored = value.column_pointers[column];
           stored < value.column_pointers[column + 1U]; ++stored) {
        result.row_indices.push_back(offset + value.row_indices[stored]);
        result.values.push_back(static_cast<double>(value.values[stored]));
      }
    }
    validate_sparse_csc(result, "full-colon selection result");
    return result;
  }
  const auto indices = selector_indices(size, resolved, base, false);
  const auto [rows, columns] = sparse_linear_result_shape(planned_rows, planned_columns,
                                                          indices.size());
  sparse_matrix<double> result;
  result.rows = rows; result.columns = columns; result.column_pointers.push_back(0U);
  result.row_indices.reserve(indices.size()); result.values.reserve(indices.size());
  for (std::size_t column = 0U; column < columns; ++column) {
    for (std::size_t row = 0U; row < rows; ++row) {
      const auto linear = indices[row + column * rows];
      const auto stored = sparse_value_at(value, linear % value.rows, linear / value.rows);
      if (stored != 0.0) { result.row_indices.push_back(row); result.values.push_back(stored); }
    }
    result.column_pointers.push_back(result.values.size());
  }
  validate_sparse_csc(result, "linear selection result");
  return result;
}
template <typename T, typename RowSelector, typename ColumnSelector>
sparse_matrix<double> sparse_submatrix_selection(
    const sparse_matrix<T>& value, const RowSelector& row_selector,
    const ColumnSelector& column_selector, const std::optional<std::size_t> planned_rows,
    const std::optional<std::size_t> planned_columns, const std::size_t base = 1U) {
  validate_sparse_csc(value, "submatrix operand");
  const auto selected_rows = selector_indices(
      value.rows, resolve_selector_extent(row_selector, value.rows), base, false);
  const auto selected_columns = selector_indices(
      value.columns, resolve_selector_extent(column_selector, value.columns), base, false);
  const auto [rows, columns] = sparse_submatrix_result_shape(
      planned_rows, planned_columns, selected_rows.size(), selected_columns.size());
  std::vector<std::pair<std::size_t, std::size_t>> row_map;
  row_map.reserve(selected_rows.size());
  for (std::size_t output_row = 0U; output_row < selected_rows.size(); ++output_row)
    row_map.emplace_back(selected_rows[output_row], output_row);
  std::sort(row_map.begin(), row_map.end());
  sparse_matrix<double> result;
  result.rows = rows; result.columns = columns; result.column_pointers.push_back(0U);
  const auto capacity = sparse_element_count(rows, columns);
  const auto sparse_capacity = std::min(capacity, value.values.size());
  result.row_indices.reserve(sparse_capacity); result.values.reserve(sparse_capacity);
  for (std::size_t column = 0U; column < columns; ++column) {
    const auto source_column = selected_columns[column];
    auto selected = row_map.begin();
    auto stored = value.column_pointers[source_column];
    const auto stored_end = value.column_pointers[source_column + 1U];
    std::vector<std::pair<std::size_t, double>> entries;
    while (selected != row_map.end() && stored < stored_end) {
      const auto selected_row = selected->first;
      const auto stored_row = value.row_indices[stored];
      if (selected_row < stored_row) { ++selected; continue; }
      if (stored_row < selected_row) { ++stored; continue; }
      const auto stored_value = static_cast<double>(value.values[stored++]);
      while (selected != row_map.end() && selected->first == selected_row) {
        entries.emplace_back(selected->second, stored_value);
        ++selected;
      }
    }
    std::sort(entries.begin(), entries.end());
    for (const auto& [row, stored_value] : entries) {
      result.row_indices.push_back(row); result.values.push_back(stored_value);
    }
    result.column_pointers.push_back(result.values.size());
  }
  validate_sparse_csc(result, "submatrix result");
  return result;
}
template <typename T> struct is_sparse_matrix : std::false_type {};
template <typename T> struct is_sparse_matrix<sparse_matrix<T>> : std::true_type {};
struct sparse_replacement_payload {
  std::vector<std::size_t> shape;
  std::vector<double> values;
};
template <typename Replacement> sparse_replacement_payload sparse_replacement_values(
    const Replacement& replacement) {
  sparse_replacement_payload result;
  if constexpr (is_sparse_matrix<std::decay_t<Replacement>>::value) {
    validate_sparse_csc(replacement, "assignment replacement");
    const auto size = sparse_element_count(replacement.rows, replacement.columns);
    result.shape = {replacement.rows, replacement.columns};
    result.values.assign(size, 0.0);
    for (std::size_t column = 0U; column < replacement.columns; ++column)
      for (auto stored = replacement.column_pointers[column];
           stored < replacement.column_pointers[column + 1U]; ++stored)
        result.values[replacement.row_indices[stored] + column * replacement.rows] =
            static_cast<double>(replacement.values[stored]);
  } else if constexpr (is_vector<std::decay_t<Replacement>>::value) {
    selector_shape(replacement, result.shape);
    const auto flattened = flatten_selector_column_major(replacement);
    result.values.reserve(flattened.size());
    for (const auto& value : flattened) result.values.push_back(static_cast<double>(value));
  } else {
    static_assert(std::is_arithmetic_v<std::decay_t<Replacement>>,
                  "MPF Matlab sparse assignment replacement must be real numeric");
    result.values.push_back(static_cast<double>(replacement));
  }
  for (const auto value : result.values)
    if (!std::isfinite(value))
      throw std::invalid_argument(
          "MPF Matlab sparse assignment requires finite real values");
  return result;
}
inline std::vector<std::size_t> sparse_nonsingleton_shape(
    const std::vector<std::size_t>& shape) {
  std::vector<std::size_t> result;
  for (const auto extent : shape)
    if (extent != 1U) result.push_back(extent);
  return result;
}
template <typename Replacement> sparse_replacement_payload sparse_assignment_payload(
    const Replacement& replacement, const bool scalar_expansion,
    const std::vector<std::size_t>& selection_shape, const std::size_t count) {
  auto result = sparse_replacement_values(replacement);
  if (scalar_expansion) {
    if (result.values.size() != 1U)
      throw std::invalid_argument(
          "MPF Matlab sparse scalar expansion requires one replacement value");
    return result;
  }
  if (result.values.size() != count ||
      sparse_nonsingleton_shape(result.shape) != sparse_nonsingleton_shape(selection_shape))
    throw std::invalid_argument("MPF Matlab sparse assignment replacement shape mismatch");
  return result;
}
struct sparse_update {
  std::size_t row{};
  std::size_t column{};
  std::size_t sequence{};
  double value{};
};
inline void verify_sparse_mutation_result_shape(
    const std::vector<std::size_t>& planned, const std::size_t rows,
    const std::size_t columns, const std::string& operation) {
  if (!planned.empty() && planned != std::vector<std::size_t>{rows, columns})
    throw std::invalid_argument("MPF Matlab sparse " + operation +
                                " result shape disagrees with lowering");
}
template <typename T> void sparse_apply_updates(
    sparse_matrix<T>& value, const std::size_t rows, const std::size_t columns,
    std::vector<sparse_update> updates) {
  std::sort(updates.begin(), updates.end(), [](const auto& left, const auto& right) {
    if (left.column != right.column) return left.column < right.column;
    if (left.row != right.row) return left.row < right.row;
    return left.sequence < right.sequence;
  });
  std::vector<sparse_update> collapsed;
  collapsed.reserve(updates.size());
  for (const auto& update : updates) {
    if (!collapsed.empty() && collapsed.back().row == update.row &&
        collapsed.back().column == update.column)
      collapsed.back() = update;
    else
      collapsed.push_back(update);
  }
  sparse_matrix<T> result;
  result.rows = rows; result.columns = columns; result.column_pointers.push_back(0U);
  result.row_indices.reserve(value.row_indices.size() + collapsed.size());
  result.values.reserve(value.values.size() + collapsed.size());
  auto changed = collapsed.begin();
  for (std::size_t column = 0U; column < columns; ++column) {
    auto stored = column < value.columns ? value.column_pointers[column] : value.values.size();
    const auto stored_end =
        column < value.columns ? value.column_pointers[column + 1U] : value.values.size();
    while ((changed != collapsed.end() && changed->column == column) || stored < stored_end) {
      const auto changed_row = changed != collapsed.end() && changed->column == column
                                   ? changed->row
                                   : rows;
      const auto stored_row = stored < stored_end ? value.row_indices[stored] : rows;
      if (stored_row < changed_row) {
        result.row_indices.push_back(stored_row);
        result.values.push_back(value.values[stored++]);
      } else if (changed_row < stored_row) {
        if (changed->value != 0.0) {
          result.row_indices.push_back(changed_row);
          result.values.push_back(static_cast<T>(changed->value));
        }
        ++changed;
      } else {
        if (changed->value != 0.0) {
          result.row_indices.push_back(changed_row);
          result.values.push_back(static_cast<T>(changed->value));
        }
        ++stored; ++changed;
      }
    }
    result.column_pointers.push_back(result.values.size());
  }
  validate_sparse_csc(result, "assignment result");
  value = std::move(result);
}
template <typename T, typename Selector, typename Replacement>
void sparse_assign_linear(
    sparse_matrix<T>& value, const Selector& selector, const Replacement& replacement,
    const std::size_t base, const bool scalar_expansion,
    const std::optional<std::size_t> planned_selection_rows,
    const std::optional<std::size_t> planned_selection_columns,
    const std::vector<std::size_t>& planned_result_shape = {}) {
  validate_sparse_csc(value, "assignment target");
  const auto original_size = sparse_element_count(value.rows, value.columns);
  const auto resolved = resolve_selector_extent(selector, original_size);
  const auto required = selector_growth_extent(original_size, resolved, base, false);
  auto rows = value.rows; auto columns = value.columns;
  if (value.rows == 1U) columns = required;
  else if (value.columns == 1U) rows = required;
  else columns = std::max(value.columns, (required + value.rows - 1U) / value.rows);
  const auto indices = selector_indices(sparse_element_count(rows, columns), resolved, base, false);
  const auto selection_shape = !planned_selection_rows.has_value() &&
                                       !planned_selection_columns.has_value() && indices.size() == 1U
                                   ? std::pair<std::size_t, std::size_t>{1U, 1U}
                                   : sparse_linear_result_shape(
                                         planned_selection_rows, planned_selection_columns,
                                         indices.size());
  verify_sparse_mutation_result_shape(planned_result_shape, rows, columns, "assignment");
  const auto payload = sparse_assignment_payload(
      replacement, scalar_expansion, {selection_shape.first, selection_shape.second},
      indices.size());
  std::vector<sparse_update> updates; updates.reserve(indices.size());
  for (std::size_t sequence = 0U; sequence < indices.size(); ++sequence) {
    const auto index = indices[sequence];
    updates.push_back({index % rows, index / rows, sequence,
                       payload.values[scalar_expansion ? 0U : sequence]});
  }
  sparse_apply_updates(value, rows, columns, std::move(updates));
}
template <typename T, typename RowSelector, typename ColumnSelector, typename Replacement>
void sparse_assign_subscripts(
    sparse_matrix<T>& value, const RowSelector& row_selector,
    const ColumnSelector& column_selector, const Replacement& replacement,
    const std::size_t base, const bool scalar_expansion,
    const std::optional<std::size_t> planned_selection_rows,
    const std::optional<std::size_t> planned_selection_columns,
    const std::vector<std::size_t>& planned_result_shape = {}) {
  validate_sparse_csc(value, "assignment target");
  const auto resolved_rows = resolve_selector_extent(row_selector, value.rows);
  const auto resolved_columns = resolve_selector_extent(column_selector, value.columns);
  const auto rows = selector_growth_extent(value.rows, resolved_rows, base, false);
  const auto columns = selector_growth_extent(value.columns, resolved_columns, base, false);
  const auto selected_rows = selector_indices(rows, resolved_rows, base, false);
  const auto selected_columns = selector_indices(columns, resolved_columns, base, false);
  const auto selection_shape =
      !planned_selection_rows.has_value() && !planned_selection_columns.has_value() &&
              selected_rows.size() == 1U && selected_columns.size() == 1U
          ? std::pair<std::size_t, std::size_t>{1U, 1U}
          : sparse_submatrix_result_shape(planned_selection_rows, planned_selection_columns,
                                          selected_rows.size(), selected_columns.size());
  verify_sparse_mutation_result_shape(planned_result_shape, rows, columns, "assignment");
  const auto count = sparse_element_count(selected_rows.size(), selected_columns.size());
  const auto payload = sparse_assignment_payload(
      replacement, scalar_expansion, {selection_shape.first, selection_shape.second}, count);
  std::vector<sparse_update> updates; updates.reserve(count); std::size_t sequence = 0U;
  for (const auto column : selected_columns)
    for (const auto row : selected_rows) {
      updates.push_back(
          {row, column, sequence, payload.values[scalar_expansion ? 0U : sequence]});
      ++sequence;
    }
  sparse_apply_updates(value, rows, columns, std::move(updates));
}
template <typename T, typename Selectors> void sparse_erase_indexed(
    sparse_matrix<T>& value, const Selectors& selectors, const std::size_t base,
    const bool linear, std::size_t axis,
    const std::vector<std::size_t>& planned_result_shape = {}) {
  validate_sparse_csc(value, "deletion target");
  if (linear) {
    const auto expected = value.rows == 1U ? 1U : value.columns == 1U ? 0U : 2U;
    if (expected > 1U || axis != expected)
      throw std::invalid_argument("MPF Matlab sparse linear deletion requires a vector");
  }
  if (axis > 1U) throw std::invalid_argument("MPF Matlab sparse deletion axis is invalid");
  const auto extent = axis == 0U ? value.rows : value.columns;
  auto removed = erase_selector_indices(selectors, linear ? 0U : axis, extent, base, false);
  std::sort(removed.begin(), removed.end());
  removed.erase(std::unique(removed.begin(), removed.end()), removed.end());
  const auto rows = value.rows - (axis == 0U ? removed.size() : 0U);
  const auto columns = value.columns - (axis == 1U ? removed.size() : 0U);
  verify_sparse_mutation_result_shape(planned_result_shape, rows, columns, "deletion");
  sparse_matrix<T> result;
  result.rows = rows; result.columns = columns; result.column_pointers.push_back(0U);
  result.row_indices.reserve(value.row_indices.size()); result.values.reserve(value.values.size());
  if (axis == 1U) {
    for (std::size_t column = 0U; column < value.columns; ++column) {
      if (std::binary_search(removed.begin(), removed.end(), column)) continue;
      for (auto stored = value.column_pointers[column];
           stored < value.column_pointers[column + 1U]; ++stored) {
        result.row_indices.push_back(value.row_indices[stored]);
        result.values.push_back(value.values[stored]);
      }
      result.column_pointers.push_back(result.values.size());
    }
  } else {
    std::vector<std::size_t> row_map(value.rows, value.rows); std::size_t next_row = 0U;
    for (std::size_t row = 0U; row < value.rows; ++row)
      if (!std::binary_search(removed.begin(), removed.end(), row)) row_map[row] = next_row++;
    for (std::size_t column = 0U; column < value.columns; ++column) {
      for (auto stored = value.column_pointers[column];
           stored < value.column_pointers[column + 1U]; ++stored) {
        const auto row = row_map[value.row_indices[stored]];
        if (row != value.rows) {
          result.row_indices.push_back(row); result.values.push_back(value.values[stored]);
        }
      }
      result.column_pointers.push_back(result.values.size());
    }
  }
  validate_sparse_csc(result, "deletion result");
  value = std::move(result);
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
