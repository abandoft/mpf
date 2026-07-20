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
                                      resultStorage, valueDomain, expectedOperation) {
  const storageValid = storage => storage === 0 || storage === 2 || storage === 3;
  const leftSparse = leftStorage === 3; const rightSparse = rightStorage === 3;
  const expectedResultStorage = leftSparse && rightSparse ? 3 : 2;
  const expectedPolicy = leftSparse && rightSparse ? 1 : 2;
  if (operation !== expectedOperation || !storageValid(leftStorage) ||
      !storageValid(rightStorage) || !leftSparse && !rightSparse ||
      valueDomain !== __mpf_sparse_value_finite_real &&
        valueDomain !== __mpf_sparse_value_finite_complex ||
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
function __mpf_sparse_arithmetic_value(value, name, valueDomain) {
  if (valueDomain === __mpf_sparse_value_finite_complex) {
    const result = __mpf_as_complex(value);
    if (!__mpf_sparse_finite_complex(result)) {
      throw new TypeError(`MPF Matlab ${name} must contain finite numeric values`);
    }
    return result;
  }
  if ((typeof value !== 'number' && typeof value !== 'boolean') ||
      !Number.isFinite(Number(value))) {
    throw new TypeError(`MPF Matlab ${name} must contain finite numeric values`);
  }
  return Number(value);
}
function __mpf_sparse_arithmetic_sparse(value, shape, name, valueDomain) {
  const matrix = __mpf_validate_sparse_csc(value, name);
  if (matrix.rows !== shape[0] || matrix.columns !== shape[1]) {
    throw new RangeError(`MPF Matlab ${name} disagrees with its static shape contract`);
  }
  if (valueDomain === __mpf_sparse_value_finite_real &&
      matrix.valueDomain === __mpf_sparse_value_finite_complex) {
    throw new TypeError('MPF Matlab sparse arithmetic value-domain plan is invalid');
  }
  return matrix;
}
function __mpf_sparse_arithmetic_full(value, shape, storage, name, valueDomain) {
  if (storage === 0) return [__mpf_sparse_arithmetic_value(value, name, valueDomain)];
  const actual = __mpf_matlab_runtime_shape(value, name);
  if (actual.length !== 2 || actual[0] !== shape[0] || actual[1] !== shape[1]) {
    throw new RangeError(`MPF Matlab ${name} disagrees with its static shape contract`);
  }
  return __mpf_flatten_column_major(value).map(
    item => __mpf_sparse_arithmetic_value(item, name, valueDomain));
}
function __mpf_sparse_arithmetic_apply(left, right, subtract, valueDomain) {
  const value = valueDomain === __mpf_sparse_value_finite_complex
    ? (subtract ? __mpf_complex_subtract(left, right) : __mpf_complex_add(left, right))
    : (subtract ? left - right : left + right);
  if (valueDomain === __mpf_sparse_value_finite_complex
        ? !__mpf_sparse_finite_complex(value) : !Number.isFinite(value)) {
    throw new RangeError('MPF Matlab sparse arithmetic produced a nonfinite value');
  }
  return value;
}
function __mpf_sparse_arithmetic_emit(result, row, value, valueDomain) {
  if (__mpf_sparse_nonzero(value, valueDomain)) {
    result.rowIndices.push(row); result.values.push(value);
  }
}
function __mpf_sparse_arithmetic_sparse_columns(left, right, leftColumn, rightColumn,
                                                 resultRows, subtract, valueDomain, result) {
  let leftIndex = left.columnPointers[leftColumn];
  const leftEnd = left.columnPointers[leftColumn + 1];
  let rightIndex = right.columnPointers[rightColumn];
  const rightEnd = right.columnPointers[rightColumn + 1];
  const leftBroadcast = left.rows === 1 && resultRows !== 1;
  const rightBroadcast = right.rows === 1 && resultRows !== 1;
  const leftBroadcastValue = leftBroadcast && leftIndex < leftEnd
    ? __mpf_sparse_arithmetic_value(
        left.values[leftIndex], 'left sparse arithmetic operand', valueDomain)
    : __mpf_sparse_zero(valueDomain);
  const rightBroadcastValue = rightBroadcast && rightIndex < rightEnd
    ? __mpf_sparse_arithmetic_value(
        right.values[rightIndex], 'right sparse arithmetic operand', valueDomain)
    : __mpf_sparse_zero(valueDomain);
  if (__mpf_sparse_nonzero(leftBroadcastValue, valueDomain) ||
      __mpf_sparse_nonzero(rightBroadcastValue, valueDomain)) {
    for (let row = 0; row < resultRows; ++row) {
      let leftValue = leftBroadcastValue; let rightValue = rightBroadcastValue;
      if (!leftBroadcast && leftIndex < leftEnd && left.rowIndices[leftIndex] === row) {
        leftValue = __mpf_sparse_arithmetic_value(
          left.values[leftIndex++], 'left sparse arithmetic operand', valueDomain);
      }
      if (!rightBroadcast && rightIndex < rightEnd && right.rowIndices[rightIndex] === row) {
        rightValue = __mpf_sparse_arithmetic_value(
          right.values[rightIndex++], 'right sparse arithmetic operand', valueDomain);
      }
      __mpf_sparse_arithmetic_emit(
        result, row,
        __mpf_sparse_arithmetic_apply(leftValue, rightValue, subtract, valueDomain), valueDomain);
    }
    return;
  }
  while (leftIndex < leftEnd || rightIndex < rightEnd) {
    const leftRow = leftIndex < leftEnd ? left.rowIndices[leftIndex] : resultRows;
    const rightRow = rightIndex < rightEnd ? right.rowIndices[rightIndex] : resultRows;
    const row = Math.min(leftRow, rightRow);
    const leftValue = leftRow === row
      ? __mpf_sparse_arithmetic_value(
          left.values[leftIndex++], 'left sparse arithmetic operand', valueDomain)
      : __mpf_sparse_zero(valueDomain);
    const rightValue = rightRow === row
      ? __mpf_sparse_arithmetic_value(
          right.values[rightIndex++], 'right sparse arithmetic operand', valueDomain)
      : __mpf_sparse_zero(valueDomain);
    __mpf_sparse_arithmetic_emit(
      result, row,
      __mpf_sparse_arithmetic_apply(leftValue, rightValue, subtract, valueDomain), valueDomain);
  }
}
function __mpf_sparse_arithmetic_preserve(leftValue, rightValue, leftShape, rightShape,
                                          resultShape, subtract, valueDomain) {
  const left = __mpf_sparse_arithmetic_sparse(
    leftValue, leftShape, 'left sparse arithmetic operand', valueDomain);
  const right = __mpf_sparse_arithmetic_sparse(
    rightValue, rightShape, 'right sparse arithmetic operand', valueDomain);
  const result = { columnPointers: [0], rowIndices: [], values: [] };
  for (let column = 0; column < resultShape[1]; ++column) {
    __mpf_sparse_arithmetic_sparse_columns(
      left, right, left.columns === 1 ? 0 : column,
      right.columns === 1 ? 0 : column, resultShape[0], subtract, valueDomain, result);
    result.columnPointers.push(result.values.length);
  }
  return __mpf_make_sparse_csc(resultShape[0], resultShape[1], result.columnPointers,
                               result.rowIndices, result.values, valueDomain);
}
function __mpf_sparse_arithmetic_materialize(leftValue, rightValue, leftShape, rightShape,
                                             resultShape, leftStorage, rightStorage, subtract,
                                             valueDomain) {
  const sparseLeft = leftStorage === 3;
  const sparse = __mpf_sparse_arithmetic_sparse(
    sparseLeft ? leftValue : rightValue, sparseLeft ? leftShape : rightShape,
    sparseLeft ? 'left sparse arithmetic operand' : 'right sparse arithmetic operand',
    valueDomain);
  const fullShape = sparseLeft ? rightShape : leftShape;
  const fullStorage = sparseLeft ? rightStorage : leftStorage;
  const full = __mpf_sparse_arithmetic_full(
    sparseLeft ? rightValue : leftValue, fullShape, fullStorage,
    sparseLeft ? 'right full arithmetic operand' : 'left full arithmetic operand', valueDomain);
  const rows = resultShape[0]; const columns = resultShape[1];
  const flattened = new Array(__mpf_checked_shape_size(resultShape));
  for (let column = 0; column < columns; ++column) {
    const sourceColumn = fullStorage === 0 || fullShape[1] === 1 ? 0 : column;
    for (let row = 0; row < rows; ++row) {
      const sourceRow = fullStorage === 0 || fullShape[0] === 1 ? 0 : row;
      const fullValue = full[fullStorage === 0 ? 0
        : sourceRow + sourceColumn * fullShape[0]];
      flattened[row + column * rows] = sparseLeft && subtract
        ? __mpf_sparse_arithmetic_apply(
            __mpf_sparse_zero(valueDomain), fullValue, true, valueDomain)
        : fullValue;
    }
  }
  for (let column = 0; column < columns; ++column) {
    const sourceColumn = sparse.columns === 1 ? 0 : column;
    for (let index = sparse.columnPointers[sourceColumn];
         index < sparse.columnPointers[sourceColumn + 1]; ++index) {
      const stored = __mpf_sparse_arithmetic_value(
        sparse.values[index], sparseLeft ? 'left sparse arithmetic operand'
          : 'right sparse arithmetic operand', valueDomain);
      const sourceRow = sparse.rowIndices[index];
      const begin = sparse.rows === 1 && rows !== 1 ? 0 : sourceRow;
      const end = sparse.rows === 1 && rows !== 1 ? rows : sourceRow + 1;
      for (let row = begin; row < end; ++row) {
        const position = row + column * rows;
        flattened[position] = __mpf_sparse_arithmetic_apply(
          flattened[position], stored, !sparseLeft && subtract, valueDomain);
      }
    }
  }
  return __mpf_build_column_major(flattened, resultShape);
}
function __mpf_sparse_arithmetic(leftValue, rightValue, leftShape, rightShape, resultShape,
                                 operation, policy, leftStorage, rightStorage, resultStorage,
                                 valueDomain, expectedOperation) {
  __mpf_sparse_arithmetic_plan(
    leftShape, rightShape, resultShape, operation, policy, leftStorage, rightStorage,
    resultStorage, valueDomain, expectedOperation);
  const subtract = expectedOperation === 2;
  if (resultStorage === 3) {
    return __mpf_sparse_arithmetic_preserve(
      leftValue, rightValue, leftShape, rightShape, resultShape, subtract, valueDomain);
  }
  return __mpf_sparse_arithmetic_materialize(
    leftValue, rightValue, leftShape, rightShape, resultShape,
    leftStorage, rightStorage, subtract, valueDomain);
}
function __mpf_sparse_add(leftValue, rightValue, leftShape, rightShape, resultShape,
                          operation, policy, leftStorage, rightStorage, resultStorage,
                          valueDomain) {
  return __mpf_sparse_arithmetic(
    leftValue, rightValue, leftShape, rightShape, resultShape,
    operation, policy, leftStorage, rightStorage, resultStorage, valueDomain, 1);
}
function __mpf_sparse_subtract(leftValue, rightValue, leftShape, rightShape, resultShape,
                               operation, policy, leftStorage, rightStorage, resultStorage,
                               valueDomain) {
  return __mpf_sparse_arithmetic(
    leftValue, rightValue, leftShape, rightShape, resultShape,
    operation, policy, leftStorage, rightStorage, resultStorage, valueDomain, 2);
}
)MPF";
}

}  // namespace mpf::detail
