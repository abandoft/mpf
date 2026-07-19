#include "sparse_matrix_runtime.hpp"

#include <ostream>

namespace mpf::detail {

void emit_javascript_sparse_matrix_runtime(std::ostream& output) {
  output << R"MPF(const __mpf_sparse_csc_tag = Symbol('mpf.sparse.csc');
function __mpf_issparse(value) {
  return value !== null && typeof value === 'object' && value[__mpf_sparse_csc_tag] === true;
}
function __mpf_validate_sparse_csc(value, name = 'sparse matrix') {
  if (!__mpf_issparse(value)) throw new TypeError(`MPF Matlab ${name} is not a CSC matrix`);
  const { rows, columns, columnPointers, rowIndices, values } = value;
  if (!Number.isSafeInteger(rows) || rows < 0 || !Number.isSafeInteger(columns) || columns < 0 ||
      !Array.isArray(columnPointers) || columnPointers.length !== columns + 1 ||
      !Array.isArray(rowIndices) || !Array.isArray(values) || rowIndices.length !== values.length ||
      columnPointers[0] !== 0 || columnPointers[columns] !== values.length) {
    throw new RangeError(`MPF Matlab ${name} has an invalid CSC structure`);
  }
  for (let column = 0; column < columns; ++column) {
    const begin = columnPointers[column]; const end = columnPointers[column + 1];
    if (!Number.isSafeInteger(begin) || !Number.isSafeInteger(end) || begin > end || begin < 0 ||
        end > values.length) throw new RangeError(`MPF Matlab ${name} has invalid CSC pointers`);
    let previous = -1;
    for (let index = begin; index < end; ++index) {
      const row = rowIndices[index]; const numeric = values[index];
      if (!Number.isSafeInteger(row) || row <= previous || row >= rows ||
          typeof numeric !== 'number' || !Number.isFinite(numeric) || numeric === 0) {
        throw new RangeError(`MPF Matlab ${name} is not canonical finite-real CSC`);
      }
      previous = row;
    }
  }
  return value;
}
function __mpf_make_sparse_csc(rows, columns, columnPointers, rowIndices, values) {
  return __mpf_validate_sparse_csc({
    rows, columns, columnPointers, rowIndices, values, [__mpf_sparse_csc_tag]: true
  });
}
function __mpf_sparse_dense_rank2(value, name) {
  const shape = __mpf_matlab_runtime_shape(value, name);
  if (shape.length !== 2) throw new RangeError(`MPF Matlab ${name} must be rank 2`);
  const flattened = __mpf_flatten_column_major(value);
  for (const item of flattened) {
    if (typeof item !== 'number' || !Number.isFinite(item)) {
      throw new TypeError(`MPF Matlab ${name} requires finite real values`);
    }
  }
  return { shape, flattened };
}
function __mpf_sparse(value) {
  if (__mpf_issparse(value)) return __mpf_validate_sparse_csc(value);
  const { shape, flattened } = __mpf_sparse_dense_rank2(value, 'sparse input');
  const [rows, columns] = shape;
  const columnPointers = [0]; const rowIndices = []; const values = [];
  for (let column = 0; column < columns; ++column) {
    for (let row = 0; row < rows; ++row) {
      const numeric = flattened[row + column * rows];
      if (numeric !== 0) { rowIndices.push(row); values.push(numeric); }
    }
    columnPointers.push(values.length);
  }
  return __mpf_make_sparse_csc(rows, columns, columnPointers, rowIndices, values);
}
function __mpf_full(value) {
  if (!__mpf_issparse(value)) {
    __mpf_sparse_dense_rank2(value, 'full input');
    return value;
  }
  const matrix = __mpf_validate_sparse_csc(value);
  const flattened = new Array(matrix.rows * matrix.columns).fill(0);
  for (let column = 0; column < matrix.columns; ++column) {
    for (let index = matrix.columnPointers[column]; index < matrix.columnPointers[column + 1];
         ++index) {
      flattened[matrix.rowIndices[index] + column * matrix.rows] = matrix.values[index];
    }
  }
  return __mpf_build_column_major(flattened, [matrix.rows, matrix.columns]);
}
function __mpf_nnz(value) {
  if (__mpf_issparse(value)) return __mpf_validate_sparse_csc(value).values.length;
  if (!Array.isArray(value)) {
    if (typeof value !== 'number' && typeof value !== 'boolean') {
      throw new TypeError('MPF Matlab nnz requires a real or logical value');
    }
    return Number(value) !== 0 ? 1 : 0;
  }
  let count = 0;
  for (const item of __mpf_flatten_column_major(value)) {
    if ((typeof item !== 'number' && typeof item !== 'boolean') ||
        (typeof item === 'number' && !Number.isFinite(item))) {
      throw new TypeError('MPF Matlab nnz requires finite real or logical values');
    }
    if (Number(item) !== 0) ++count;
  }
  return count;
}
function __mpf_sparse_transpose(value) {
  const matrix = __mpf_validate_sparse_csc(value, 'transpose operand');
  const counts = new Array(matrix.rows).fill(0);
  for (const row of matrix.rowIndices) ++counts[row];
  const pointers = [0];
  for (const count of counts) pointers.push(pointers[pointers.length - 1] + count);
  const next = pointers.slice(0, -1); const rows = new Array(matrix.values.length);
  const values = new Array(matrix.values.length);
  for (let column = 0; column < matrix.columns; ++column) {
    for (let index = matrix.columnPointers[column]; index < matrix.columnPointers[column + 1];
         ++index) {
      const target = next[matrix.rowIndices[index]]++;
      rows[target] = column; values[target] = matrix.values[index];
    }
  }
  return __mpf_make_sparse_csc(matrix.columns, matrix.rows, pointers, rows, values);
}
function __mpf_sparse_is_tridiagonal(matrix) {
  for (let column = 0; column < matrix.columns; ++column) {
    for (let index = matrix.columnPointers[column]; index < matrix.columnPointers[column + 1];
         ++index) if (Math.abs(matrix.rowIndices[index] - column) > 1) return false;
  }
  return true;
}
function __mpf_sparse_tridiagonal_factor(matrix) {
  const size = matrix.rows; const lower = new Array(Math.max(0, size - 1)).fill(0);
  const diagonal = new Array(size).fill(0); const upper = new Array(Math.max(0, size - 1)).fill(0);
  for (let column = 0; column < size; ++column) {
    for (let index = matrix.columnPointers[column]; index < matrix.columnPointers[column + 1];
         ++index) {
      const row = matrix.rowIndices[index]; const value = matrix.values[index];
      if (row === column) diagonal[column] = value;
      else if (row === column + 1) lower[column] = value;
      else if (column === row + 1) upper[row] = value;
    }
  }
  for (let row = 1; row < size; ++row) {
    if (diagonal[row - 1] === 0 || !Number.isFinite(diagonal[row - 1])) return null;
    lower[row - 1] /= diagonal[row - 1];
    diagonal[row] -= lower[row - 1] * upper[row - 1];
  }
  if (size !== 0 && (diagonal[size - 1] === 0 || !Number.isFinite(diagonal[size - 1]))) return null;
  return { lower, diagonal, upper, singular: false };
}
function __mpf_sparse_tridiagonal_apply(factor, input) {
  const values = input.map((row) => row.slice()); const size = factor.diagonal.length;
  for (let row = 1; row < size; ++row) for (let column = 0; column < values[0].length; ++column)
    values[row][column] -= factor.lower[row - 1] * values[row - 1][column];
  for (let reverse = size; reverse > 0; --reverse) {
    const row = reverse - 1;
    for (let column = 0; column < values[0].length; ++column) {
      if (row + 1 < size) values[row][column] -= factor.upper[row] * values[row + 1][column];
      values[row][column] /= factor.diagonal[row];
    }
  }
  return values;
}
function __mpf_sparse_tridiagonal_apply_transpose(factor, input) {
  const values = input.map((row) => row.slice()); const size = factor.diagonal.length;
  for (let row = 0; row < size; ++row) for (let column = 0; column < values[0].length; ++column) {
    if (row !== 0) values[row][column] -= factor.upper[row - 1] * values[row - 1][column];
    values[row][column] /= factor.diagonal[row];
  }
  for (let reverse = size; reverse > 1; --reverse) {
    const row = reverse - 2;
    for (let column = 0; column < values[0].length; ++column)
      values[row][column] -= factor.lower[row] * values[row + 1][column];
  }
  return values;
}
function __mpf_sparse_row_lu_factor(matrix) {
  const size = matrix.rows; const rows = Array.from({ length: size }, () => new Map());
  for (let column = 0; column < size; ++column) for (let index = matrix.columnPointers[column];
       index < matrix.columnPointers[column + 1]; ++index)
    rows[matrix.rowIndices[index]].set(column, matrix.values[index]);
  const swaps = new Array(size); let singular = false;
  const set = (row, column, value) => {
    if (value === 0) rows[row].delete(column); else rows[row].set(column, value);
  };
  for (let pivot = 0; pivot < size; ++pivot) {
    let selected = pivot; let magnitude = Math.abs(rows[pivot].get(pivot) ?? 0);
    for (let row = pivot + 1; row < size; ++row) {
      const candidate = Math.abs(rows[row].get(pivot) ?? 0);
      if (candidate > magnitude) { selected = row; magnitude = candidate; }
    }
    swaps[pivot] = selected;
    if (selected !== pivot) [rows[pivot], rows[selected]] = [rows[selected], rows[pivot]];
    const diagonal = rows[pivot].get(pivot) ?? 0;
    if (diagonal === 0 || !Number.isFinite(diagonal)) { singular = true; continue; }
    const pivotEntries = [...rows[pivot].entries()].filter(([column]) => column > pivot);
    for (let row = pivot + 1; row < size; ++row) {
      const below = rows[row].get(pivot) ?? 0;
      if (below === 0) continue;
      const multiplier = below / diagonal; set(row, pivot, multiplier);
      for (const [column, value] of pivotEntries)
        set(row, column, (rows[row].get(column) ?? 0) - multiplier * value);
    }
  }
  return { rows, swaps, singular };
}
function __mpf_sparse_row_lu_apply(factor, input) {
  const values = input.map((row) => row.slice()); const size = factor.rows.length;
  for (let pivot = 0; pivot < size; ++pivot) if (factor.swaps[pivot] !== pivot)
    [values[pivot], values[factor.swaps[pivot]]] = [values[factor.swaps[pivot]], values[pivot]];
  for (let row = 0; row < size; ++row) for (const [column, value] of factor.rows[row])
    if (column < row) for (let rhs = 0; rhs < values[0].length; ++rhs)
      values[row][rhs] -= value * values[column][rhs];
  for (let reverse = size; reverse > 0; --reverse) {
    const row = reverse - 1; const diagonal = factor.rows[row].get(row) ?? 0;
    for (const [column, value] of factor.rows[row]) if (column > row)
      for (let rhs = 0; rhs < values[0].length; ++rhs)
        values[row][rhs] -= value * values[column][rhs];
    for (let rhs = 0; rhs < values[0].length; ++rhs) values[row][rhs] /= diagonal;
  }
  return values;
}
function __mpf_sparse_row_lu_apply_transpose(factor, input) {
  const values = input.map((row) => row.slice()); const size = factor.rows.length;
  for (let row = 0; row < size; ++row) {
    const diagonal = factor.rows[row].get(row) ?? 0;
    for (let inner = 0; inner < row; ++inner) {
      const value = factor.rows[inner].get(row) ?? 0;
      if (value !== 0) for (let rhs = 0; rhs < values[0].length; ++rhs)
        values[row][rhs] -= value * values[inner][rhs];
    }
    for (let rhs = 0; rhs < values[0].length; ++rhs) values[row][rhs] /= diagonal;
  }
  for (let reverse = size; reverse > 0; --reverse) {
    const row = reverse - 1;
    for (let inner = row + 1; inner < size; ++inner) {
      const value = factor.rows[inner].get(row) ?? 0;
      if (value !== 0) for (let rhs = 0; rhs < values[0].length; ++rhs)
        values[row][rhs] -= value * values[inner][rhs];
    }
  }
  for (let reverse = size; reverse > 0; --reverse) {
    const pivot = reverse - 1; const selected = factor.swaps[pivot];
    if (selected !== pivot) [values[pivot], values[selected]] = [values[selected], values[pivot]];
  }
  return values;
}
function __mpf_sparse_one_norm(matrix) {
  let maximum = 0;
  for (let column = 0; column < matrix.columns; ++column) {
    let sum = 0;
    for (let index = matrix.columnPointers[column]; index < matrix.columnPointers[column + 1];
         ++index) sum += Math.abs(matrix.values[index]);
    maximum = Math.max(maximum, sum);
  }
  return maximum;
}
function __mpf_sparse_rcond(matrix, factor, apply, applyTranspose) {
  if (factor.singular) return 0;
  const norm = __mpf_sparse_one_norm(matrix); const size = matrix.rows;
  if (norm === 0 || size === 0 || !Number.isFinite(norm)) return 0;
  let vector = Array.from({ length: size }, () => [1 / size]); let inverseNorm = 0;
  for (let iteration = 0; iteration < 5; ++iteration) {
    const solved = apply(factor, vector);
    const estimate = solved.reduce((sum, row) => sum + Math.abs(row[0]), 0);
    if (!Number.isFinite(estimate)) return 0;
    inverseNorm = Math.max(inverseNorm, estimate);
    const transposed = applyTranspose(factor, solved.map((row) => [row[0] >= 0 ? 1 : -1]));
    if (!transposed.every((row) => Number.isFinite(row[0]))) return 0;
    let selected = 0;
    for (let row = 1; row < size; ++row)
      if (Math.abs(transposed[row][0]) > Math.abs(transposed[selected][0])) selected = row;
    let dual = 0;
    for (let row = 0; row < size; ++row) dual += transposed[row][0] * vector[row][0];
    if (Math.abs(transposed[selected][0]) <= dual) break;
    vector = Array.from({ length: size }, (_, row) => [row === selected ? 1 : 0]);
  }
  const product = norm * inverseNorm;
  return product > 0 && Number.isFinite(product) ? Math.min(1, 1 / product) : 0;
}
function __mpf_matlab_mldivide_sparse_real_square(coefficients, rightHandSide) {
  const matrix = __mpf_validate_sparse_csc(coefficients, 'sparse coefficient matrix');
  if (matrix.rows === 0 || matrix.rows !== matrix.columns) {
    throw new RangeError('MPF Matlab sparse solve requires a non-empty square coefficient matrix');
  }
  const preserveSparse = __mpf_issparse(rightHandSide);
  const denseRight = preserveSparse ? __mpf_full(rightHandSide) : rightHandSide;
  const right = __mpf_matlab_dense_matrix(denseRight, 'right-hand side');
  if (right.length !== matrix.rows) throw new RangeError('MPF Matlab sparse solve shape mismatch');
  let factor; let apply; let applyTranspose;
  if (__mpf_sparse_is_tridiagonal(matrix)) {
    factor = __mpf_sparse_tridiagonal_factor(matrix);
    if (factor !== null) {
      apply = __mpf_sparse_tridiagonal_apply;
      applyTranspose = __mpf_sparse_tridiagonal_apply_transpose;
    }
  }
  if (factor == null) {
    factor = __mpf_sparse_row_lu_factor(matrix);
    apply = __mpf_sparse_row_lu_apply; applyTranspose = __mpf_sparse_row_lu_apply_transpose;
  }
  __mpf_matlab_warn_square_condition(
      __mpf_sparse_rcond(matrix, factor, apply, applyTranspose));
  const result = apply(factor, right);
  return preserveSparse ? __mpf_sparse(result) : result;
}
function __mpf_matlab_mrdivide_sparse_real_square(leftValue, coefficients) {
  const preserveSparse = __mpf_issparse(leftValue);
  const left = preserveSparse ? __mpf_sparse_transpose(leftValue) :
    __mpf_matlab_matrix_transpose(__mpf_matlab_dense_matrix(leftValue, 'left operand'));
  const solved = __mpf_matlab_mldivide_sparse_real_square(
      __mpf_sparse_transpose(coefficients), left);
  return preserveSparse ? __mpf_sparse_transpose(solved) : __mpf_matlab_matrix_transpose(solved);
}
)MPF";
}

}  // namespace mpf::detail
