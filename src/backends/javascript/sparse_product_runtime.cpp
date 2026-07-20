#include "sparse_product_runtime.hpp"

#include <ostream>

namespace mpf::detail {

void emit_javascript_sparse_product_runtime(std::ostream& output) {
  output << R"MPF(function __mpf_sparse_product_size(rows, columns) {
  const size = rows * columns;
  if (!Number.isSafeInteger(size)) {
    throw new RangeError('MPF Matlab sparse matrix product exceeds safe integer limits');
  }
  return size;
}
function __mpf_sparse_product_dense_result(flattened, rows, columns, valueDomain) {
  for (const value of flattened) {
    if (valueDomain === __mpf_sparse_value_finite_complex
          ? !__mpf_sparse_finite_complex(value) : !Number.isFinite(value)) {
      throw new RangeError('MPF Matlab sparse matrix product produced a nonfinite value');
    }
  }
  return __mpf_build_column_major(flattened, [rows, columns]);
}
function __mpf_sparse_product_plan(leftShape, rightShape, resultShape, numericDomain) {
  if (numericDomain !== __mpf_sparse_value_finite_real &&
      numericDomain !== __mpf_sparse_value_finite_complex) {
    throw new RangeError('MPF Matlab sparse matrix product numeric-domain plan is invalid');
  }
  for (const [shape, name] of [[leftShape, 'left'], [rightShape, 'right'],
                               [resultShape, 'result']]) {
    if (!Array.isArray(shape) || shape.length !== 2 ||
        shape.some(extent => !Number.isSafeInteger(extent) || extent < 0)) {
      throw new RangeError(`MPF Matlab sparse matrix product ${name} shape is invalid`);
    }
  }
  if (leftShape[1] !== rightShape[0] || resultShape[0] !== leftShape[0] ||
      resultShape[1] !== rightShape[1]) {
    throw new RangeError('MPF Matlab sparse matrix product shape plans are inconsistent');
  }
  return numericDomain;
}
function __mpf_sparse_product_sparse(value, shape, name, valueDomain) {
  const matrix = __mpf_validate_sparse_csc(value, name);
  if (matrix.rows !== shape[0] || matrix.columns !== shape[1]) {
    throw new RangeError(`MPF Matlab ${name} disagrees with its static shape contract`);
  }
  if (valueDomain === __mpf_sparse_value_finite_real &&
      matrix.valueDomain === __mpf_sparse_value_finite_complex) {
    throw new TypeError('MPF Matlab sparse matrix product numeric-domain plan is invalid');
  }
  return matrix;
}
function __mpf_sparse_product_accumulate(accumulator, left, right, valueDomain) {
  const leftValue = __mpf_sparse_stored_value(left, valueDomain, 'matrix product operand');
  const rightValue = __mpf_sparse_stored_value(right, valueDomain, 'matrix product operand');
  const product = valueDomain === __mpf_sparse_value_finite_complex
    ? __mpf_complex_multiply(leftValue, rightValue) : leftValue * rightValue;
  const result = valueDomain === __mpf_sparse_value_finite_complex
    ? __mpf_complex_add(accumulator, product) : accumulator + product;
  if (valueDomain === __mpf_sparse_value_finite_complex
        ? !__mpf_sparse_finite_complex(result) : !Number.isFinite(result)) {
    throw new RangeError('MPF Matlab sparse matrix product produced a nonfinite value');
  }
  return result;
}
function __mpf_sparse_sparse_mtimes(leftValue, rightValue,
                                    leftShape, rightShape, resultShape, numericDomain) {
  const valueDomain = __mpf_sparse_product_plan(
    leftShape, rightShape, resultShape, numericDomain);
  const left = __mpf_sparse_product_sparse(
    leftValue, leftShape, 'left matrix product operand', valueDomain);
  const right = __mpf_sparse_product_sparse(
    rightValue, rightShape, 'right matrix product operand', valueDomain);
  const columnPointers = [0]; const rowIndices = []; const values = [];
  const marker = new Array(left.rows).fill(-1);
  const accumulator = new Array(left.rows).fill(__mpf_sparse_zero(valueDomain));
  for (let column = 0; column < right.columns; ++column) {
    const touched = [];
    for (let rightIndex = right.columnPointers[column];
         rightIndex < right.columnPointers[column + 1]; ++rightIndex) {
      const inner = right.rowIndices[rightIndex]; const rightValueAt = right.values[rightIndex];
      for (let leftIndex = left.columnPointers[inner];
           leftIndex < left.columnPointers[inner + 1]; ++leftIndex) {
        const row = left.rowIndices[leftIndex];
        const initial = marker[row] !== column ? __mpf_sparse_zero(valueDomain) : accumulator[row];
        const updated = __mpf_sparse_product_accumulate(
          initial, left.values[leftIndex], rightValueAt, valueDomain);
        if (marker[row] !== column) {
          marker[row] = column; touched.push(row);
        }
        accumulator[row] = updated;
      }
    }
    touched.sort((a, b) => a - b);
    for (const row of touched) {
      const value = accumulator[row];
      if (__mpf_sparse_nonzero(value, valueDomain)) {
        rowIndices.push(row); values.push(value);
      }
    }
    columnPointers.push(values.length);
  }
  return __mpf_make_sparse_csc(
    resultShape[0], resultShape[1], columnPointers, rowIndices, values, valueDomain);
}
function __mpf_sparse_dense_mtimes(leftValue, rightValue,
                                   leftShape, rightShape, resultShape, numericDomain) {
  const valueDomain = __mpf_sparse_product_plan(
    leftShape, rightShape, resultShape, numericDomain);
  const left = __mpf_sparse_product_sparse(
    leftValue, leftShape, 'left matrix product operand', valueDomain);
  const right = __mpf_sparse_dense_rank2(
    rightValue, 'right matrix product operand', rightShape, valueDomain);
  const columns = resultShape[1];
  const result = new Array(__mpf_sparse_product_size(resultShape[0], columns))
    .fill(__mpf_sparse_zero(valueDomain));
  for (let column = 0; column < columns; ++column) {
    for (let inner = 0; inner < left.columns; ++inner) {
      const rightItem = right.flattened[inner + column * left.columns];
      if (!__mpf_sparse_nonzero(
            __mpf_sparse_stored_value(
              rightItem, valueDomain, 'right matrix product operand'), valueDomain)) continue;
      for (let index = left.columnPointers[inner]; index < left.columnPointers[inner + 1]; ++index) {
        const position = left.rowIndices[index] + column * resultShape[0];
        result[position] = __mpf_sparse_product_accumulate(
          result[position], left.values[index], rightItem, valueDomain);
      }
    }
  }
  return __mpf_sparse_product_dense_result(result, resultShape[0], columns, valueDomain);
}
function __mpf_dense_sparse_mtimes(leftValue, rightValue,
                                   leftShape, rightShape, resultShape, numericDomain) {
  const valueDomain = __mpf_sparse_product_plan(
    leftShape, rightShape, resultShape, numericDomain);
  const left = __mpf_sparse_dense_rank2(
    leftValue, 'left matrix product operand', leftShape, valueDomain);
  const right = __mpf_sparse_product_sparse(
    rightValue, rightShape, 'right matrix product operand', valueDomain);
  const rows = resultShape[0];
  const result = new Array(__mpf_sparse_product_size(rows, resultShape[1]))
    .fill(__mpf_sparse_zero(valueDomain));
  for (let column = 0; column < right.columns; ++column) {
    for (let index = right.columnPointers[column]; index < right.columnPointers[column + 1]; ++index) {
      const inner = right.rowIndices[index]; const rightItem = right.values[index];
      for (let row = 0; row < rows; ++row) {
        const position = row + column * rows;
        result[position] = __mpf_sparse_product_accumulate(
          result[position], left.flattened[row + inner * rows], rightItem, valueDomain);
      }
    }
  }
  return __mpf_sparse_product_dense_result(result, rows, resultShape[1], valueDomain);
}
)MPF";
}

}  // namespace mpf::detail
