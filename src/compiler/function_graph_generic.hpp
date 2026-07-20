#pragma once

#include <algorithm>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "expression_ast.hpp"
#include "function_graph.hpp"
#include "statement_kind.hpp"

namespace mpf::detail {

template <typename Expression, typename Statement>
FunctionDependencyGraph build_function_dependency_graph_generic(
    const std::vector<Statement>& statements) {
  const auto collect_local_names = [](const auto& self, const std::vector<Statement>& nodes,
                                      std::unordered_set<std::string>& names) -> void {
    for (const auto& statement : nodes) {
      switch (statement.kind) {
        case StatementKind::declaration:
        case StatementKind::assignment:
        case StatementKind::range_loop:
        case StatementKind::for_loop:
        case StatementKind::function: names.insert(statement.name); break;
        case StatementKind::try_statement:
          if (!statement.name.empty()) names.insert(statement.name);
          break;
        case StatementKind::multi_assignment:
          names.insert(statement.target_names.begin(), statement.target_names.end());
          break;
        case StatementKind::indexed_assignment:
        case StatementKind::print:
        case StatementKind::return_statement:
        case StatementKind::break_statement:
        case StatementKind::continue_statement:
        case StatementKind::expression:
        case StatementKind::if_statement:
        case StatementKind::select_case:
        case StatementKind::case_clause:
        case StatementKind::while_loop: break;
      }
      if (statement.kind == StatementKind::function) continue;
      self(self, statement.body, names);
      self(self, statement.alternative, names);
    }
  };
  const auto collect_expression = [](const auto& self, const Expression& expression,
                                     const std::unordered_map<std::string, std::size_t>& functions,
                                     const std::unordered_set<std::string>& local_names,
                                     std::vector<std::size_t>& dependencies) -> void {
    if (expression.kind == ExpressionKind::call && !expression.children.empty()) {
      const auto& callee = expression.children.front();
      if (callee.kind == ExpressionKind::identifier && local_names.count(callee.value) == 0U) {
        const auto found = functions.find(callee.value);
        if (found != functions.end()) dependencies.push_back(found->second);
      }
    }
    for (const auto& child : expression.children) {
      self(self, child, functions, local_names, dependencies);
    }
  };
  const auto collect_statements = [&](const auto& self, const std::vector<Statement>& nodes,
                                      const std::unordered_map<std::string, std::size_t>& functions,
                                      const std::unordered_set<std::string>& local_names,
                                      std::vector<std::size_t>& dependencies) -> void {
    for (const auto& statement : nodes) {
      if (statement.has_expression) {
        collect_expression(collect_expression, statement.expression, functions, local_names,
                           dependencies);
      }
      if (statement.has_target_expression) {
        collect_expression(collect_expression, statement.target_expression, functions, local_names,
                           dependencies);
      }
      if (statement.has_secondary_expression) {
        collect_expression(collect_expression, statement.secondary_expression, functions,
                           local_names, dependencies);
      }
      if (statement.has_tertiary_expression) {
        collect_expression(collect_expression, statement.tertiary_expression, functions,
                           local_names, dependencies);
      }
      for (const auto& selector : statement.case_selectors) {
        if (selector.has_lower) {
          collect_expression(collect_expression, selector.lower, functions, local_names,
                             dependencies);
        }
        if (selector.has_upper) {
          collect_expression(collect_expression, selector.upper, functions, local_names,
                             dependencies);
        }
      }
      if (statement.kind == StatementKind::function) continue;
      self(self, statement.body, functions, local_names, dependencies);
      self(self, statement.alternative, functions, local_names, dependencies);
    }
  };

  FunctionDependencyGraph graph;
  graph.dependencies.resize(statements.size());
  graph.recursive.resize(statements.size(), false);
  std::unordered_map<std::string, std::size_t> functions;
  std::vector<std::size_t> function_indices;
  for (std::size_t index = 0; index < statements.size(); ++index) {
    if (statements[index].kind != StatementKind::function) continue;
    functions.insert_or_assign(statements[index].name, index);
    function_indices.push_back(index);
  }
  for (const auto index : function_indices) {
    const auto& function = statements[index];
    std::unordered_set<std::string> local_names(function.parameters.begin(),
                                                function.parameters.end());
    local_names.insert(function.return_names.begin(), function.return_names.end());
    collect_local_names(collect_local_names, function.body, local_names);
    auto& dependencies = graph.dependencies[index];
    collect_statements(collect_statements, function.body, functions, local_names, dependencies);
    std::sort(dependencies.begin(), dependencies.end());
    dependencies.erase(std::unique(dependencies.begin(), dependencies.end()), dependencies.end());
  }
  std::vector<unsigned char> state(statements.size(), 0);
  std::function<void(std::size_t)> visit = [&](const std::size_t index) {
    if (state[index] != 0) return;
    state[index] = 1;
    for (const auto dependency : graph.dependencies[index]) visit(dependency);
    state[index] = 2;
    graph.definition_order.push_back(index);
  };
  for (const auto index : function_indices) visit(index);

  const auto reaches = [&](const auto& self, const std::size_t current, const std::size_t target,
                           std::vector<bool>& visited) -> bool {
    if (visited[current]) return false;
    visited[current] = true;
    for (const auto dependency : graph.dependencies[current]) {
      if (dependency == target || self(self, dependency, target, visited)) return true;
    }
    return false;
  };
  for (const auto index : function_indices) {
    std::vector<bool> visited(statements.size(), false);
    graph.recursive[index] = reaches(reaches, index, index, visited);
  }
  return graph;
}

}  // namespace mpf::detail
