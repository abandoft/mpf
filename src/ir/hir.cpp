#include "hir.hpp"

#include <algorithm>
#include <string>
#include <utility>

namespace mpf::detail::hir {
namespace {

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
  if (expression.kind == ExpressionKind::binary) {
    const bool has_comparison = expression.comparison != ComparisonOperator::none;
    const bool has_operation = expression.operation != BinaryOperator::none;
    if (has_comparison == has_operation || (has_comparison && !expression.value.empty()) ||
        (has_operation && expression.value.empty())) {
      add_error(diagnostics, expression.location, stage,
                "binary expression has no operator or carries multiple operator forms");
    }
  }
  if ((expression.kind == ExpressionKind::unary) !=
          (expression.unary_operation != UnaryOperator::none) ||
      (expression.kind == ExpressionKind::unary && expression.children.size() != 1U)) {
    add_error(diagnostics, expression.location, stage,
              "unary expression has an invalid typed operator contract");
  }
  if (expression.kind == ExpressionKind::comparison_chain &&
      (expression.children.size() < 3 ||
       expression.comparisons.size() + 1 != expression.children.size() ||
       std::any_of(expression.comparisons.begin(), expression.comparisons.end(),
                   [](const auto operation) { return operation == ComparisonOperator::none; }))) {
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
    if (statement.implicit_result != semantic::ImplicitResultPolicy::none &&
        (statement.implicit_result != semantic::ImplicitResultPolicy::matlab_ans_if_value ||
         statement.kind != StatementKind::expression || statement.name != "ans" ||
         !statement.has_expression || statement.expression.kind != ExpressionKind::call)) {
      add_error(diagnostics, {statement.line, 1}, stage,
                "implicit result is not a normalized Matlab ans call");
    }
    if ((statement.kind == StatementKind::try_statement) != statement.has_exception_handler ||
        (statement.kind == StatementKind::try_statement) !=
            (statement.exception_handler_line != 0U)) {
      add_error(diagnostics, {statement.line, 1}, stage,
                "try statement and exception-handler presence are inconsistent");
    }
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

}  // namespace

std::vector<Diagnostic> verify(const Program& program, const std::string_view stage) {
  std::vector<Diagnostic> diagnostics;
  if (program.language == SourceLanguage::automatic) {
    add_error(diagnostics, {1, 1}, stage, "source language is unresolved");
    return diagnostics;
  }
  if (!semantic::source_division_contract_matches(program.language, program.semantics)) {
    add_error(diagnostics, {1, 1}, stage,
              "source division profile is inconsistent with the language");
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
