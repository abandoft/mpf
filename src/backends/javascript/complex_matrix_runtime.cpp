#include "complex_matrix_runtime.hpp"

#include <ostream>

namespace mpf::detail {

void emit_javascript_complex_matrix_runtime(std::ostream& output) {
  output << R"MPF(function __mpf_matlab_complex_dense_matrix(values, name, requireFinite = true) {
  if (!Array.isArray(values) || values.length === 0) {
    throw new RangeError(`MPF Matlab ${name} must be a non-empty matrix`);
  }
  if (values.every((value) => !Array.isArray(value))) values = [values];
  if (!values.every(Array.isArray)) {
    throw new RangeError(`MPF Matlab ${name} must be a rectangular rank-2 matrix`);
  }
  const columns = values[0].length;
  if (columns === 0 || !values.every((row) => row.length === columns)) {
    throw new RangeError(`MPF Matlab ${name} must be a non-empty rectangular matrix`);
  }
  return values.map((row) => row.map((value) => {
    const numeric = __mpf_as_complex(value);
    if (requireFinite && (!Number.isFinite(numeric.re) || !Number.isFinite(numeric.im))) {
      throw new TypeError(`MPF Matlab ${name} requires finite numeric values`);
    }
    return numeric;
  }));
}
function __mpf_matlab_complex_mtimes(leftValue, rightValue) {
  const left = __mpf_matlab_complex_dense_matrix(leftValue, 'left matrix operand', false);
  const right = __mpf_matlab_complex_dense_matrix(rightValue, 'right matrix operand', false);
  const innerExtent = left[0].length;
  const columns = right[0].length;
  if (innerExtent !== right.length) {
    throw new RangeError('MPF Matlab complex matrix multiplication shape mismatch');
  }
  return Array.from({ length: left.length }, (_, row) =>
    Array.from({ length: columns }, (_, column) => {
      let sum = __mpf_complex(0, 0);
      for (let inner = 0; inner < innerExtent; ++inner) {
        sum = __mpf_complex_add(
          sum, __mpf_complex_multiply(left[row][inner], right[inner][column]));
      }
      return sum;
    }));
}
function __mpf_matlab_complex_identity(size) {
  return Array.from({ length: size }, (_, row) =>
    Array.from({ length: size }, (_, column) => __mpf_complex(row === column ? 1 : 0, 0)));
}
function __mpf_matlab_complex_ctranspose(values) {
  return Array.from({ length: values[0].length }, (_, column) =>
    values.map((row) => __mpf_conj(row[column])));
}
function __mpf_matlab_complex_matrix_one_norm(values) {
  let maximum = 0;
  for (let column = 0; column < values[0].length; ++column) {
    let sum = 0;
    for (let row = 0; row < values.length; ++row) sum += __mpf_abs(values[row][column]);
    maximum = Math.max(maximum, sum);
  }
  return maximum;
}
function __mpf_matlab_complex_lu_factor(coefficients) {
  const size = coefficients.length;
  const lu = coefficients.map((row) => row.slice());
  const swaps = new Array(size);
  let singular = false;
  for (let pivot = 0; pivot < size; ++pivot) {
    let selected = pivot;
    for (let row = pivot + 1; row < size; ++row) {
      if (__mpf_abs(lu[row][pivot]) > __mpf_abs(lu[selected][pivot])) selected = row;
    }
    swaps[pivot] = selected;
    if (selected !== pivot) [lu[pivot], lu[selected]] = [lu[selected], lu[pivot]];
    const pivotMagnitude = __mpf_abs(lu[pivot][pivot]);
    if (pivotMagnitude === 0 || !Number.isFinite(pivotMagnitude)) {
      singular = true;
      continue;
    }
    for (let row = pivot + 1; row < size; ++row) {
      lu[row][pivot] = __mpf_complex_divide(lu[row][pivot], lu[pivot][pivot]);
      for (let column = pivot + 1; column < size; ++column) {
        lu[row][column] = __mpf_complex_subtract(
          lu[row][column], __mpf_complex_multiply(lu[row][pivot], lu[pivot][column]));
      }
    }
  }
  return { lu, swaps, singular };
}
function __mpf_matlab_complex_lu_apply(factor, values) {
  const result = values.map((row) => row.slice());
  const size = factor.lu.length;
  const columns = result[0].length;
  for (let pivot = 0; pivot < size; ++pivot) {
    if (factor.swaps[pivot] !== pivot) {
      [result[pivot], result[factor.swaps[pivot]]] =
        [result[factor.swaps[pivot]], result[pivot]];
    }
    for (let row = pivot + 1; row < size; ++row) {
      for (let column = 0; column < columns; ++column) {
        result[row][column] = __mpf_complex_subtract(
          result[row][column], __mpf_complex_multiply(
            factor.lu[row][pivot], result[pivot][column]));
      }
    }
  }
  for (let reverse = size; reverse > 0; --reverse) {
    const row = reverse - 1;
    for (let column = 0; column < columns; ++column) {
      let value = result[row][column];
      for (let inner = row + 1; inner < size; ++inner) {
        value = __mpf_complex_subtract(
          value, __mpf_complex_multiply(factor.lu[row][inner], result[inner][column]));
      }
      result[row][column] = __mpf_complex_divide(value, factor.lu[row][row]);
    }
  }
  return result;
}
function __mpf_matlab_complex_lu_rcond(coefficients, factor) {
  if (factor.singular) return 0;
  const inverse = __mpf_matlab_complex_lu_apply(
    factor, __mpf_matlab_complex_identity(coefficients.length));
  const denominator = __mpf_matlab_complex_matrix_one_norm(coefficients) *
    __mpf_matlab_complex_matrix_one_norm(inverse);
  return denominator === 0 || !Number.isFinite(denominator) ? 0 : 1 / denominator;
}
function __mpf_matlab_is_hermitian(values) {
  for (let row = 0; row < values.length; ++row) {
    if (values[row][row].im !== 0) return false;
    for (let column = 0; column < row; ++column) {
      const lower = values[row][column];
      const upper = values[column][row];
      if (lower.re !== upper.re || lower.im !== -upper.im) return false;
    }
  }
  return true;
}
function __mpf_matlab_complex_cholesky_factor(coefficients) {
  const size = coefficients.length;
  const lower = Array.from({ length: size }, () =>
    Array.from({ length: size }, () => __mpf_complex(0, 0)));
  for (let row = 0; row < size; ++row) {
    for (let column = 0; column <= row; ++column) {
      let value = coefficients[row][column];
      for (let inner = 0; inner < column; ++inner) {
        value = __mpf_complex_subtract(value, __mpf_complex_multiply(
          lower[row][inner], __mpf_conj(lower[column][inner])));
      }
      if (row === column) {
        const tolerance = Number.EPSILON * Math.max(1, Math.abs(value.re));
        if (!(value.re > 0) || Math.abs(value.im) > tolerance || !Number.isFinite(value.re)) {
          return { lower, positiveDefinite: false };
        }
        lower[row][column] = __mpf_complex(Math.sqrt(value.re), 0);
      } else {
        lower[row][column] = __mpf_complex_divide(value, lower[column][column]);
      }
    }
  }
  return { lower, positiveDefinite: true };
}
function __mpf_matlab_complex_cholesky_apply(factor, values) {
  const result = values.map((row) => row.slice());
  const size = factor.lower.length;
  const columns = result[0].length;
  for (let row = 0; row < size; ++row) {
    for (let column = 0; column < columns; ++column) {
      let value = result[row][column];
      for (let inner = 0; inner < row; ++inner) {
        value = __mpf_complex_subtract(value, __mpf_complex_multiply(
          factor.lower[row][inner], result[inner][column]));
      }
      result[row][column] = __mpf_complex_divide(value, factor.lower[row][row]);
    }
  }
  for (let reverse = size; reverse > 0; --reverse) {
    const row = reverse - 1;
    for (let column = 0; column < columns; ++column) {
      let value = result[row][column];
      for (let inner = row + 1; inner < size; ++inner) {
        value = __mpf_complex_subtract(value, __mpf_complex_multiply(
          __mpf_conj(factor.lower[inner][row]), result[inner][column]));
      }
      result[row][column] = __mpf_complex_divide(
        value, __mpf_conj(factor.lower[row][row]));
    }
  }
  return result;
}
function __mpf_matlab_complex_cholesky_rcond(coefficients, factor) {
  const inverse = __mpf_matlab_complex_cholesky_apply(
    factor, __mpf_matlab_complex_identity(coefficients.length));
  const denominator = __mpf_matlab_complex_matrix_one_norm(coefficients) *
    __mpf_matlab_complex_matrix_one_norm(inverse);
  return denominator === 0 || !Number.isFinite(denominator) ? 0 : 1 / denominator;
}
function __mpf_matlab_complex_cpqr_factor(values) {
  const rows = values.length;
  const columns = values[0].length;
  const pivots = Math.min(rows, columns);
  const qr = values.map((row) => row.slice());
  const tau = new Array(pivots).fill(0);
  const permutation = Array.from({ length: columns }, (_, index) => index);
  let scale = 0;
  for (let column = 0; column < columns; ++column) {
    let norm = 0;
    for (let row = 0; row < rows; ++row) norm = Math.hypot(norm, __mpf_abs(qr[row][column]));
    scale = Math.max(scale, norm);
  }
  const tolerance = Number.EPSILON * Math.max(rows, columns) * scale;
  let rank = 0;
  for (let pivot = 0; pivot < pivots; ++pivot) {
    let selected = pivot;
    let selectedNorm = -1;
    for (let column = pivot; column < columns; ++column) {
      let norm = 0;
      for (let row = pivot; row < rows; ++row) {
        norm = Math.hypot(norm, __mpf_abs(qr[row][column]));
      }
      if (norm > selectedNorm) {
        selected = column;
        selectedNorm = norm;
      }
    }
    if (selectedNorm <= tolerance) break;
    if (selected !== pivot) {
      for (let row = 0; row < rows; ++row) {
        [qr[row][pivot], qr[row][selected]] = [qr[row][selected], qr[row][pivot]];
      }
      [permutation[pivot], permutation[selected]] =
        [permutation[selected], permutation[pivot]];
    }
    const alpha = qr[pivot][pivot];
    const alphaMagnitude = __mpf_abs(alpha);
    const phase = alphaMagnitude === 0
      ? __mpf_complex(1, 0)
      : __mpf_complex(alpha.re / alphaMagnitude, alpha.im / alphaMagnitude);
    const beta = __mpf_complex(-phase.re * selectedNorm, -phase.im * selectedNorm);
    const leading = __mpf_complex_subtract(alpha, beta);
    tau[pivot] = 1 + alphaMagnitude / selectedNorm;
    qr[pivot][pivot] = beta;
    for (let row = pivot + 1; row < rows; ++row) {
      qr[row][pivot] = __mpf_complex_divide(qr[row][pivot], leading);
    }
    for (let column = pivot + 1; column < columns; ++column) {
      let projection = qr[pivot][column];
      for (let row = pivot + 1; row < rows; ++row) {
        projection = __mpf_complex_add(projection, __mpf_complex_multiply(
          __mpf_conj(qr[row][pivot]), qr[row][column]));
      }
      projection = __mpf_complex_multiply(projection, __mpf_complex(tau[pivot], 0));
      qr[pivot][column] = __mpf_complex_subtract(qr[pivot][column], projection);
      for (let row = pivot + 1; row < rows; ++row) {
        qr[row][column] = __mpf_complex_subtract(
          qr[row][column], __mpf_complex_multiply(qr[row][pivot], projection));
      }
    }
    rank = pivot + 1;
  }
  return { qr, tau, permutation, rank, tolerance };
}
function __mpf_matlab_apply_complex_cpqr_qh(factor, values) {
  const result = values.map((row) => row.slice());
  for (let pivot = 0; pivot < factor.rank; ++pivot) {
    for (let column = 0; column < result[0].length; ++column) {
      let projection = result[pivot][column];
      for (let row = pivot + 1; row < result.length; ++row) {
        projection = __mpf_complex_add(projection, __mpf_complex_multiply(
          __mpf_conj(factor.qr[row][pivot]), result[row][column]));
      }
      projection = __mpf_complex_multiply(
        projection, __mpf_complex(factor.tau[pivot], 0));
      result[pivot][column] = __mpf_complex_subtract(
        result[pivot][column], projection);
      for (let row = pivot + 1; row < result.length; ++row) {
        result[row][column] = __mpf_complex_subtract(
          result[row][column], __mpf_complex_multiply(factor.qr[row][pivot], projection));
      }
    }
  }
  return result;
}
function __mpf_matlab_complex_basic_least_squares(left, right) {
  const factor = __mpf_matlab_complex_cpqr_factor(left);
  const transformed = __mpf_matlab_apply_complex_cpqr_qh(factor, right);
  const columns = left[0].length;
  const outputs = right[0].length;
  if (factor.rank < Math.min(left.length, columns)) {
    console.warn(`MPF Matlab matrix is rank deficient to working precision (rank ${factor.rank}, ` +
      `tolerance ${factor.tolerance}); using a basic least-squares solution`);
  }
  const pivoted = Array.from({ length: columns }, () =>
    Array.from({ length: outputs }, () => __mpf_complex(0, 0)));
  for (let reverse = factor.rank; reverse > 0; --reverse) {
    const row = reverse - 1;
    for (let output = 0; output < outputs; ++output) {
      let value = transformed[row][output];
      for (let inner = row + 1; inner < factor.rank; ++inner) {
        value = __mpf_complex_subtract(value, __mpf_complex_multiply(
          factor.qr[row][inner], pivoted[inner][output]));
      }
      pivoted[row][output] = __mpf_complex_divide(value, factor.qr[row][row]);
    }
  }
  const result = Array.from({ length: columns }, () =>
    Array.from({ length: outputs }, () => __mpf_complex(0, 0)));
  for (let row = 0; row < columns; ++row) result[factor.permutation[row]] = pivoted[row];
  return result;
}
function __mpf_matlab_complex_rectangular_solve(leftValue, rightValue, solve) {
  const left = __mpf_matlab_complex_dense_matrix(
    leftValue, 'complex rectangular coefficient matrix');
  const right = __mpf_matlab_complex_dense_matrix(
    rightValue, 'complex rectangular right-hand side');
  if (right.length !== left.length) {
    throw new RangeError('MPF Matlab complex rectangular matrix solve shape mismatch');
  }
  if (solve === 'overdetermined' && left.length <= left[0].length) {
    throw new RangeError('MPF Matlab complex matrix solve disagrees with its overdetermined plan');
  }
  if (solve === 'underdetermined' && left.length >= left[0].length) {
    throw new RangeError('MPF Matlab complex matrix solve disagrees with its underdetermined plan');
  }
  return __mpf_matlab_complex_basic_least_squares(left, right);
}
function __mpf_matlab_mldivide_complex_overdetermined(left, right) {
  return __mpf_matlab_complex_rectangular_solve(left, right, 'overdetermined');
}
function __mpf_matlab_mldivide_complex_underdetermined(left, right) {
  return __mpf_matlab_complex_rectangular_solve(left, right, 'underdetermined');
}
function __mpf_matlab_complex_mrdivide_rectangular(leftValue, rightValue, solve) {
  const left = __mpf_matlab_complex_dense_matrix(leftValue, 'complex left operand');
  const right = __mpf_matlab_complex_dense_matrix(rightValue, 'complex right divisor');
  return __mpf_matlab_complex_ctranspose(__mpf_matlab_complex_rectangular_solve(
    __mpf_matlab_complex_ctranspose(right), __mpf_matlab_complex_ctranspose(left), solve));
}
function __mpf_matlab_mrdivide_complex_overdetermined(left, right) {
  return __mpf_matlab_complex_mrdivide_rectangular(left, right, 'overdetermined');
}
function __mpf_matlab_mrdivide_complex_underdetermined(left, right) {
  return __mpf_matlab_complex_mrdivide_rectangular(left, right, 'underdetermined');
}
function __mpf_matlab_structured_complex_square_solve(left, right) {
  if (__mpf_matlab_is_hermitian(left)) {
    const factor = __mpf_matlab_complex_cholesky_factor(left);
    if (factor.positiveDefinite) {
      __mpf_matlab_warn_square_condition(
        __mpf_matlab_complex_cholesky_rcond(left, factor));
      return __mpf_matlab_complex_cholesky_apply(factor, right);
    }
  }
  const factor = __mpf_matlab_complex_lu_factor(left);
  __mpf_matlab_warn_square_condition(__mpf_matlab_complex_lu_rcond(left, factor));
  return __mpf_matlab_complex_lu_apply(factor, right);
}
function __mpf_matlab_complex_square_solve(leftValue, rightValue) {
  const left = __mpf_matlab_complex_dense_matrix(leftValue, 'complex coefficient matrix');
  const right = __mpf_matlab_complex_dense_matrix(rightValue, 'complex right-hand side');
  if (left.length !== left[0].length || right.length !== left.length) {
    throw new RangeError('MPF Matlab complex matrix solve disagrees with its square plan');
  }
  return __mpf_matlab_structured_complex_square_solve(left, right);
}
function __mpf_matlab_mldivide_structured_complex_square(left, right) {
  return __mpf_matlab_complex_square_solve(left, right);
}
function __mpf_matlab_mrdivide_structured_complex_square(leftValue, rightValue) {
  const left = __mpf_matlab_complex_dense_matrix(leftValue, 'complex left operand');
  const right = __mpf_matlab_complex_dense_matrix(rightValue, 'complex right divisor');
  return __mpf_matlab_complex_ctranspose(__mpf_matlab_complex_square_solve(
    __mpf_matlab_complex_ctranspose(right), __mpf_matlab_complex_ctranspose(left)));
}
function __mpf_matlab_complex_mpower(values, exponent) {
  let factor = __mpf_matlab_complex_dense_matrix(values, 'complex matrix power base');
  if (factor.length !== factor[0].length) {
    throw new RangeError('MPF Matlab complex matrix power requires a square matrix');
  }
  if (!Number.isSafeInteger(exponent)) {
    throw new RangeError('MPF Matlab complex matrix power requires a safe integer exponent');
  }
  let result = __mpf_matlab_complex_identity(factor.length);
  if (exponent < 0) {
    factor = __mpf_matlab_structured_complex_square_solve(factor, result);
    exponent = -exponent;
  }
  while (exponent > 0) {
    if (exponent % 2 === 1) result = __mpf_matlab_complex_mtimes(result, factor);
    exponent = Math.floor(exponent / 2);
    if (exponent > 0) factor = __mpf_matlab_complex_mtimes(factor, factor);
  }
  return result;
}
)MPF";
}

}  // namespace mpf::detail
