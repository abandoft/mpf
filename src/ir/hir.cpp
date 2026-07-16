#include "hir.hpp"

#include <algorithm>
#include <string>
#include <utility>

namespace mpf::detail::hir {
namespace {

semantic::Profile semantic_profile(const SourceLanguage language) noexcept {
  semantic::Profile profile;
  if (language == SourceLanguage::python) {
    profile.truthiness = semantic::Truthiness::dynamic;
    profile.logical_result = semantic::LogicalResult::operand;
    profile.equality = semantic::Equality::structural;
    profile.division = semantic::Division::real_quotient;
    profile.resizable_sections = true;
    profile.emit_parameter_defaults = true;
  } else if (language == SourceLanguage::matlab) {
    profile.division = semantic::Division::real_quotient;
    profile.layout = semantic::IndexLayout::column_major;
    profile.top_level_storage = semantic::TopLevelStorage::entry_function;
  } else if (language == SourceLanguage::fortran) {
    profile.layout = semantic::IndexLayout::column_major;
  }
  return profile;
}

Expression lower_expression(mpf::detail::Expression&& source, IrIdAllocator<HirNodeId>& ids) {
  Expression result;
  if (source.valid()) result.id = ids.next();
  result.location = source.location;
  result.kind = source.kind;
  result.value = std::move(source.value);
  result.operators = std::move(source.operators);
  result.children.reserve(source.children.size());
  for (auto& child : source.children) {
    result.children.push_back(lower_expression(std::move(child), ids));
  }
  result.inferred_type = source.inferred_type;
  result.binding = source.binding;
  result.intrinsic = source.intrinsic;
  result.element_type = source.element_type;
  result.shape = std::move(source.shape);
  result.tuple_types = std::move(source.tuple_types);
  result.tuple_element_types = std::move(source.tuple_element_types);
  result.tuple_shapes = std::move(source.tuple_shapes);
  result.sequence_is_list = source.sequence_is_list;
  result.sequence_elements = std::move(source.sequence_elements);
  result.requested_outputs = source.requested_outputs;
  result.multi_output_call = source.matlab_multi_output_call;
  result.argument_intents = std::move(source.argument_intents);
  result.argument_names = std::move(source.argument_names);
  result.argument_optional_forward = std::move(source.argument_optional_forward);
  result.procedure_has_result = source.procedure_has_result;
  result.index_base = source.index_base;
  result.allow_negative_index = source.allow_negative_index;
  result.column_major = source.column_major;
  result.slice_stop_inclusive = source.slice_stop_inclusive;
  return result;
}

CaseSelector lower_selector(mpf::detail::CaseSelector&& source, IrIdAllocator<HirNodeId>& ids) {
  CaseSelector result;
  result.lower = lower_expression(std::move(source.lower), ids);
  result.has_lower = source.has_lower;
  result.upper = lower_expression(std::move(source.upper), ids);
  result.has_upper = source.has_upper;
  result.range = source.range;
  return result;
}

Statement lower_statement(mpf::detail::Statement&& source, IrIdAllocator<HirNodeId>& ids) {
  Statement result;
  result.id = ids.next();
  result.kind = source.kind;
  result.line = source.line;
  result.name = std::move(source.name);
  result.expression = lower_expression(std::move(source.expression), ids);
  result.has_expression = source.has_expression;
  result.procedure_call = source.procedure_call;
  result.secondary_expression = lower_expression(std::move(source.secondary_expression), ids);
  result.has_secondary_expression = source.has_secondary_expression;
  result.tertiary_expression = lower_expression(std::move(source.tertiary_expression), ids);
  result.has_tertiary_expression = source.has_tertiary_expression;
  result.inclusive_stop = source.inclusive_stop;
  result.retain_last_loop_value = source.retain_last_loop_value;
  result.declared_type = source.declared_type;
  result.element_type = source.element_type;
  result.previous_type = source.previous_type;
  result.previous_element_type = source.previous_element_type;
  result.parameter_intent = source.parameter_intent;
  result.optional_parameter = source.optional_parameter;
  result.dummy_parameter = source.dummy_parameter;
  result.shape = std::move(source.shape);
  result.index_base = source.index_base;
  result.allow_negative_index = source.allow_negative_index;
  result.target_expression = lower_expression(std::move(source.target_expression), ids);
  result.has_target_expression = source.has_target_expression;
  result.parameters = std::move(source.parameters);
  result.parameter_kinds = std::move(source.parameter_kinds);
  result.parameter_defaults.reserve(source.parameter_defaults.size());
  for (auto& expression : source.parameter_defaults) {
    result.parameter_defaults.push_back(lower_expression(std::move(expression), ids));
  }
  result.parameter_intents = std::move(source.parameter_intents);
  result.parameter_optional = std::move(source.parameter_optional);
  result.parameter_types = std::move(source.parameter_types);
  result.parameter_element_types = std::move(source.parameter_element_types);
  result.parameter_shapes = std::move(source.parameter_shapes);
  result.return_names = std::move(source.return_names);
  result.has_value_return = source.has_value_return;
  result.return_types = std::move(source.return_types);
  result.return_element_types = std::move(source.return_element_types);
  result.return_shapes = std::move(source.return_shapes);
  result.return_sequence_is_list = source.return_sequence_is_list;
  result.return_sequence_elements = std::move(source.return_sequence_elements);
  result.target_names = std::move(source.target_names);
  result.target_pattern = std::move(source.target_pattern);
  result.has_target_pattern = source.has_target_pattern;
  result.target_types = std::move(source.target_types);
  result.target_element_types = std::move(source.target_element_types);
  result.target_shapes = std::move(source.target_shapes);
  result.target_previous_types = std::move(source.target_previous_types);
  result.target_previous_element_types = std::move(source.target_previous_element_types);
  result.case_selectors.reserve(source.case_selectors.size());
  for (auto& selector : source.case_selectors) {
    result.case_selectors.push_back(lower_selector(std::move(selector), ids));
  }
  result.default_case = source.default_case;
  result.body.reserve(source.body.size());
  for (auto& statement : source.body) {
    result.body.push_back(lower_statement(std::move(statement), ids));
  }
  result.alternative.reserve(source.alternative.size());
  for (auto& statement : source.alternative) {
    result.alternative.push_back(lower_statement(std::move(statement), ids));
  }
  return result;
}

void add_error(std::vector<Diagnostic>& diagnostics, const SourceLocation location,
               const std::string_view stage, std::string message) {
  diagnostics.push_back({DiagnosticSeverity::error, "MPF0005",
                         "invalid HIR at '" + std::string(stage) + "': " + std::move(message),
                         location});
}

void verify_expression(const Expression& expression, const std::size_t node_count,
                       std::vector<bool>& seen, const std::string_view stage,
                       std::vector<Diagnostic>& diagnostics) {
  if (!expression.valid()) {
    if (expression.id.valid()) {
      add_error(diagnostics, expression.location, stage, "invalid expression has an ID");
    }
    return;
  }
  const auto id = static_cast<std::size_t>(expression.id.value());
  if (id == 0 || id > node_count || seen[id]) {
    add_error(diagnostics, expression.location, stage,
              "expression ID is missing, duplicated, or out of range");
    return;
  }
  seen[id] = true;
  if (expression.requested_outputs == 0) {
    add_error(diagnostics, expression.location, stage, "expression requests zero outputs");
  }
  if (expression.kind == ExpressionKind::comparison_chain &&
      (expression.children.size() < 2 ||
       expression.operators.size() + 1 != expression.children.size())) {
    add_error(diagnostics, expression.location, stage,
              "comparison chain operand/operator count is inconsistent");
  }
  if (expression.kind == ExpressionKind::conditional && expression.children.size() != 3) {
    add_error(diagnostics, expression.location, stage,
              "conditional expression must have three operands");
  }
  for (const auto& child : expression.children) {
    verify_expression(child, node_count, seen, stage, diagnostics);
  }
}

void verify_statements(const std::vector<Statement>& statements, const std::size_t node_count,
                       std::vector<bool>& seen, const std::string_view stage,
                       std::vector<Diagnostic>& diagnostics) {
  for (const auto& statement : statements) {
    const auto id = static_cast<std::size_t>(statement.id.value());
    if (id == 0 || id > node_count || seen[id]) {
      add_error(diagnostics, {statement.line, 1}, stage,
                "statement ID is missing, duplicated, or out of range");
      continue;
    }
    seen[id] = true;
    const auto verify_optional = [&](const Expression& expression, const bool present,
                                     const char* name) {
      if (present && !expression.valid()) {
        add_error(diagnostics, {statement.line, 1}, stage,
                  std::string(name) + " presence flag has no expression");
      }
      verify_expression(expression, node_count, seen, stage, diagnostics);
    };
    verify_optional(statement.expression, statement.has_expression, "primary expression");
    verify_optional(statement.secondary_expression, statement.has_secondary_expression,
                    "secondary expression");
    verify_optional(statement.tertiary_expression, statement.has_tertiary_expression,
                    "tertiary expression");
    verify_optional(statement.target_expression, statement.has_target_expression,
                    "target expression");
    for (const auto& expression : statement.parameter_defaults) {
      verify_expression(expression, node_count, seen, stage, diagnostics);
    }
    for (const auto& selector : statement.case_selectors) {
      verify_optional(selector.lower, selector.has_lower, "case lower bound");
      verify_optional(selector.upper, selector.has_upper, "case upper bound");
    }
    verify_statements(statement.body, node_count, seen, stage, diagnostics);
    verify_statements(statement.alternative, node_count, seen, stage, diagnostics);
  }
}

void reindex_expression(Expression& expression, IrIdAllocator<HirNodeId>& ids) noexcept {
  expression.id = expression.valid() ? ids.next() : HirNodeId{};
  for (auto& child : expression.children) reindex_expression(child, ids);
}

void reindex_statements(std::vector<Statement>& statements,
                        IrIdAllocator<HirNodeId>& ids) noexcept {
  for (auto& statement : statements) {
    statement.id = ids.next();
    reindex_expression(statement.expression, ids);
    reindex_expression(statement.secondary_expression, ids);
    reindex_expression(statement.tertiary_expression, ids);
    reindex_expression(statement.target_expression, ids);
    for (auto& expression : statement.parameter_defaults) reindex_expression(expression, ids);
    for (auto& selector : statement.case_selectors) {
      reindex_expression(selector.lower, ids);
      reindex_expression(selector.upper, ids);
    }
    reindex_statements(statement.body, ids);
    reindex_statements(statement.alternative, ids);
  }
}

}  // namespace

LoweringResult lower_from_syntax(mpf::detail::Program&& source) {
  LoweringResult result;
  result.program.language = source.language;
  result.program.semantics = semantic_profile(source.language);
  IrIdAllocator<HirNodeId> ids;
  result.program.statements.reserve(source.statements.size());
  for (auto& statement : source.statements) {
    result.program.statements.push_back(lower_statement(std::move(statement), ids));
  }
  result.program.node_count = ids.count();
  result.diagnostics = verify(result.program, "syntax-to-hir");
  return result;
}

void reindex(Program& program) noexcept {
  IrIdAllocator<HirNodeId> ids;
  reindex_statements(program.statements, ids);
  program.node_count = ids.count();
}

std::vector<Diagnostic> verify(const Program& program, const std::string_view stage) {
  std::vector<Diagnostic> diagnostics;
  if (program.language == SourceLanguage::automatic) {
    add_error(diagnostics, {1, 1}, stage, "source language is unresolved");
    return diagnostics;
  }
  std::vector<bool> seen(program.node_count + 1, false);
  verify_statements(program.statements, program.node_count, seen, stage, diagnostics);
  const auto visited = static_cast<std::size_t>(std::count(seen.begin(), seen.end(), true));
  if (visited != program.node_count) {
    add_error(diagnostics, {1, 1}, stage, "node ID space has unreachable entries");
  }
  return diagnostics;
}

}  // namespace mpf::detail::hir
