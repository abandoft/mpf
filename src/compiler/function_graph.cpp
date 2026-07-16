#include "function_graph.hpp"

#include <algorithm>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace mpf::detail {
namespace {

void collect_local_names(const std::vector<Statement>& statements,
                         std::unordered_set<std::string>& names) {
  for (const auto& statement : statements) {
    switch (statement.kind) {
      case StatementKind::declaration:
      case StatementKind::assignment:
      case StatementKind::range_loop:
      case StatementKind::function: names.insert(statement.name); break;
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
    collect_local_names(statement.body, names);
    collect_local_names(statement.alternative, names);
  }
}

void collect_expression_dependencies(const Expression& expression,
                                     const std::unordered_map<std::string, std::size_t>& functions,
                                     const std::unordered_set<std::string>& local_names,
                                     std::vector<std::size_t>& dependencies) {
  if (expression.kind == ExpressionKind::call && !expression.children.empty()) {
    const auto& callee = expression.children.front();
    if (callee.kind == ExpressionKind::identifier && local_names.count(callee.value) == 0U) {
      const auto found = functions.find(callee.value);
      if (found != functions.end()) dependencies.push_back(found->second);
    }
  }
  for (const auto& child : expression.children) {
    collect_expression_dependencies(child, functions, local_names, dependencies);
  }
}

void collect_statement_dependencies(const std::vector<Statement>& statements,
                                    const std::unordered_map<std::string, std::size_t>& functions,
                                    const std::unordered_set<std::string>& local_names,
                                    std::vector<std::size_t>& dependencies) {
  for (const auto& statement : statements) {
    if (statement.has_expression) {
      collect_expression_dependencies(statement.expression, functions, local_names, dependencies);
    }
    if (statement.has_target_expression) {
      collect_expression_dependencies(statement.target_expression, functions, local_names,
                                      dependencies);
    }
    if (statement.has_secondary_expression) {
      collect_expression_dependencies(statement.secondary_expression, functions, local_names,
                                      dependencies);
    }
    if (statement.has_tertiary_expression) {
      collect_expression_dependencies(statement.tertiary_expression, functions, local_names,
                                      dependencies);
    }
    for (const auto& selector : statement.case_selectors) {
      if (selector.has_lower) {
        collect_expression_dependencies(selector.lower, functions, local_names, dependencies);
      }
      if (selector.has_upper) {
        collect_expression_dependencies(selector.upper, functions, local_names, dependencies);
      }
    }
    if (statement.kind == StatementKind::function) continue;
    collect_statement_dependencies(statement.body, functions, local_names, dependencies);
    collect_statement_dependencies(statement.alternative, functions, local_names, dependencies);
  }
}

bool reaches(const std::size_t current, const std::size_t target,
             const std::vector<std::vector<std::size_t>>& dependencies,
             std::vector<bool>& visited) {
  if (visited[current]) return false;
  visited[current] = true;
  for (const auto dependency : dependencies[current]) {
    if (dependency == target || reaches(dependency, target, dependencies, visited)) return true;
  }
  return false;
}

}  // namespace

FunctionDependencyGraph build_function_dependency_graph(const std::vector<Statement>& statements) {
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
    collect_local_names(function.body, local_names);
    auto& dependencies = graph.dependencies[index];
    collect_statement_dependencies(function.body, functions, local_names, dependencies);
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

  for (const auto index : function_indices) {
    std::vector<bool> visited(statements.size(), false);
    graph.recursive[index] = reaches(index, index, graph.dependencies, visited);
  }
  return graph;
}

}  // namespace mpf::detail
