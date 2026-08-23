#include "backends/javascript/argument_validation_runtime.hpp"

#include <ostream>

namespace mpf::detail {

void emit_javascript_argument_validation_runtime(std::ostream& output) {
  output << R"js(function __mpf_argument_map(value, transform) {
  if (!Array.isArray(value)) return transform(value);
  const result = value.map((item) => __mpf_argument_map(item, transform));
  const shape = value[__mpf_shape_tag];
  return Array.isArray(shape) ? __mpf_record_shape(result, shape) : result;
}
function __mpf_argument_flatten(value, result = []) {
  if (Array.isArray(value)) {
    for (const item of value) __mpf_argument_flatten(item, result);
  } else {
    result.push(value);
  }
  return result;
}
function __mpf_argument_shape(value, name) {
  if (typeof value === 'string') return [1, value.length];
  if (!Array.isArray(value)) return [1, 1];
  const shape = __mpf_matlab_runtime_shape(value, `argument '${name}'`);
  return shape.length === 1 ? [1, shape[0]] : shape;
}
function __mpf_argument_size(shape) {
  return __mpf_checked_shape_size(shape);
}
function __mpf_argument_failure(name, requirement) {
  throw new TypeError(`MPF Matlab argument '${name}' failed ${requirement}`);
}
const __mpf_argument_validator_names = [
  'mustBeNumeric', 'mustBeNumericOrLogical', 'mustBeFloat', 'mustBeReal',
  'mustBeFinite', 'mustBeNonNan', 'mustBePositive', 'mustBeNonpositive',
  'mustBeNonnegative', 'mustBeNegative', 'mustBeNonzero', 'mustBeInteger',
  'mustBeNonempty', 'mustBeScalarOrEmpty', 'mustBeVector', 'mustBeRow',
  'mustBeColumn', 'mustBeMatrix', 'mustBeNonmissing', 'mustBeNonzeroLengthText',
  'mustBeText', 'mustBeTextScalar', 'mustBeValidVariableName'
];
const __mpf_matlab_keywords = new Set([
  'break', 'case', 'catch', 'classdef', 'continue', 'else', 'elseif', 'end',
  'for', 'function', 'global', 'if', 'otherwise', 'parfor', 'persistent',
  'return', 'spmd', 'switch', 'try', 'while'
]);
function __mpf_argument_resolve_shape(source, dimensions, name, direction) {
  if (dimensions.length === 0) return source;
  const sourceSize = __mpf_argument_size(source);
  const target = dimensions.map((extent) => extent < 0 ? 0 : extent);
  const variableAxes = [];
  let sameRankMatch = source.length === dimensions.length;
  for (let axis = 0; axis < dimensions.length; ++axis) {
    if (dimensions[axis] < 0) {
      variableAxes.push(axis);
      if (sameRankMatch) target[axis] = source[axis];
    } else if (sameRankMatch && source[axis] !== dimensions[axis]) {
      sameRankMatch = false;
    }
  }
  if (sameRankMatch) return target;
  if (variableAxes.length === 1) {
    let fixed = 1;
    for (let axis = 0; axis < target.length; ++axis) {
      if (axis === variableAxes[0]) continue;
      if (target[axis] !== 0 && fixed > Number.MAX_SAFE_INTEGER / target[axis])
        __mpf_argument_failure(name, `${direction} size validation`);
      fixed *= target[axis];
    }
    if (fixed === 0) {
      if (sourceSize !== 0) __mpf_argument_failure(name, `${direction} size validation`);
      target[variableAxes[0]] = 0;
    } else if (sourceSize % fixed === 0) {
      target[variableAxes[0]] = sourceSize / fixed;
    }
  } else if (variableAxes.length !== 0) {
    __mpf_argument_failure(name, `${direction} size validation`);
  }
  const targetSize = __mpf_argument_size(target);
  if (targetSize !== sourceSize && sourceSize !== 1)
    __mpf_argument_failure(name, `${direction} size validation`);
  return target;
}
function __mpf_argument_convert_size(value, name, direction, dimensions) {
  const source = __mpf_argument_shape(value, name);
  const target = __mpf_argument_resolve_shape(source, dimensions, name, direction);
  if (typeof value === 'string') {
    if (source.length !== target.length || source.some((extent, axis) => extent !== target[axis]))
      __mpf_argument_failure(name, `${direction} size validation`);
    return value;
  }
  const unchanged = source.length === target.length &&
                    source.every((extent, axis) => extent === target[axis]);
  if (unchanged) return value;
  const flattened = Array.isArray(value) ? __mpf_flatten_column_major(value) : [value];
  const targetSize = __mpf_argument_size(target);
  const converted = flattened.length === 1 && targetSize !== 1
    ? Array(targetSize).fill(flattened[0]) : flattened;
  return __mpf_build_column_major(converted, target);
}
function __mpf_validate_argument(value, name, direction, dimensions, classConstraint, validators) {
  if (classConstraint === 1) {
    value = __mpf_argument_map(value, (item) => {
      if (__mpf_is_complex(item)) return item;
      if (typeof item !== 'number' && typeof item !== 'boolean')
        __mpf_argument_failure(name, 'double class conversion');
      return Number(item);
    });
  } else if (classConstraint === 2) {
    value = __mpf_argument_map(value, (item) => {
      if (__mpf_is_complex(item)) return item.re !== 0 || item.im !== 0;
      if (typeof item !== 'number' && typeof item !== 'boolean')
        __mpf_argument_failure(name, 'logical class conversion');
      return Boolean(item);
    });
  } else if (classConstraint === 3 && typeof value !== 'string') {
    __mpf_argument_failure(name, 'char class conversion');
  }
  value = __mpf_argument_convert_size(value, name, direction, dimensions);
  const shape = __mpf_argument_shape(value, name);
  const items = typeof value === 'string' ? [value] : __mpf_argument_flatten(value);
  const empty = __mpf_argument_size(shape) === 0;
  const numeric = (item) => typeof item === 'number' || __mpf_is_complex(item);
  const numericOrLogical = (item) => numeric(item) || typeof item === 'boolean';
  for (const validator of validators) {
    let valid = true;
    switch (validator) {
      case 0: valid = empty || items.every(numeric); break;
      case 1: valid = empty || items.every(numericOrLogical); break;
      case 2: valid = empty || items.every(numeric); break;
      case 3: valid = empty || typeof value === 'string' ||
        items.every((item) => numericOrLogical(item) && !__mpf_is_complex(item)); break;
      case 4: valid = empty || typeof value === 'string' || items.every((item) =>
        __mpf_is_complex(item)
          ? Number.isFinite(item.re) && Number.isFinite(item.im)
          : numericOrLogical(item) && Number.isFinite(Number(item))); break;
      case 5: valid = empty || typeof value === 'string' || items.every((item) =>
        __mpf_is_complex(item)
          ? !Number.isNaN(item.re) && !Number.isNaN(item.im)
          : numericOrLogical(item) && !Number.isNaN(Number(item))); break;
      case 6: valid = empty || items.every((item) => !__mpf_is_complex(item) &&
        numericOrLogical(item) && Number(item) > 0); break;
      case 7: valid = empty || items.every((item) => !__mpf_is_complex(item) &&
        numericOrLogical(item) && Number(item) <= 0); break;
      case 8: valid = empty || items.every((item) => !__mpf_is_complex(item) &&
        numericOrLogical(item) && Number(item) >= 0); break;
      case 9: valid = empty || items.every((item) => !__mpf_is_complex(item) &&
        numericOrLogical(item) && Number(item) < 0); break;
      case 10: valid = empty || items.every((item) => __mpf_is_complex(item)
        ? item.re !== 0 || item.im !== 0
        : numericOrLogical(item) && Number(item) !== 0); break;
      case 11: valid = empty || items.every((item) => !__mpf_is_complex(item) &&
        numericOrLogical(item) && Number.isFinite(Number(item)) &&
        Number.isInteger(Number(item))); break;
      case 12: valid = __mpf_argument_size(shape) !== 0; break;
      case 13: { const size = __mpf_argument_size(shape); valid = size === 0 || size === 1; break; }
      case 14: valid = shape.length === 2 && (shape[0] === 1 || shape[1] === 1); break;
      case 15: valid = shape.length === 2 && shape[0] === 1; break;
      case 16: valid = shape.length === 2 && shape[1] === 1; break;
      case 17: valid = shape.length === 2; break;
      case 18: valid = items.every((item) => __mpf_is_complex(item)
        ? !Number.isNaN(item.re) && !Number.isNaN(item.im)
        : typeof item !== 'number' || !Number.isNaN(item)); break;
      case 19: valid = typeof value === 'string' && value.length !== 0; break;
      case 20: valid = typeof value === 'string'; break;
      case 21: valid = typeof value === 'string'; break;
      case 22: valid = typeof value === 'string' && value.length <= 63 &&
        /^[A-Za-z][A-Za-z0-9_]*$/.test(value) && !__mpf_matlab_keywords.has(value); break;
      default: valid = false; break;
    }
    if (!valid) __mpf_argument_failure(
      name, __mpf_argument_validator_names[validator] ?? `unknown validator ${validator}`);
  }
  return value;
}

)js";
}

}  // namespace mpf::detail
