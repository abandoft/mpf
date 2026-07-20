#pragma once

namespace mpf::detail {

enum class StatementKind {
  declaration,
  assignment,
  multi_assignment,
  indexed_assignment,
  print,
  return_statement,
  break_statement,
  continue_statement,
  expression,
  if_statement,
  try_statement,
  select_case,
  case_clause,
  while_loop,
  range_loop,
  for_loop,
  function
};

}  // namespace mpf::detail
