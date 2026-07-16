#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "../source/source_text.hpp"
#include "assignment_pattern.hpp"
#include "expression_ast.hpp"
#include "mpf/diagnostic.hpp"
#include "mpf/transpiler.hpp"

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
  select_case,
  case_clause,
  while_loop,
  range_loop,
  function
};

struct CaseSelector {
  Expression lower;
  bool has_lower{false};
  Expression upper;
  bool has_upper{false};
  bool range{false};
};

struct Statement {
  StatementKind kind{StatementKind::expression};
  std::size_t line{1};
  std::string name;
  Expression expression;
  bool has_expression{false};
  bool procedure_call{false};
  Expression secondary_expression;
  bool has_secondary_expression{false};
  Expression tertiary_expression;
  bool has_tertiary_expression{false};
  bool inclusive_stop{false};
  bool retain_last_loop_value{true};
  ValueType declared_type{ValueType::unknown};
  ValueType element_type{ValueType::unknown};
  ValueType previous_type{ValueType::unknown};
  ValueType previous_element_type{ValueType::unknown};
  ParameterIntent parameter_intent{ParameterIntent::none};
  bool optional_parameter{false};
  bool dummy_parameter{false};
  std::vector<std::size_t> shape;
  std::size_t index_base{0};
  bool allow_negative_index{false};
  Expression target_expression;
  bool has_target_expression{false};
  std::vector<std::string> parameters;
  std::vector<ParameterKind> parameter_kinds;
  std::vector<Expression> parameter_defaults;
  std::vector<ParameterIntent> parameter_intents;
  std::vector<bool> parameter_optional;
  std::vector<ValueType> parameter_types;
  std::vector<ValueType> parameter_element_types;
  std::vector<std::vector<std::size_t>> parameter_shapes;
  std::vector<std::string> return_names;
  bool has_value_return{false};
  std::vector<ValueType> return_types;
  std::vector<ValueType> return_element_types;
  std::vector<std::vector<std::size_t>> return_shapes;
  bool return_sequence_is_list{false};
  std::vector<ValueMetadata> return_sequence_elements;
  std::vector<std::string> target_names;
  AssignmentPattern target_pattern;
  bool has_target_pattern{false};
  std::vector<ValueType> target_types;
  std::vector<ValueType> target_element_types;
  std::vector<std::vector<std::size_t>> target_shapes;
  std::vector<ValueType> target_previous_types;
  std::vector<ValueType> target_previous_element_types;
  std::vector<CaseSelector> case_selectors;
  bool default_case{false};
  std::vector<Statement> body;
  std::vector<Statement> alternative;
};

struct Program {
  SourceLanguage language{SourceLanguage::automatic};
  std::vector<Statement> statements;
};

struct ParseResult {
  Program program;
  std::vector<Diagnostic> diagnostics;
};

}  // namespace mpf::detail
