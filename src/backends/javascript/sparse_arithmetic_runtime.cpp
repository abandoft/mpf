#include "sparse_arithmetic_runtime.hpp"

#include <ostream>

namespace mpf::detail {

void emit_javascript_sparse_arithmetic_runtime(std::ostream& output) {
  output << R"MPF(function __mpf_sparse_arithmetic_shape(shape, storage, name) {
  const rank = storage === 0 ? 0 : 2;
  if (!Array.isArray(shape) || shape.length !== rank ||
      shape.some(extent => !Number.isSafeInteger(extent) || extent < 0)) {
    throw new RangeError(`MPF Matlab ${name} has an invalid static shape contract`);
  }
}
function __mpf_sparse_arithmetic_plan(leftShape, rightShape, resultShape,
                                      operation, policy, leftStorage, rightStorage,
                                      resultStorage, expectedOperation) {
  const storageValid = storage => storage === 0 || storage === 2 || storage === 3;
  const leftSparse = leftStorage === 3; const rightSparse = rightStorage === 3;
  const expectedResultStorage = leftSparse && rightSparse ? 3 : 2;
  const expectedPolicy = leftSparse && rightSparse ? 1 : 2;
  if (operation !== expectedOperation || !storageValid(leftStorage) ||
      !storageValid(rightStorage) || !leftSparse && !rightSparse ||
      resultStorage !== expectedResultStorage || policy !== expectedPolicy) {
    throw new RangeError('MPF Matlab sparse arithmetic storage plan is invalid');
  }
  __mpf_sparse_arithmetic_shape(leftShape, leftStorage, 'left sparse arithmetic shape');
  __mpf_sparse_arithmetic_shape(rightShape, rightStorage, 'right sparse arithmetic shape');
  __mpf_sparse_arithmetic_shape(resultShape, resultStorage, 'sparse arithmetic result shape');
  for (let axis = 0; axis < 2; ++axis) {
    const left = leftStorage === 0 ? 1 : leftShape[axis];
    const right = rightStorage === 0 ? 1 : rightShape[axis];
    const expected = left === right ? left : left === 1 ? right : left;
    if ((left !== right && left !== 1 && right !== 1) || resultShape[axis] !== expected) {
      throw new RangeError('MPF Matlab sparse arithmetic shape mismatch');
    }
  }
  __mpf_checked_shape_size(resultShape);
}
function __mpf_sparse_arithmetic_number(value, name) {
  if ((typeof value !== 'number' && typeof value !== 'boolean') ||
      !Number.isFinite(Number(value))) {
    throw new TypeError(`MPF Matlab ${name} must contain finite real or logical values`);
  }
  return Number(value);
}
function __mpf_sparse_arithmetic_sparse(value, shape, name) {
  const matrix = __mpf_validate_sparse_csc(value, name);
  if (matrix.rows !== shape[0] || matrix.columns !== shape[1]) {
    throw new RangeError(`MPF Matlab ${name} disagrees with its static shape contract`);
  }
  return matrix;
}
function __mpf_sparse_arithmetic_full(value, shape, storage, name) {
  if (storage === 0) return [__mpf_sparse_arithmetic_number(value, name)];
  const actual = __mpf_matlab_runtime_shape(value, name);
  if (actual.length !== 2 || actual[0] !== shape[0] || actual[1] !== shape[1]) {
    throw new RangeError(`MPF Matlab ${name} disagrees with its static shape contract`);
  }
  return __mpf_flatten_column_major(value).map(
    item => __mpf_sparse_arithmetic_number(item, name));
}
function __mpf_sparse_arithmetic_emit(result, row, value) {
  if (!Number.isFinite(value)) {
    throw new RangeError('MPF Matlab sparse arithmetic produced a nonfinite value');
  }
  if (value !== 0) { result.rowIndices.push(row); result.values.push(value); }
}
function __mpf_sparse_arithmetic_sparse_columns(left, right, leftColumn, rightColumn,
                                                 resultRows, subtract, result) {
  let leftIndex = left.columnPointers[leftColumn];
  const leftEnd = left.columnPointers[leftColumn + 1];
  let rightIndex = right.columnPointers[rightColumn];
  const rightEnd = right.columnPointers[rightColumn + 1];
  const leftBroadcast = left.rows === 1 && resultRows !== 1;
  const rightBroadcast = right.rows === 1 && resultRows !== 1;
  const leftBroadcastValue = leftBroadcast && leftIndex < leftEnd
    ? Number(left.values[leftIndex]) : 0;
  const rightBroadcastValue = rightBroadcast && rightIndex < rightEnd
    ? Number(right.values[rightIndex]) : 0;
  if (leftBroadcastValue !== 0 || rightBroadcastValue !== 0) {
    for (let row = 0; row < resultRows; ++row) {
      let leftValue = leftBroadcastValue; let rightValue = rightBroadcastValue;
      if (!leftBroadcast && leftIndex < leftEnd && left.rowIndices[leftIndex] === row) {
        leftValue = Number(left.values[leftIndex++]);
      }
      if (!rightBroadcast && rightIndex < rightEnd && right.rowIndices[rightIndex] === row) {
        rightValue = Number(right.values[rightIndex++]);
      }
      __mpf_sparse_arithmetic_emit(
        result, row, subtract ? leftValue - rightValue : leftValue + rightValue);
    }
    return;
  }
  while (leftIndex < leftEnd || rightIndex < rightEnd) {
    const leftRow = leftIndex < leftEnd ? left.rowIndices[leftIndex] : resultRows;
    const rightRow = rightIndex < rightEnd ? right.rowIndices[rightIndex] : resultRows;
    const row = Math.min(leftRow, rightRow);
    const leftValue = leftRow === row ? Number(left.values[leftIndex++]) : 0;
    const rightValue = rightRow === row ? Number(right.values[rightIndex++]) : 0;
    __mpf_sparse_arithmetic_emit(
      result, row, subtract ? leftValue - rightValue : leftValue + rightValue);
  }
}
function __mpf_sparse_arithmetic_preserve(leftValue, rightValue, leftShape, rightShape,
                                          resultShape, subtract) {
  const left = __mpf_sparse_arithmetic_sparse(
    leftValue, leftShape, 'left sparse arithmetic operand');
  const right = __mpf_sparse_arithmetic_sparse(
    rightValue, rightShape, 'right sparse arithmetic operand');
  const result = { columnPointers: [0], rowIndices: [], values: [] };
  for (let column = 0; column < resultShape[1]; ++column) {
    __mpf_sparse_arithmetic_sparse_columns(
      left, right, left.columns === 1 ? 0 : column,
      right.columns === 1 ? 0 : column, resultShape[0], subtract, result);
    result.columnPointers.push(result.values.length);
  }
  return __mpf_make_sparse_csc(resultShape[0], resultShape[1], result.columnPointers,
                               result.rowIndices, result.values,
                               __mpf_sparse_value_finite_real);
}
function __mpf_sparse_arithmetic_materialize(leftValue, rightValue, leftShape, rightShape,
                                             resultShape, leftStorage, rightStorage, subtract) {
  const sparseLeft = leftStorage === 3;
  const sparse = __mpf_sparse_arithmetic_sparse(
    sparseLeft ? leftValue : rightValue, sparseLeft ? leftShape : rightShape,
    sparseLeft ? 'left sparse arithmetic operand' : 'right sparse arithmetic operand');
  const fullShape = sparseLeft ? rightShape : leftShape;
  const fullStorage = sparseLeft ? rightStorage : leftStorage;
  const full = __mpf_sparse_arithmetic_full(
    sparseLeft ? rightValue : leftValue, fullShape, fullStorage,
    sparseLeft ? 'right full arithmetic operand' : 'left full arithmetic operand');
  const rows = resultShape[0]; const columns = resultShape[1];
  const flattened = new Array(__mpf_checked_shape_size(resultShape));
  const fullSign = sparseLeft && subtract ? -1 : 1;
  for (let column = 0; column < columns; ++column) {
    const sourceColumn = fullStorage === 0 || fullShape[1] === 1 ? 0 : column;
    for (let row = 0; row < rows; ++row) {
      const sourceRow = fullStorage === 0 || fullShape[0] === 1 ? 0 : row;
      flattened[row + column * rows] =
        fullSign * full[fullStorage === 0 ? 0 : sourceRow + sourceColumn * fullShape[0]];
    }
  }
  const sparseSign = sparseLeft || !subtract ? 1 : -1;
  for (let column = 0; column < columns; ++column) {
    const sourceColumn = sparse.columns === 1 ? 0 : column;
    for (let index = sparse.columnPointers[sourceColumn];
         index < sparse.columnPointers[sourceColumn + 1]; ++index) {
      const stored = sparseSign * Number(sparse.values[index]);
      const sourceRow = sparse.rowIndices[index];
      const begin = sparse.rows === 1 && rows !== 1 ? 0 : sourceRow;
      const end = sparse.rows === 1 && rows !== 1 ? rows : sourceRow + 1;
      for (let row = begin; row < end; ++row) {
        const position = row + column * rows;
        const updated = flattened[position] + stored;
        if (!Number.isFinite(updated)) {
          throw new RangeError('MPF Matlab sparse arithmetic produced a nonfinite value');
        }
        flattened[position] = updated;
      }
    }
  }
  return __mpf_build_column_major(flattened, resultShape);
}
function __mpf_sparse_arithmetic(leftValue, rightValue, leftShape, rightShape, resultShape,
                                 operation, policy, leftStorage, rightStorage, resultStorage,
                                 expectedOperation) {
  __mpf_sparse_arithmetic_plan(
    leftShape, rightShape, resultShape, operation, policy, leftStorage, rightStorage,
    resultStorage, expectedOperation);
  const subtract = expectedOperation === 2;
  if (resultStorage === 3) {
    return __mpf_sparse_arithmetic_preserve(
      leftValue, rightValue, leftShape, rightShape, resultShape, subtract);
  }
  return __mpf_sparse_arithmetic_materialize(
    leftValue, rightValue, leftShape, rightShape, resultShape,
    leftStorage, rightStorage, subtract);
}
function __mpf_sparse_add(leftValue, rightValue, leftShape, rightShape, resultShape,
                          operation, policy, leftStorage, rightStorage, resultStorage) {
  return __mpf_sparse_arithmetic(
    leftValue, rightValue, leftShape, rightShape, resultShape,
    operation, policy, leftStorage, rightStorage, resultStorage, 1);
}
function __mpf_sparse_subtract(leftValue, rightValue, leftShape, rightShape, resultShape,
                               operation, policy, leftStorage, rightStorage, resultStorage) {
  return __mpf_sparse_arithmetic(
    leftValue, rightValue, leftShape, rightShape, resultShape,
    operation, policy, leftStorage, rightStorage, resultStorage, 2);
}
)MPF";
}

}  // namespace mpf::detail
