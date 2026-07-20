#include "sparse_reduction_runtime.hpp"

#include <ostream>

namespace mpf::detail {

void emit_javascript_sparse_reduction_runtime(std::ostream& output) {
  output << R"MPF(function __mpf_sparse_logical_reduction_plan(
    matrix, inputShape, axes, resultShape, outputShape,
    operation, policy, inputStorage, resultStorage) {
  if (!Array.isArray(inputShape) || inputShape.length !== 2 ||
      !Array.isArray(axes) || !Array.isArray(resultShape) || resultShape.length !== 2 ||
      !Array.isArray(outputShape) ||
      inputShape.some(value => !Number.isSafeInteger(value) || value < 0) ||
      resultShape.some(value => !Number.isSafeInteger(value) || value < 0) ||
      matrix.rows !== inputShape[0] || matrix.columns !== inputShape[1] ||
      (operation !== 1 && operation !== 2)) {
    throw new RangeError('MPF Matlab sparse logical reduction plan is invalid');
  }
  let previous = -1;
  for (const axis of axes) {
    if (!Number.isSafeInteger(axis) || axis < 0 || axis >= 2 || axis <= previous) {
      throw new RangeError('MPF Matlab sparse logical reduction axes are invalid');
    }
    previous = axis;
  }
  for (let axis = 0; axis < 2; ++axis) {
    const expected = axes.includes(axis) ? 1 : inputShape[axis];
    if (resultShape[axis] !== expected) {
      throw new RangeError('MPF Matlab sparse logical reduction result shape is invalid');
    }
  }
  const resultSize = resultShape[0] * resultShape[1];
  if (!Number.isSafeInteger(resultSize)) {
    throw new RangeError('MPF Matlab sparse logical reduction result exceeds safe integer limits');
  }
  const scalarResult = outputShape.length === 0;
  if (scalarResult ? resultSize !== 1
                   : outputShape.length !== 2 || outputShape[0] !== resultShape[0] ||
                       outputShape[1] !== resultShape[1]) {
    throw new RangeError('MPF Matlab sparse logical reduction output shape is invalid');
  }
  const expectedPolicy = scalarResult ? 3 : 2;
  const expectedResultStorage = scalarResult ? 0 : 3;
  if (policy !== expectedPolicy || inputStorage !== 3 || resultStorage !== expectedResultStorage) {
    throw new RangeError('MPF Matlab sparse logical reduction storage plan is invalid');
  }
  const inputSize = matrix.rows * matrix.columns;
  if (!Number.isSafeInteger(inputSize)) {
    throw new RangeError('MPF Matlab sparse logical reduction input exceeds safe integer limits');
  }
  return { allMode: operation === 1, scalarResult, inputSize };
}
function __mpf_sparse_logical_rows(matrix, allMode) {
  const sortedRows = matrix.rowIndices.slice().sort((left, right) => left - right);
  const rowIndices = []; const values = [];
  if (allMode && matrix.columns === 0) {
    for (let row = 0; row < matrix.rows; ++row) {
      rowIndices.push(row); values.push(true);
    }
  } else {
    for (let index = 0; index < sortedRows.length;) {
      const row = sortedRows[index]; let end = index + 1;
      while (end < sortedRows.length && sortedRows[end] === row) ++end;
      if (allMode ? end - index === matrix.columns : true) {
        rowIndices.push(row); values.push(true);
      }
      index = end;
    }
  }
  return __mpf_make_sparse_csc(matrix.rows, 1, [0, values.length], rowIndices, values,
                               __mpf_sparse_value_logical);
}
function __mpf_sparse_logical_columns(matrix, allMode) {
  const columnPointers = [0]; const rowIndices = []; const values = [];
  for (let column = 0; column < matrix.columns; ++column) {
    const count = matrix.columnPointers[column + 1] - matrix.columnPointers[column];
    if (allMode ? count === matrix.rows : count !== 0) {
      rowIndices.push(0); values.push(true);
    }
    columnPointers.push(values.length);
  }
  return __mpf_make_sparse_csc(1, matrix.columns, columnPointers, rowIndices, values,
                               __mpf_sparse_value_logical);
}
function __mpf_sparse_logical_reduction_scalar(matrix, axes, allMode, inputSize) {
  if (axes.length === 0 || axes.length === 2) {
    return allMode ? matrix.values.length === inputSize : matrix.values.length !== 0;
  }
  if (axes[0] === 0) {
    const count = matrix.columnPointers[1] - matrix.columnPointers[0];
    return allMode ? count === matrix.rows : count !== 0;
  }
  return allMode ? matrix.values.length === matrix.columns : matrix.values.length !== 0;
}
function __mpf_sparse_logical_reduce(value, inputShape, axes, resultShape, outputShape,
                                     operation, policy, inputStorage, resultStorage) {
  const matrix = __mpf_validate_sparse_csc(value, 'sparse logical reduction operand');
  const plan = __mpf_sparse_logical_reduction_plan(
    matrix, inputShape, axes, resultShape, outputShape, operation, policy,
    inputStorage, resultStorage);
  if (plan.scalarResult) {
    return __mpf_sparse_logical_reduction_scalar(matrix, axes, plan.allMode, plan.inputSize);
  }
  if (axes.length === 0) {
    return __mpf_make_sparse_csc(
      matrix.rows, matrix.columns, matrix.columnPointers.slice(), matrix.rowIndices.slice(),
      new Array(matrix.values.length).fill(true), __mpf_sparse_value_logical);
  }
  if (axes.length === 1 && axes[0] === 0) {
    return __mpf_sparse_logical_columns(matrix, plan.allMode);
  }
  if (axes.length === 1 && axes[0] === 1) {
    return __mpf_sparse_logical_rows(matrix, plan.allMode);
  }
  throw new RangeError('MPF Matlab nonscalar sparse logical reduction axes are invalid');
}
)MPF";
}

}  // namespace mpf::detail
