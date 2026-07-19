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
function __mpf_sparse_from_dense(value) {
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
function __mpf_sparse_dimension(value, name, allowZero = false) {
  if (typeof value !== 'number' || !Number.isSafeInteger(value) ||
      (allowZero ? value < 0 : value <= 0)) {
    throw new RangeError(`MPF Matlab ${name} must be a ${allowZero ? 'nonnegative' : 'positive'} safe integer`);
  }
  return value;
}
function __mpf_sparse_triplet_input(value, name) {
  const sequence = Array.isArray(value);
  const flattened = sequence ? __mpf_flatten_column_major(value) : [value];
  for (const item of flattened) if (typeof item !== 'number' || !Number.isFinite(item)) {
    throw new TypeError(`MPF Matlab sparse ${name} requires finite real values`);
  }
  return { sequence, flattened };
}
function __mpf_sparse_from_triplets(rowValue, columnValue, storedValue,
                                    explicitRows, explicitColumns, reserveHint) {
  const rowInput = __mpf_sparse_triplet_input(rowValue, 'row indices');
  const columnInput = __mpf_sparse_triplet_input(columnValue, 'column indices');
  const valueInput = __mpf_sparse_triplet_input(storedValue, 'stored values');
  const inputs = [rowInput, columnInput, valueInput];
  let sequenceCount;
  for (const input of inputs) if (input.sequence) {
    if (sequenceCount !== undefined && sequenceCount !== input.flattened.length) {
      throw new RangeError('MPF Matlab nonscalar sparse triplets must have equal element counts');
    }
    sequenceCount = input.flattened.length;
  }
  const count = sequenceCount ?? 1;
  const valueAt = (input, index) => input.sequence ? input.flattened[index] : input.flattened[0];
  const entries = new Array(count); let inferredRows = 0; let inferredColumns = 0;
  for (let index = 0; index < count; ++index) {
    const row = valueAt(rowInput, index); const column = valueAt(columnInput, index);
    const value = valueAt(valueInput, index);
    if (!Number.isSafeInteger(row) || row <= 0 || !Number.isSafeInteger(column) || column <= 0) {
      throw new RangeError('MPF Matlab sparse triplet indices must be positive safe integers');
    }
    inferredRows = Math.max(inferredRows, row); inferredColumns = Math.max(inferredColumns, column);
    entries[index] = { row: row - 1, column: column - 1, value };
  }
  const rows = explicitRows === undefined
    ? __mpf_sparse_dimension(inferredRows, 'inferred row extent')
    : __mpf_sparse_dimension(explicitRows, 'row extent');
  const columns = explicitColumns === undefined
    ? __mpf_sparse_dimension(inferredColumns, 'inferred column extent')
    : __mpf_sparse_dimension(explicitColumns, 'column extent');
  if (reserveHint !== undefined) __mpf_sparse_dimension(reserveHint, 'nzmax', true);
  for (const entry of entries) if (entry.row >= rows || entry.column >= columns) {
    throw new RangeError('MPF Matlab sparse triplet index exceeds the requested dimensions');
  }
  entries.sort((left, right) => left.column - right.column || left.row - right.row);
  const columnPointers = [0]; const rowIndices = []; const values = [];
  let entry = 0;
  for (let column = 0; column < columns; ++column) {
    while (entry < entries.length && entries[entry].column === column) {
      const row = entries[entry].row; let sum = 0;
      while (entry < entries.length && entries[entry].column === column &&
             entries[entry].row === row) sum += entries[entry++].value;
      if (!Number.isFinite(sum)) {
        throw new RangeError('MPF Matlab sparse duplicate accumulation is not finite');
      }
      if (sum !== 0) { rowIndices.push(row); values.push(sum); }
    }
    columnPointers.push(values.length);
  }
  return __mpf_make_sparse_csc(rows, columns, columnPointers, rowIndices, values);
}
function __mpf_sparse(...args) {
  if (args.length === 1) return __mpf_sparse_from_dense(args[0]);
  if (args.length === 2) {
    const rows = __mpf_sparse_dimension(args[0], 'row extent');
    const columns = __mpf_sparse_dimension(args[1], 'column extent');
    return __mpf_make_sparse_csc(rows, columns, new Array(columns + 1).fill(0), [], []);
  }
  if (args.length === 3) return __mpf_sparse_from_triplets(args[0], args[1], args[2]);
  if (args.length === 5) {
    return __mpf_sparse_from_triplets(args[0], args[1], args[2], args[3], args[4]);
  }
  if (args.length === 6) {
    return __mpf_sparse_from_triplets(args[0], args[1], args[2], args[3], args[4], args[5]);
  }
  throw new TypeError('MPF Matlab sparse received an unsupported argument contract');
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
function __mpf_sparse_value_at(matrix, row, column) {
  let first = matrix.columnPointers[column]; let last = matrix.columnPointers[column + 1];
  while (first < last) {
    const middle = first + Math.floor((last - first) / 2);
    const candidate = matrix.rowIndices[middle];
    if (candidate < row) first = middle + 1; else last = middle;
  }
  return first < matrix.columnPointers[column + 1] && matrix.rowIndices[first] === row
    ? matrix.values[first] : 0;
}
function __mpf_sparse_linear_shape(shape, count) {
  if (!Array.isArray(shape) || shape.length !== 2) {
    throw new RangeError('MPF Matlab sparse linear index has an invalid result-shape plan');
  }
  const resolved = shape.slice(); const missing = [];
  resolved.forEach((extent, axis) => {
    if (extent === null) missing.push(axis);
    else if (!Number.isSafeInteger(extent) || extent < 0) {
      throw new RangeError('MPF Matlab sparse index result extent is invalid');
    }
  });
  if (missing.length > 1) {
    throw new RangeError('MPF Matlab sparse linear index result shape is underdetermined');
  }
  if (missing.length === 1) {
    const known = resolved[1 - missing[0]];
    if (known === 0 ? count !== 0 : count % known !== 0) {
      throw new RangeError('MPF Matlab sparse linear index result shape is inconsistent');
    }
    resolved[missing[0]] = known === 0 ? 0 : count / known;
  }
  const size = resolved[0] * resolved[1];
  if (!Number.isSafeInteger(size) || size !== count) {
    throw new RangeError('MPF Matlab sparse linear index result shape is inconsistent');
  }
  return resolved;
}
function __mpf_sparse_submatrix_shape(shape, rowCount, columnCount) {
  if (!Array.isArray(shape) || shape.length !== 2) {
    throw new RangeError('MPF Matlab sparse submatrix has an invalid result-shape plan');
  }
  const actual = [rowCount, columnCount];
  for (let axis = 0; axis < 2; ++axis) {
    if (shape[axis] !== null && shape[axis] !== actual[axis]) {
      throw new RangeError('MPF Matlab sparse submatrix result shape is inconsistent');
    }
  }
  return actual;
}
function __mpf_sparse_is_full_slice(selector) {
  return selector !== null && typeof selector === 'object' && selector.kind === 'slice' &&
    selector.start === null && selector.stop === null && selector.step === null &&
    selector.inclusive === true;
}
function __mpf_sparse_linear_element(value, selector) {
  const matrix = __mpf_validate_sparse_csc(value, 'linear index operand');
  const size = matrix.rows * matrix.columns;
  if (!Number.isSafeInteger(size)) {
    throw new RangeError('MPF Matlab sparse linear index extent exceeds safe integer limits');
  }
  const indices = __mpf_selector_indices(size, __mpf_resolve_extent(selector, size), 1, false);
  if (indices.length !== 1) {
    throw new RangeError('MPF Matlab sparse scalar linear index selected multiple elements');
  }
  const linear = indices[0];
  return __mpf_sparse_value_at(matrix, linear % matrix.rows, Math.floor(linear / matrix.rows));
}
function __mpf_sparse_subscript_element(value, rowSelector, columnSelector) {
  const matrix = __mpf_validate_sparse_csc(value, 'subscript operand');
  const rows = __mpf_selector_indices(
    matrix.rows, __mpf_resolve_extent(rowSelector, matrix.rows), 1, false);
  const columns = __mpf_selector_indices(
    matrix.columns, __mpf_resolve_extent(columnSelector, matrix.columns), 1, false);
  if (rows.length !== 1 || columns.length !== 1) {
    throw new RangeError('MPF Matlab sparse scalar subscript selected multiple elements');
  }
  return __mpf_sparse_value_at(matrix, rows[0], columns[0]);
}
function __mpf_sparse_linear_selection(value, selector, resultShape) {
  const matrix = __mpf_validate_sparse_csc(value, 'linear selection operand');
  const size = matrix.rows * matrix.columns;
  if (!Number.isSafeInteger(size)) {
    throw new RangeError('MPF Matlab sparse linear selection extent exceeds safe integer limits');
  }
  const resolvedSelector = __mpf_resolve_extent(selector, size);
  if (__mpf_sparse_is_full_slice(resolvedSelector)) {
    const [rows, columns] = __mpf_sparse_linear_shape(resultShape, size);
    if (rows !== size || columns !== 1) {
      throw new RangeError('MPF Matlab sparse full-colon result shape is inconsistent');
    }
    const rowIndices = [];
    for (let column = 0; column < matrix.columns; ++column) {
      const offset = column * matrix.rows;
      for (let stored = matrix.columnPointers[column];
           stored < matrix.columnPointers[column + 1]; ++stored) {
        rowIndices.push(offset + matrix.rowIndices[stored]);
      }
    }
    return __mpf_make_sparse_csc(
      rows, columns, [0, matrix.values.length], rowIndices, matrix.values.slice());
  }
  const indices = __mpf_selector_indices(size, resolvedSelector, 1, false);
  const [rows, columns] = __mpf_sparse_linear_shape(resultShape, indices.length);
  const pointers = [0]; const rowIndices = []; const values = [];
  for (let column = 0; column < columns; ++column) {
    for (let row = 0; row < rows; ++row) {
      const linear = indices[row + column * rows];
      const stored = __mpf_sparse_value_at(
        matrix, linear % matrix.rows, Math.floor(linear / matrix.rows));
      if (stored !== 0) { rowIndices.push(row); values.push(stored); }
    }
    pointers.push(values.length);
  }
  return __mpf_make_sparse_csc(rows, columns, pointers, rowIndices, values);
}
function __mpf_sparse_submatrix_selection(value, rowSelector, columnSelector, resultShape) {
  const matrix = __mpf_validate_sparse_csc(value, 'submatrix operand');
  const selectedRows = __mpf_selector_indices(
    matrix.rows, __mpf_resolve_extent(rowSelector, matrix.rows), 1, false);
  const selectedColumns = __mpf_selector_indices(
    matrix.columns, __mpf_resolve_extent(columnSelector, matrix.columns), 1, false);
  const [rows, columns] = __mpf_sparse_submatrix_shape(
    resultShape, selectedRows.length, selectedColumns.length);
  const rowMap = selectedRows.map((sourceRow, outputRow) => ({ sourceRow, outputRow }));
  rowMap.sort((left, right) => left.sourceRow - right.sourceRow || left.outputRow - right.outputRow);
  const pointers = [0]; const rowIndices = []; const values = [];
  for (let column = 0; column < columns; ++column) {
    const sourceColumn = selectedColumns[column];
    let selected = 0; let stored = matrix.columnPointers[sourceColumn];
    const storedEnd = matrix.columnPointers[sourceColumn + 1]; const entries = [];
    while (selected < rowMap.length && stored < storedEnd) {
      const selectedRow = rowMap[selected].sourceRow;
      const storedRow = matrix.rowIndices[stored];
      if (selectedRow < storedRow) { ++selected; continue; }
      if (storedRow < selectedRow) { ++stored; continue; }
      const value = matrix.values[stored++];
      while (selected < rowMap.length && rowMap[selected].sourceRow === selectedRow) {
        entries.push({ row: rowMap[selected++].outputRow, value });
      }
    }
    entries.sort((left, right) => left.row - right.row);
    for (const entry of entries) { rowIndices.push(entry.row); values.push(entry.value); }
    pointers.push(values.length);
  }
  return __mpf_make_sparse_csc(rows, columns, pointers, rowIndices, values);
}
function __mpf_sparse_nonsingleton_shape(shape) {
  return shape.filter((extent) => extent !== 1);
}
function __mpf_sparse_same_shape(left, right) {
  return left.length === right.length && left.every((extent, axis) => extent === right[axis]);
}
function __mpf_sparse_replacement_payload(value) {
  if (__mpf_issparse(value)) {
    const matrix = __mpf_validate_sparse_csc(value, 'assignment replacement');
    const size = matrix.rows * matrix.columns;
    if (!Number.isSafeInteger(size)) {
      throw new RangeError('MPF Matlab sparse replacement exceeds safe integer limits');
    }
    const flattened = new Array(size).fill(0);
    for (let column = 0; column < matrix.columns; ++column) {
      for (let stored = matrix.columnPointers[column];
           stored < matrix.columnPointers[column + 1]; ++stored) {
        flattened[matrix.rowIndices[stored] + column * matrix.rows] = matrix.values[stored];
      }
    }
    return { shape: [matrix.rows, matrix.columns], flattened };
  }
  if (Array.isArray(value)) {
    const shape = __mpf_matlab_runtime_shape(value, 'sparse assignment replacement');
    const flattened = __mpf_flatten_column_major(value).map((item) => {
      if ((typeof item !== 'number' && typeof item !== 'boolean') ||
          (typeof item === 'number' && !Number.isFinite(item))) {
        throw new TypeError('MPF Matlab sparse assignment requires finite real values');
      }
      return Number(item);
    });
    return { shape, flattened };
  }
  if ((typeof value !== 'number' && typeof value !== 'boolean') ||
      (typeof value === 'number' && !Number.isFinite(value))) {
    throw new TypeError('MPF Matlab sparse assignment requires finite real values');
  }
  return { shape: [], flattened: [Number(value)] };
}
function __mpf_sparse_assignment_payload(value, scalarExpansion, selectionShape, count) {
  const payload = __mpf_sparse_replacement_payload(value);
  if (scalarExpansion) {
    if (payload.flattened.length !== 1) {
      throw new RangeError('MPF Matlab sparse scalar expansion requires one replacement value');
    }
    return { scalar: true, values: payload.flattened };
  }
  if (payload.flattened.length !== count ||
      !__mpf_sparse_same_shape(__mpf_sparse_nonsingleton_shape(payload.shape),
                               __mpf_sparse_nonsingleton_shape(selectionShape))) {
    throw new RangeError('MPF Matlab sparse assignment replacement shape mismatch');
  }
  return { scalar: false, values: payload.flattened };
}
function __mpf_sparse_verify_result_shape(planned, actual, operation) {
  if (planned !== undefined && !__mpf_sparse_same_shape(planned, actual)) {
    throw new RangeError(`MPF Matlab sparse ${operation} result shape disagrees with lowering`);
  }
}
function __mpf_sparse_commit(value, rows, columns, columnPointers, rowIndices, values) {
  const result = __mpf_make_sparse_csc(rows, columns, columnPointers, rowIndices, values);
  value.rows = result.rows; value.columns = result.columns;
  value.columnPointers = result.columnPointers; value.rowIndices = result.rowIndices;
  value.values = result.values;
  return value;
}
function __mpf_sparse_assign(value, selectors, replacement, base, linear, scalarExpansion,
                             plannedSelectionShape, plannedResultShape) {
  const matrix = __mpf_validate_sparse_csc(value, 'assignment target');
  if (!Array.isArray(selectors) || selectors.length !== (linear ? 1 : 2)) {
    throw new RangeError('MPF Matlab sparse assignment selector arity mismatch');
  }
  let rows = matrix.rows; let columns = matrix.columns; let selectedRows; let selectedColumns;
  let selectionShape; const coordinates = [];
  if (linear) {
    const size = matrix.rows * matrix.columns;
    if (!Number.isSafeInteger(size)) {
      throw new RangeError('MPF Matlab sparse linear assignment exceeds safe integer limits');
    }
    const resolved = __mpf_resolve_extent(selectors[0], size);
    const required = __mpf_growth_extent(size, resolved, base);
    if (matrix.rows === 1) columns = required;
    else if (matrix.columns === 1) rows = required;
    else columns = Math.max(matrix.columns, Math.ceil(required / matrix.rows));
    const grownSize = rows * columns;
    if (!Number.isSafeInteger(grownSize)) {
      throw new RangeError('MPF Matlab sparse growth exceeds safe integer limits');
    }
    const indices = __mpf_selector_indices(grownSize, resolved, base, false);
    selectionShape = plannedSelectionShape.length === 0
      ? [1, 1] : __mpf_sparse_linear_shape(plannedSelectionShape, indices.length);
    for (const index of indices) {
      coordinates.push({ row: index % rows, column: Math.floor(index / rows) });
    }
  } else {
    const resolvedRows = __mpf_resolve_extent(selectors[0], matrix.rows);
    const resolvedColumns = __mpf_resolve_extent(selectors[1], matrix.columns);
    rows = __mpf_growth_extent(matrix.rows, resolvedRows, base);
    columns = __mpf_growth_extent(matrix.columns, resolvedColumns, base);
    selectedRows = __mpf_selector_indices(rows, resolvedRows, base, false);
    selectedColumns = __mpf_selector_indices(columns, resolvedColumns, base, false);
    selectionShape = plannedSelectionShape.length === 0
      ? [1, 1]
      : __mpf_sparse_submatrix_shape(
          plannedSelectionShape, selectedRows.length, selectedColumns.length);
    for (const column of selectedColumns) {
      for (const row of selectedRows) coordinates.push({ row, column });
    }
  }
  __mpf_sparse_verify_result_shape(plannedResultShape, [rows, columns], 'assignment');
  const payload = __mpf_sparse_assignment_payload(
    replacement, scalarExpansion, selectionShape, coordinates.length);
  const updates = coordinates.map((coordinate, sequence) => ({
    ...coordinate, sequence, value: payload.values[payload.scalar ? 0 : sequence]
  }));
  updates.sort((left, right) => left.column - right.column || left.row - right.row ||
    left.sequence - right.sequence);
  const collapsed = [];
  for (const update of updates) {
    const previous = collapsed[collapsed.length - 1];
    if (previous !== undefined && previous.row === update.row &&
        previous.column === update.column) collapsed[collapsed.length - 1] = update;
    else collapsed.push(update);
  }
  const columnPointers = [0]; const rowIndices = []; const values = []; let update = 0;
  for (let column = 0; column < columns; ++column) {
    let stored = column < matrix.columns ? matrix.columnPointers[column]
                                         : matrix.values.length;
    const storedEnd = column < matrix.columns ? matrix.columnPointers[column + 1]
                                               : matrix.values.length;
    while ((update < collapsed.length && collapsed[update].column === column) ||
           stored < storedEnd) {
      const changed = update < collapsed.length && collapsed[update].column === column
        ? collapsed[update] : undefined;
      const changedRow = changed === undefined ? rows : changed.row;
      const storedRow = stored < storedEnd ? matrix.rowIndices[stored] : rows;
      if (storedRow < changedRow) {
        rowIndices.push(storedRow); values.push(matrix.values[stored++]);
      } else if (changedRow < storedRow) {
        if (changed.value !== 0) { rowIndices.push(changedRow); values.push(changed.value); }
        ++update;
      } else {
        if (changed.value !== 0) { rowIndices.push(changedRow); values.push(changed.value); }
        ++stored; ++update;
      }
    }
    columnPointers.push(values.length);
  }
  return __mpf_sparse_commit(
    value, rows, columns, columnPointers, rowIndices, values);
}
function __mpf_sparse_erase(value, selectors, base, linear, axis, plannedResultShape) {
  const matrix = __mpf_validate_sparse_csc(value, 'deletion target');
  if (!Array.isArray(selectors) || selectors.length !== (linear ? 1 : 2)) {
    throw new RangeError('MPF Matlab sparse deletion selector arity mismatch');
  }
  let deletionAxis = axis;
  if (linear) {
    const expectedAxis = matrix.rows === 1 ? 1 : matrix.columns === 1 ? 0 : -1;
    if (expectedAxis < 0 || deletionAxis !== expectedAxis) {
      throw new RangeError('MPF Matlab sparse linear deletion requires a vector');
    }
  }
  if (deletionAxis !== 0 && deletionAxis !== 1) {
    throw new RangeError('MPF Matlab sparse deletion axis is invalid');
  }
  const extent = deletionAxis === 0 ? matrix.rows : matrix.columns;
  const selector = __mpf_resolve_extent(selectors[linear ? 0 : deletionAxis], extent);
  const removed = [...new Set(__mpf_selector_indices(extent, selector, base, false))]
    .sort((left, right) => left - right);
  const rows = matrix.rows - (deletionAxis === 0 ? removed.length : 0);
  const columns = matrix.columns - (deletionAxis === 1 ? removed.length : 0);
  __mpf_sparse_verify_result_shape(plannedResultShape, [rows, columns], 'deletion');
  const removedSet = new Set(removed); const columnPointers = [0];
  const rowIndices = []; const values = [];
  if (deletionAxis === 1) {
    for (let column = 0; column < matrix.columns; ++column) {
      if (removedSet.has(column)) continue;
      for (let stored = matrix.columnPointers[column];
           stored < matrix.columnPointers[column + 1]; ++stored) {
        rowIndices.push(matrix.rowIndices[stored]); values.push(matrix.values[stored]);
      }
      columnPointers.push(values.length);
    }
  } else {
    const rowMap = new Array(matrix.rows); let nextRow = 0;
    for (let row = 0; row < matrix.rows; ++row) {
      rowMap[row] = removedSet.has(row) ? -1 : nextRow++;
    }
    for (let column = 0; column < matrix.columns; ++column) {
      for (let stored = matrix.columnPointers[column];
           stored < matrix.columnPointers[column + 1]; ++stored) {
        const row = rowMap[matrix.rowIndices[stored]];
        if (row >= 0) { rowIndices.push(row); values.push(matrix.values[stored]); }
      }
      columnPointers.push(values.length);
    }
  }
  return __mpf_sparse_commit(
    value, rows, columns, columnPointers, rowIndices, values);
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
