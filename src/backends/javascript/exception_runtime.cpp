#include "exception_runtime.hpp"

#include <ostream>

namespace mpf::detail {

void emit_javascript_exception_runtime(std::ostream& output) {
  output << R"MPFJS(const __mpf_exception_tag = Symbol('mpf.matlab.exception');
function __mpf_valid_exception_identifier(value) {
  return typeof value === 'string' &&
    /^[A-Za-z][A-Za-z0-9_]*(?::[A-Za-z][A-Za-z0-9_]*)+$/.test(value);
}
function __mpf_decode_format_escapes(value) {
  return value.replace(/\\([\\nrtbf])/g, (_, escape) => {
    if (escape === 'n') return '\n';
    if (escape === 'r') return '\r';
    if (escape === 't') return '\t';
    if (escape === 'b') return '\b';
    if (escape === 'f') return '\f';
    return '\\';
  });
}
function __mpf_normalize_format_exponent(value) {
  return value.replace(/([eE][+-])(\d)$/, (_, sign, digit) => `${sign}0${digit}`);
}
function __mpf_format_exception_message(format, values) {
  if (typeof format !== 'string') throw new TypeError('MPF Matlab error message must be text');
  let result = '';
  let argument = 0;
  for (let index = 0; index < format.length;) {
    if (format[index] !== '%') { result += format[index++]; continue; }
    if (index + 1 < format.length && format[index + 1] === '%') {
      result += '%'; index += 2; continue;
    }
    const begin = index++;
    let flags = '';
    while (index < format.length && '-+ 0#'.includes(format[index])) flags += format[index++];
    let widthText = '';
    while (index < format.length && /[0-9]/.test(format[index])) widthText += format[index++];
    let precision;
    if (format[index] === '.') {
      ++index; let precisionText = '';
      while (index < format.length && /[0-9]/.test(format[index])) {
        precisionText += format[index++];
      }
      precision = precisionText.length === 0 ? 0 : Number(precisionText);
    }
    const width = widthText.length === 0 ? 0 : Number(widthText);
    if (!Number.isSafeInteger(width) || width > 65536 ||
        (precision !== undefined && (!Number.isSafeInteger(precision) || precision > 65536))) {
      throw new RangeError('MPF Matlab format width or precision exceeds the runtime limit');
    }
    if (index >= format.length) throw new TypeError('MPF Matlab format specifier is incomplete');
    const conversion = format[index++];
    if (!'diuoxXfFeEgGcs'.includes(conversion)) {
      throw new TypeError(`MPF Matlab format specifier is unsupported: ${format.slice(begin, index)}`);
    }
    if (argument >= values.length) throw new TypeError('MPF Matlab format argument is missing');
    const value = values[argument++];
    let rendered;
    if (conversion === 's') {
      if (typeof value !== 'string') {
        throw new TypeError('MPF Matlab string format requires text');
      }
      rendered = value;
      if (precision !== undefined) rendered = rendered.slice(0, precision);
    } else if (conversion === 'c') {
      if (typeof value === 'string' && value.length !== 0) rendered = value[0];
      else if ((typeof value === 'number' || typeof value === 'boolean') &&
               Number.isInteger(Number(value)) && Number(value) >= 0 && Number(value) <= 255) {
        rendered = String.fromCodePoint(Number(value));
      } else {
        throw new TypeError('MPF Matlab character format requires text or an unsigned byte');
      }
    } else {
      if (typeof value !== 'number' && typeof value !== 'boolean') {
        throw new TypeError('MPF Matlab numeric format requires a number');
      }
      const numeric = Number(value);
      if ('diuoxX'.includes(conversion)) {
        const integral = Math.trunc(numeric);
        if (!Number.isFinite(numeric) || !Number.isSafeInteger(integral) ||
            ('uoxX'.includes(conversion) && integral < 0)) {
          throw new RangeError('MPF Matlab integer format requires a representable scalar');
        }
        if (conversion === 'o') rendered = integral.toString(8);
        else if (conversion === 'x' || conversion === 'X') {
          rendered = integral.toString(16);
          if (conversion === 'X') rendered = rendered.toUpperCase();
        } else rendered = String(integral);
        if (flags.includes('#') && integral !== 0) {
          if (conversion === 'o') rendered = `0${rendered}`;
          else if (conversion === 'x') rendered = `0x${rendered}`;
          else if (conversion === 'X') rendered = `0X${rendered}`;
        }
      } else if (!Number.isFinite(numeric)) {
        rendered = Number.isNaN(numeric) ? 'NaN' : `${numeric < 0 ? '-' : ''}Inf`;
      } else if (conversion === 'f' || conversion === 'F') {
        rendered = numeric.toFixed(precision === undefined ? 6 : precision);
        if (flags.includes('#') && !rendered.includes('.')) rendered += '.';
      } else if (conversion === 'e' || conversion === 'E') {
        rendered = numeric.toExponential(precision === undefined ? 6 : precision);
        if (flags.includes('#') && !rendered.includes('.')) {
          rendered = rendered.replace(/e/i, match => `.${match}`);
        }
        rendered = __mpf_normalize_format_exponent(rendered);
        if (conversion === 'E') rendered = rendered.toUpperCase();
      } else {
        rendered = numeric.toPrecision(precision === undefined ? 6 : Math.max(1, precision));
        const exponentIndex = rendered.search(/e/i);
        const suffix = exponentIndex < 0 ? '' : rendered.slice(exponentIndex);
        const mantissa = exponentIndex < 0 ? rendered : rendered.slice(0, exponentIndex);
        rendered = (flags.includes('#')
          ? mantissa
          : mantissa.replace(/(\.\d*?[1-9])0+$/, '$1').replace(/\.0+$/, '')) + suffix;
        rendered = __mpf_normalize_format_exponent(rendered);
        if (conversion === 'G') rendered = rendered.toUpperCase();
      }
      if (numeric >= 0 && flags.includes('+')) rendered = `+${rendered}`;
      else if (numeric >= 0 && flags.includes(' ')) rendered = ` ${rendered}`;
    }
    if (rendered.length < width) {
      const padding = (flags.includes('0') && !flags.includes('-') ? '0' : ' ').repeat(width - rendered.length);
      if (flags.includes('-')) rendered += padding;
      else if (padding[0] === '0') {
        const prefix = rendered.match(/^[+\- ]?(?:0[xX])?/)[0];
        rendered = prefix + padding + rendered.slice(prefix.length);
      } else rendered = padding + rendered;
    }
    result += rendered;
  }
  if (argument !== values.length) throw new TypeError('MPF Matlab format has unused arguments');
  return __mpf_decode_format_escapes(result);
}
function __mpf_exception_record(identifier, message, cause = [], stack = [], original = undefined) {
  if (!__mpf_valid_exception_identifier(identifier)) {
    throw new TypeError('MPF Matlab exception identifier is invalid');
  }
  const record = { identifier, message: String(message), cause: Object.freeze([...cause]),
    stack: Object.freeze([...stack]), __mpf_original: original, [__mpf_exception_tag]: true };
  return Object.freeze(record);
}
function __mpf_is_exception(value) {
  return value !== null && typeof value === 'object' && value[__mpf_exception_tag] === true;
}
function __mpf_matlab_exception(identifier, message, ...values) {
  const text = __mpf_format_exception_message(message, values);
  return __mpf_exception_record(String(identifier), text);
}
function __mpf_capture_exception(value) {
  const object = value !== null && (typeof value === 'object' || typeof value === 'function')
    ? value : null;
  if (object !== null && __mpf_is_exception(object.__mpf_record)) {
    const base = object.__mpf_record;
    return __mpf_exception_record(base.identifier, base.message, base.cause, base.stack, value);
  }
  if (__mpf_is_exception(value)) return value;
  const message = object !== null && typeof object.message === 'string' ? object.message : String(value);
  const identifier = object !== null && __mpf_valid_exception_identifier(object.identifier)
    ? object.identifier
    : (object !== null && typeof object.name === 'string' && object.name.length !== 0
      ? `MPF:${object.name.replace(/[^A-Za-z0-9_]/g, '_')}` : 'MPF:RuntimeError');
  return __mpf_exception_record(identifier, message, [], [], value);
}
function __mpf_throw_exception_record(exception, asCaller) {
  if (!__mpf_is_exception(exception)) throw new TypeError('MPF Matlab throw requires MException');
  const error = new Error(exception.message);
  error.name = exception.identifier;
  error.identifier = exception.identifier;
  Object.defineProperty(error, '__mpf_record', { value: exception });
  if (asCaller && typeof error.stack === 'string') {
    const lines = error.stack.split('\n');
    if (lines.length > 2) lines.splice(1, 1);
    error.stack = lines.join('\n');
  }
  throw error;
}
function __mpf_matlab_error(first, ...rest) {
  if (rest.length === 0) {
    __mpf_throw_exception_record(__mpf_exception_record('MATLAB:error', String(first)), false);
  }
  if (__mpf_valid_exception_identifier(String(first)) && typeof rest[0] === 'string') {
    const message = __mpf_format_exception_message(rest[0], rest.slice(1));
    __mpf_throw_exception_record(__mpf_exception_record(String(first), message), false);
  }
  __mpf_throw_exception_record(
    __mpf_exception_record('MATLAB:error', __mpf_format_exception_message(first, rest)), false);
}
function __mpf_matlab_throw(exception) { __mpf_throw_exception_record(exception, false); }
function __mpf_matlab_throw_as_caller(exception) { __mpf_throw_exception_record(exception, true); }
function __mpf_matlab_rethrow(exception) {
  if (!__mpf_is_exception(exception)) throw new TypeError('MPF Matlab rethrow requires MException');
  if (exception.__mpf_original !== undefined) throw exception.__mpf_original;
  __mpf_throw_exception_record(exception, false);
}
function __mpf_matlab_add_cause(base, cause) {
  if (!__mpf_is_exception(base) || !__mpf_is_exception(cause)) {
    throw new TypeError('MPF Matlab addCause requires MException values');
  }
  return __mpf_exception_record(base.identifier, base.message, [...base.cause, cause], base.stack,
    base.__mpf_original);
}
function __mpf_exception_report(exception, indent) {
  let result = `${' '.repeat(indent)}${exception.message}`;
  for (const cause of exception.cause) {
    result += `\n${' '.repeat(indent)}Caused by:\n${__mpf_exception_report(cause, indent + 2)}`;
  }
  for (const frame of exception.stack) result += `\n${' '.repeat(indent)}${frame}`;
  return result;
}
function __mpf_matlab_get_report(exception, detail = 'extended', ...options) {
  if (!__mpf_is_exception(exception)) throw new TypeError('MPF Matlab getReport requires MException');
  if (detail !== 'basic' && detail !== 'extended') {
    throw new TypeError("MPF Matlab report detail must be 'basic' or 'extended'");
  }
  if (options.length !== 0 &&
      (options.length !== 2 || options[0] !== 'hyperlinks' ||
       !['default', 'on', 'off'].includes(options[1]))) {
    throw new TypeError('MPF Matlab getReport hyperlink option is invalid');
  }
  return detail === 'basic' ? exception.message : __mpf_exception_report(exception, 0);
}

)MPFJS";
}

}  // namespace mpf::detail
