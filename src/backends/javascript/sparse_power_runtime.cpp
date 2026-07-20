#include "sparse_power_runtime.hpp"

#include <ostream>

namespace mpf::detail {

void emit_javascript_sparse_power_runtime(std::ostream& output) {
  output << R"MPF(function __mpf_sparse_power_shape(shape, name) {
  if (!Array.isArray(shape) || shape.length !== 2 ||
      shape.some(extent => !Number.isSafeInteger(extent) || extent < 0)) {
    throw new RangeError(`MPF Matlab sparse matrix power ${name} shape is invalid`);
  }
}
function __mpf_sparse_power_plan(inputShape, resultShape, exponentPolicy,
                                 inputStorage, resultStorage) {
  __mpf_sparse_power_shape(inputShape, 'input');
  __mpf_sparse_power_shape(resultShape, 'result');
  if (exponentPolicy !== 2 || inputStorage !== 3 || resultStorage !== 3 ||
      inputShape[0] !== inputShape[1] ||
      resultShape[0] !== inputShape[0] || resultShape[1] !== inputShape[1]) {
    throw new RangeError('MPF Matlab sparse matrix power plan is inconsistent');
  }
}
function __mpf_sparse_power_exponent(value) {
  if (typeof value !== 'number' || !Number.isSafeInteger(value) || value < 0) {
    throw new RangeError(
      'MPF Matlab sparse matrix power requires a nonnegative safe integer exponent');
  }
  return value;
}
function __mpf_sparse_power_double(matrix) {
  return __mpf_make_sparse_csc(
    matrix.rows, matrix.columns, matrix.columnPointers.slice(), matrix.rowIndices.slice(),
    matrix.values.map(value => Number(value)), __mpf_sparse_value_finite_real);
}
function __mpf_sparse_power_identity(size) {
  const columnPointers = [0]; const rowIndices = []; const values = [];
  for (let column = 0; column < size; ++column) {
    rowIndices.push(column); values.push(1); columnPointers.push(values.length);
  }
  return __mpf_make_sparse_csc(
    size, size, columnPointers, rowIndices, values, __mpf_sparse_value_finite_real);
}
function __mpf_sparse_mpower(value, exponent, inputShape, resultShape,
                             exponentPolicy, inputStorage, resultStorage) {
  __mpf_sparse_power_plan(
    inputShape, resultShape, exponentPolicy, inputStorage, resultStorage);
  let remaining = __mpf_sparse_power_exponent(exponent);
  const input = __mpf_validate_sparse_csc(value, 'matrix power base');
  if (input.rows !== inputShape[0] || input.columns !== inputShape[1]) {
    throw new RangeError('MPF Matlab sparse matrix power base disagrees with its shape plan');
  }
  if (remaining === 0) return __mpf_sparse_power_identity(input.rows);
  let factor = __mpf_sparse_power_double(input);
  let result = null;
  while (remaining > 0) {
    if (remaining % 2 === 1) {
      result = result === null
        ? factor
        : __mpf_sparse_sparse_mtimes(result, factor, resultShape, inputShape, resultShape);
    }
    remaining = Math.floor(remaining / 2);
    if (remaining > 0) {
      factor = __mpf_sparse_sparse_mtimes(
        factor, factor, inputShape, inputShape, inputShape);
    }
  }
  return result;
}
)MPF";
}

}  // namespace mpf::detail
