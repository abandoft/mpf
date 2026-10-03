#pragma once

namespace mpf::detail::mir {

enum class Opcode {
  invalid,
  literal,
  identifier,
  load,
  unary,
  binary,
  compare,
  comparison_chain,
  conditional,
  truthiness,
  call,
  member,
  index,
  slice,
  aggregate,
  allocate,
  store,
  store_indexed,
  copy,
  writeback,
  output,
  return_value,
  expression,
  selection,
  loop,
  function,
  control,
  catch_exception,
  invocation_output_count,
  parameter_presence,
  argument_normalize,
  argument_validate,
  discard_output
};

}  // namespace mpf::detail::mir
