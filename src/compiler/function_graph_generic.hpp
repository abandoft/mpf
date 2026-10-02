#pragma once

#include <algorithm>
#include <vector>

#include "expression_ast.hpp"
#include "function_graph.hpp"
#include "statement_kind.hpp"

namespace mpf::detail {

template <typename Expression, typename Statement, typename ResolveCallee>
FunctionDependencyGraph build_function_dependency_graph_generic(
    const std::vector<Statement>& statements, ResolveCallee resolve_callee) {
  const auto collect_expression = [&](const auto& self, const Expression& expression,
                                      std::vector<std::size_t>& dependencies) -> void {
    if (expression.kind == ExpressionKind::call && !expression.children.empty()) {
      const auto& callee = expression.children.front();
      if (callee.kind == ExpressionKind::identifier) {
        const auto resolved = resolve_callee(callee);
        if (resolved.has_value()) dependencies.push_back(*resolved);
      }
    }
    for (const auto& child : expression.children) {
      self(self, child, dependencies);
    }
  };
  const auto collect_statements = [&](const auto& self, const std::vector<Statement>& nodes,
                                      std::vector<std::size_t>& dependencies) -> void {
    for (const auto& statement : nodes) {
      if (statement.kind == StatementKind::function) continue;
      if (statement.has_expression) {
        collect_expression(collect_expression, statement.expression, dependencies);
      }
      if (statement.has_target_expression) {
        collect_expression(collect_expression, statement.target_expression, dependencies);
      }
      if (statement.has_secondary_expression) {
        collect_expression(collect_expression, statement.secondary_expression, dependencies);
      }
      if (statement.has_tertiary_expression) {
        collect_expression(collect_expression, statement.tertiary_expression, dependencies);
      }
      for (const auto& selector : statement.case_selectors) {
        if (selector.has_lower) {
          collect_expression(collect_expression, selector.lower, dependencies);
        }
        if (selector.has_upper) {
          collect_expression(collect_expression, selector.upper, dependencies);
        }
      }
      self(self, statement.body, dependencies);
      self(self, statement.alternative, dependencies);
    }
  };

  FunctionDependencyGraph graph;
  graph.dependencies.resize(statements.size());
  std::vector<std::size_t> function_indices;
  for (std::size_t index = 0; index < statements.size(); ++index) {
    if (statements[index].kind != StatementKind::function) continue;
    function_indices.push_back(index);
  }
  for (const auto index : function_indices) {
    const auto& function = statements[index];
    auto& dependencies = graph.dependencies[index];
    for (const auto& value : function.parameter_defaults) {
      collect_expression(collect_expression, value, dependencies);
    }
    collect_statements(collect_statements, function.body, dependencies);
    std::sort(dependencies.begin(), dependencies.end());
    dependencies.erase(std::unique(dependencies.begin(), dependencies.end()), dependencies.end());
  }
  analyze_function_dependencies(graph, function_indices);
  return graph;
}

}  // namespace mpf::detail
