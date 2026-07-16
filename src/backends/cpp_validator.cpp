#include "cpp_validator.hpp"

#include <algorithm>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "../compiler/function_graph_generic.hpp"
#include "../semantic/analyzer.hpp"

namespace mpf::detail {
namespace {

using Expression = mir::Expression;
using Statement = mir::Statement;
using Program = mir::Program;

using FunctionReturnTypes = std::unordered_map<std::string, ValueType>;

bool numeric(const ValueType type) noexcept {
  return type == ValueType::integer || type == ValueType::real || type == ValueType::unknown;
}

bool primitive(const ValueType type) noexcept {
  return type == ValueType::integer || type == ValueType::real || type == ValueType::boolean ||
         type == ValueType::string || type == ValueType::null_value;
}

bool representable_recursive_return(const Statement& function) {
  if (function.return_names.empty() && !function.has_value_return) return true;
  const bool tuple_return =
      function.return_names.size() > 1 ||
      (function.declared_type == ValueType::tuple && !function.return_types.empty());
  if (tuple_return) {
    return (function.return_names.empty() ||
            function.return_types.size() == function.return_names.size()) &&
           std::all_of(function.return_types.begin(), function.return_types.end(),
                       [](const ValueType type) { return primitive(type); });
  }
  return primitive(function.declared_type);
}

ValueType join_types(const ValueType left, const ValueType right) noexcept {
  if (left == right) return left;
  if (left == ValueType::unknown) return right;
  if (right == ValueType::unknown) return left;
  if (numeric(left) && numeric(right)) return ValueType::real;
  return ValueType::unknown;
}

void add_error(std::vector<Diagnostic>& diagnostics, const std::size_t line, std::string code,
               std::string message) {
  diagnostics.push_back(
      {DiagnosticSeverity::error, std::move(code), std::move(message), {line, 1}});
}

bool list_is_homogeneous(const Expression& expression) {
  if (expression.kind != ExpressionKind::list) return true;
  ValueType element_type = ValueType::unknown;
  bool nested = !expression.children.empty();
  std::vector<std::size_t> nested_shape;
  for (const auto& child : expression.children) {
    const auto child_type = child.inferred_type;
    nested = nested && child_type == ValueType::list;
    if (child_type == ValueType::list) {
      if (nested_shape.empty())
        nested_shape = child.shape;
      else if (nested_shape != child.shape)
        return false;
    }
    const auto scalar_type = child_type == ValueType::list ? child.element_type : child_type;
    const auto joined = join_types(element_type, scalar_type);
    if (element_type != ValueType::unknown && scalar_type != ValueType::unknown &&
        joined == ValueType::unknown)
      return false;
    element_type = joined;
  }
  const bool has_nested =
      std::any_of(expression.children.begin(), expression.children.end(),
                  [](const Expression& child) { return child.inferred_type == ValueType::list; });
  return !has_nested || nested;
}

bool has_direct_slice(const Expression& expression) {
  return expression.kind == ExpressionKind::index && expression.children.size() > 1 &&
         std::any_of(expression.children.begin() + 1, expression.children.end(),
                     [](const Expression& child) { return child.kind == ExpressionKind::slice; });
}

ValueType effective_type(const Expression& expression,
                         const FunctionReturnTypes& function_returns) {
  if (expression.inferred_type != ValueType::unknown) return expression.inferred_type;
  if (expression.kind != ExpressionKind::call || expression.children.empty()) {
    return ValueType::unknown;
  }
  const auto& callee = expression.children.front();
  if (callee.kind != ExpressionKind::identifier || callee.binding != BindingKind::function) {
    return ValueType::unknown;
  }
  const auto found = function_returns.find(callee.value);
  return found == function_returns.end() ? ValueType::unknown : found->second;
}

bool cpp_expression_types_compatible(const Expression& left, const Expression& right,
                                     const FunctionReturnTypes& function_returns) {
  const auto left_type = effective_type(left, function_returns);
  const auto right_type = effective_type(right, function_returns);
  if (left_type == ValueType::unknown || right_type == ValueType::unknown) return true;
  const auto arithmetic = [](const ValueType type) {
    return type == ValueType::integer || type == ValueType::real || type == ValueType::boolean;
  };
  if (left_type == right_type && primitive(left_type)) return true;
  if (arithmetic(left_type) && arithmetic(right_type)) return true;
  if (left_type == ValueType::list && right_type == ValueType::list) {
    return left.shape.size() == right.shape.size() && left.element_type == right.element_type;
  }
  if (left_type == ValueType::tuple && right_type == ValueType::tuple) {
    return left.tuple_types == right.tuple_types &&
           left.tuple_element_types == right.tuple_element_types &&
           left.tuple_shapes == right.tuple_shapes;
  }
  return false;
}

bool comparison_expression(const std::string& operation) {
  return operation == "===" || operation == "!==" || operation == "<" || operation == "<=" ||
         operation == ">" || operation == ">=";
}

void validate_expression(const Expression& expression, const semantic::Profile& semantics,
                         const FunctionReturnTypes& function_returns,
                         std::vector<Diagnostic>& diagnostics) {
  for (const auto& child : expression.children) {
    validate_expression(child, semantics, function_returns, diagnostics);
  }
  if (expression.kind == ExpressionKind::list && !list_is_homogeneous(expression)) {
    add_error(diagnostics, expression.location.line, "MPF2020",
              "C++17 target requires a rectangular list with a homogeneous element type");
  }
  if (semantics.logical_result == semantic::LogicalResult::operand &&
      expression.kind == ExpressionKind::binary &&
      (expression.value == "&&" || expression.value == "||") && expression.children.size() == 2) {
    const auto& left = expression.children[0];
    const auto& right = expression.children[1];
    const auto left_type = effective_type(left, function_returns);
    const auto right_type = effective_type(right, function_returns);
    bool compatible = expression.inferred_type != ValueType::unknown &&
                      left_type != ValueType::unknown && right_type != ValueType::unknown &&
                      (left_type == right_type || (numeric(left_type) && numeric(right_type)));
    if (left_type == ValueType::list && right_type == ValueType::list) {
      compatible =
          left.inferred_type == ValueType::list && right.inferred_type == ValueType::list &&
          left.shape.size() == right.shape.size() && left.element_type == right.element_type;
    }
    if (!compatible) {
      add_error(diagnostics, expression.location.line, "MPF2032",
                "C++17 requires statically compatible Python and/or result types");
    }
  }
  if (semantics.truthiness == semantic::Truthiness::dynamic &&
      expression.kind == ExpressionKind::binary && comparison_expression(expression.value) &&
      expression.children.size() == 2 &&
      !cpp_expression_types_compatible(expression.children[0], expression.children[1],
                                       function_returns)) {
    add_error(diagnostics, expression.location.line, "MPF2044",
              "C++17 cannot compare these Python operand types without changing semantics");
  }
  if (semantics.truthiness == semantic::Truthiness::dynamic &&
      expression.kind == ExpressionKind::comparison_chain) {
    for (std::size_t index = 0; index + 1 < expression.children.size(); ++index) {
      if (!cpp_expression_types_compatible(expression.children[index],
                                           expression.children[index + 1], function_returns)) {
        add_error(diagnostics, expression.location.line, "MPF2044",
                  "C++17 cannot compare these Python chain operand types without changing "
                  "semantics");
        break;
      }
    }
  }
  if (semantics.truthiness == semantic::Truthiness::dynamic &&
      expression.kind == ExpressionKind::conditional && expression.children.size() == 3 &&
      !cpp_expression_types_compatible(expression.children[1], expression.children[2],
                                       function_returns)) {
    add_error(diagnostics, expression.location.line, "MPF2044",
              "C++17 requires compatible Python conditional-expression result types");
  }
}

void collect_return_type(const std::vector<Statement>& statements, ValueType& result,
                         bool& has_value, bool& has_empty, bool& incompatible) {
  for (const auto& statement : statements) {
    if (statement.kind == StatementKind::function) continue;
    if (statement.kind == StatementKind::return_statement) {
      if (!statement.has_expression) {
        has_empty = true;
      } else {
        has_value = true;
        const auto joined = join_types(result, statement.expression.inferred_type);
        if (result != ValueType::unknown &&
            statement.expression.inferred_type != ValueType::unknown &&
            joined == ValueType::unknown)
          incompatible = true;
        result = joined;
      }
    }
    collect_return_type(statement.body, result, has_value, has_empty, incompatible);
    collect_return_type(statement.alternative, result, has_value, has_empty, incompatible);
  }
}

bool statement_terminates(const Statement& statement);

bool statements_terminate(const std::vector<Statement>& statements) {
  for (const auto& statement : statements) {
    if (statement_terminates(statement)) return true;
  }
  return false;
}

bool statement_terminates(const Statement& statement) {
  if (statement.kind == StatementKind::return_statement) return true;
  if (statement.kind == StatementKind::if_statement) {
    return !statement.alternative.empty() && statements_terminate(statement.body) &&
           statements_terminate(statement.alternative);
  }
  if (statement.kind == StatementKind::select_case) {
    bool has_default = false;
    for (const auto& clause : statement.body) {
      has_default = has_default || clause.default_case;
      if (!statements_terminate(clause.body)) return false;
    }
    return has_default;
  }
  return false;
}

void validate_statements(const std::vector<Statement>& statements,
                         const semantic::Profile& semantics,
                         const FunctionReturnTypes& function_returns,
                         std::vector<Diagnostic>& diagnostics) {
  for (const auto& statement : statements) {
    if (statement.has_expression) {
      validate_expression(statement.expression, semantics, function_returns, diagnostics);
    }
    if (statement.has_target_expression) {
      validate_expression(statement.target_expression, semantics, function_returns, diagnostics);
    }
    if (statement.has_secondary_expression) {
      validate_expression(statement.secondary_expression, semantics, function_returns, diagnostics);
    }
    if (statement.has_tertiary_expression) {
      validate_expression(statement.tertiary_expression, semantics, function_returns, diagnostics);
    }
    for (const auto& selector : statement.case_selectors) {
      if (selector.has_lower) {
        validate_expression(selector.lower, semantics, function_returns, diagnostics);
      }
      if (selector.has_upper) {
        validate_expression(selector.upper, semantics, function_returns, diagnostics);
      }
    }

    if (statement.kind == StatementKind::assignment) {
      const auto current_type = statement.expression.inferred_type;
      if (statement.previous_type != ValueType::unknown && current_type != ValueType::unknown &&
          join_types(statement.previous_type, current_type) == ValueType::unknown) {
        add_error(diagnostics, statement.line, "MPF2007",
                  "C++17 target cannot represent variable '" + statement.name + "' changing from " +
                      to_string(statement.previous_type) + " to " + to_string(current_type));
      }
      if (current_type == ValueType::list &&
          statement.previous_element_type != ValueType::unknown &&
          statement.expression.element_type != ValueType::unknown &&
          join_types(statement.previous_element_type, statement.expression.element_type) ==
              ValueType::unknown) {
        add_error(diagnostics, statement.line, "MPF2020",
                  "C++17 target cannot change an array/list element type");
      }
    }

    if (statement.kind == StatementKind::multi_assignment) {
      for (std::size_t index = 0; index < statement.target_names.size(); ++index) {
        const auto previous = index < statement.target_previous_types.size()
                                  ? statement.target_previous_types[index]
                                  : ValueType::unknown;
        const auto current = index < statement.target_types.size() ? statement.target_types[index]
                                                                   : ValueType::unknown;
        if (previous != ValueType::unknown && current != ValueType::unknown &&
            join_types(previous, current) == ValueType::unknown) {
          add_error(diagnostics, statement.line, "MPF2007",
                    "C++17 target cannot represent variable '" + statement.target_names[index] +
                        "' changing from " + to_string(previous) + " to " + to_string(current));
        }
        const auto previous_element = index < statement.target_previous_element_types.size()
                                          ? statement.target_previous_element_types[index]
                                          : ValueType::unknown;
        const auto current_element = index < statement.target_element_types.size()
                                         ? statement.target_element_types[index]
                                         : ValueType::unknown;
        if (previous_element != ValueType::unknown && current_element != ValueType::unknown &&
            join_types(previous_element, current_element) == ValueType::unknown) {
          add_error(diagnostics, statement.line, "MPF2020",
                    "C++17 target cannot change an array/list element type for variable '" +
                        statement.target_names[index] + "'");
        }
      }
      if (statement.has_target_pattern) {
        std::vector<const AssignmentPattern*> leaves;
        collect_assignment_leaves(statement.target_pattern, leaves);
        for (const auto* leaf : leaves) {
          if (leaf->kind == AssignmentPatternKind::starred_name && !leaf->captured_paths.empty() &&
              leaf->element_type == ValueType::unknown) {
            add_error(diagnostics, statement.line, "MPF2020",
                      "C++17 target requires a homogeneous starred capture for variable '" +
                          leaf->name + "'");
          }
        }
      }
    }

    if (statement.kind == StatementKind::indexed_assignment &&
        has_direct_slice(statement.target_expression) &&
        statement.expression.inferred_type == ValueType::list) {
      if (semantics.resizable_sections &&
          statement.target_expression.element_type != ValueType::unknown &&
          statement.expression.element_type != ValueType::unknown &&
          statement.target_expression.element_type != statement.expression.element_type) {
        add_error(diagnostics, statement.line, "MPF2020",
                  "C++17 cannot preserve a Python slice assignment that changes element type");
      }
      auto replacement_rank = statement.expression.shape.size();
      if (statement.target_expression.column_major &&
          statement.target_expression.shape.size() == 1 && replacement_rank == 2 &&
          (statement.expression.shape[0] == 1 || statement.expression.shape[1] == 1)) {
        replacement_rank = 1;
      }
      if (replacement_rank != statement.target_expression.shape.size()) {
        add_error(diagnostics, statement.line, "MPF2020",
                  "C++17 section assignment cannot change the container nesting rank");
      }
    }

    if (statement.kind == StatementKind::function) {
      ValueType return_type = ValueType::unknown;
      bool has_value = false;
      bool has_empty = false;
      bool incompatible = false;
      collect_return_type(statement.body, return_type, has_value, has_empty, incompatible);
      if (incompatible) {
        add_error(diagnostics, statement.line, "MPF2008",
                  "C++17 target requires all value-returning paths to have a compatible type");
      }
      if (has_value && (has_empty || !statements_terminate(statement.body)) &&
          statement.return_names.empty()) {
        add_error(
            diagnostics, statement.line, "MPF2009",
            "C++17 target cannot mix a value return with an empty or fallthrough return path");
      }
    }

    validate_statements(statement.body, semantics, function_returns, diagnostics);
    validate_statements(statement.alternative, semantics, function_returns, diagnostics);
  }
}

}  // namespace

std::vector<Diagnostic> validate_cpp_capabilities(const mir::Program& program) {
  std::vector<Diagnostic> diagnostics;
  const auto function_graph =
      build_function_dependency_graph_generic<mir::Expression, mir::Statement>(program.statements);
  for (const auto index : function_graph.definition_order) {
    const auto& function = program.statements[index];
    if (function_graph.recursive[index] && !representable_recursive_return(function)) {
      add_error(diagnostics, function.line, "MPF2035",
                "C++17 requires a statically representable return type for recursive function '" +
                    function.name + "'");
    }
  }
  FunctionReturnTypes function_returns;
  for (const auto& statement : program.statements) {
    if (statement.kind != StatementKind::function) continue;
    ValueType return_type = ValueType::unknown;
    bool has_value = false;
    bool has_empty = false;
    bool incompatible = false;
    collect_return_type(statement.body, return_type, has_value, has_empty, incompatible);
    if (has_value && !incompatible) function_returns.insert_or_assign(statement.name, return_type);
  }
  validate_statements(program.statements, program.semantics, function_returns, diagnostics);
  return diagnostics;
}

}  // namespace mpf::detail
