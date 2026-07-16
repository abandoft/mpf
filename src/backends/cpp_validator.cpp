#include "cpp_validator.hpp"

#include <algorithm>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "../semantic/analyzer.hpp"

namespace mpf::detail {
namespace {

using FunctionReturnTypes = std::unordered_map<std::string, ValueType>;

bool numeric(const ValueType type) noexcept {
  return type == ValueType::integer || type == ValueType::real || type == ValueType::unknown;
}

bool primitive(const ValueType type) noexcept {
  return type == ValueType::integer || type == ValueType::real || type == ValueType::boolean ||
         type == ValueType::string || type == ValueType::null_value;
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

bool representable_recursive_return(const mir::Statement& function) {
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

bool list_is_homogeneous(const mir::Program& program, const mir::Expression& expression) {
  if (expression.kind != ExpressionKind::list) return true;
  ValueType element_type = ValueType::unknown;
  bool nested = !expression.children.empty();
  std::vector<std::size_t> nested_shape;
  bool has_nested = false;
  for (const auto child_id : expression.children) {
    const auto* child = mir::expression(program, child_id);
    if (child == nullptr) return false;
    const auto child_type = child->inferred_type;
    nested = nested && child_type == ValueType::list;
    has_nested = has_nested || child_type == ValueType::list;
    if (child_type == ValueType::list) {
      if (nested_shape.empty())
        nested_shape = child->shape;
      else if (nested_shape != child->shape)
        return false;
    }
    const auto scalar_type = child_type == ValueType::list ? child->element_type : child_type;
    const auto joined = join_types(element_type, scalar_type);
    if (element_type != ValueType::unknown && scalar_type != ValueType::unknown &&
        joined == ValueType::unknown) {
      return false;
    }
    element_type = joined;
  }
  return !has_nested || nested;
}

bool has_direct_slice(const mir::Program& program, const mir::Expression& expression) {
  if (expression.kind != ExpressionKind::index || expression.children.size() <= 1) return false;
  return std::any_of(expression.children.begin() + 1, expression.children.end(),
                     [&](const MirExpressionId child_id) {
                       const auto* child = mir::expression(program, child_id);
                       return child != nullptr && child->kind == ExpressionKind::slice;
                     });
}

ValueType effective_type(const mir::Program& program, const mir::Expression& expression,
                         const FunctionReturnTypes& function_returns) {
  if (expression.inferred_type != ValueType::unknown) return expression.inferred_type;
  if (expression.kind != ExpressionKind::call || expression.children.empty()) {
    return ValueType::unknown;
  }
  const auto* callee = mir::expression(program, expression.children.front());
  if (callee == nullptr || callee->kind != ExpressionKind::identifier ||
      callee->binding != BindingKind::function) {
    return ValueType::unknown;
  }
  const auto found = function_returns.find(callee->value);
  return found == function_returns.end() ? ValueType::unknown : found->second;
}

bool cpp_expression_types_compatible(const mir::Program& program, const mir::Expression& left,
                                     const mir::Expression& right,
                                     const FunctionReturnTypes& function_returns) {
  const auto left_type = effective_type(program, left, function_returns);
  const auto right_type = effective_type(program, right, function_returns);
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

bool compatible_value_types(const ValueType left, const ValueType right) noexcept {
  if (left == ValueType::unknown || right == ValueType::unknown || left == right) return true;
  const auto arithmetic = [](const ValueType type) {
    return type == ValueType::integer || type == ValueType::real || type == ValueType::boolean;
  };
  return arithmetic(left) && arithmetic(right);
}

bool cpp_comparison_supported(const mir::Program& program, const ComparisonOperator operation,
                              const mir::Expression& left, const mir::Expression& right,
                              const FunctionReturnTypes& function_returns) {
  if (operation == ComparisonOperator::equal || operation == ComparisonOperator::not_equal) {
    return true;
  }
  if (comparison_is_identity(operation)) {
    const auto left_type = effective_type(program, left, function_returns);
    const auto right_type = effective_type(program, right, function_returns);
    if (left_type == ValueType::null_value || right_type == ValueType::null_value ||
        left_type == ValueType::boolean || right_type == ValueType::boolean) {
      return true;
    }
    return left_type != ValueType::list && left_type != ValueType::tuple &&
           right_type != ValueType::list && right_type != ValueType::tuple;
  }
  if (comparison_is_membership(operation)) {
    const auto left_type = effective_type(program, left, function_returns);
    const auto right_type = effective_type(program, right, function_returns);
    if (right_type == ValueType::unknown) return true;
    if (right_type == ValueType::string) return left_type == ValueType::string;
    if (right_type == ValueType::list) {
      if (!right.sequence_elements.empty()) {
        return std::any_of(
            right.sequence_elements.begin(), right.sequence_elements.end(),
            [&](const auto& element) { return compatible_value_types(left_type, element.type); });
      }
      return compatible_value_types(left_type, right.element_type);
    }
    if (right_type == ValueType::tuple) {
      if (!right.sequence_elements.empty()) {
        return std::any_of(
            right.sequence_elements.begin(), right.sequence_elements.end(),
            [&](const auto& element) { return compatible_value_types(left_type, element.type); });
      }
      return std::any_of(right.tuple_types.begin(), right.tuple_types.end(),
                         [&](const auto type) { return compatible_value_types(left_type, type); });
    }
    return false;
  }
  return cpp_expression_types_compatible(program, left, right, function_returns);
}

void validate_expression(const mir::Program& program, const MirExpressionId expression_id,
                         const semantic::Profile& semantics,
                         const FunctionReturnTypes& function_returns,
                         std::vector<Diagnostic>& diagnostics) {
  const auto* expression = mir::expression(program, expression_id);
  if (expression == nullptr) return;
  for (const auto child : expression->children) {
    validate_expression(program, child, semantics, function_returns, diagnostics);
  }
  if (expression->kind == ExpressionKind::list && !list_is_homogeneous(program, *expression)) {
    add_error(diagnostics, expression->location.line, "MPF2020",
              "C++17 target requires a rectangular list with a homogeneous element type");
  }
  if (semantics.logical_result == semantic::LogicalResult::operand &&
      expression->kind == ExpressionKind::binary &&
      (expression->value == "&&" || expression->value == "||") &&
      expression->children.size() == 2) {
    const auto* left = mir::expression(program, expression->children[0]);
    const auto* right = mir::expression(program, expression->children[1]);
    if (left != nullptr && right != nullptr) {
      const auto left_type = effective_type(program, *left, function_returns);
      const auto right_type = effective_type(program, *right, function_returns);
      bool compatible = expression->inferred_type != ValueType::unknown &&
                        left_type != ValueType::unknown && right_type != ValueType::unknown &&
                        (left_type == right_type || (numeric(left_type) && numeric(right_type)));
      if (left_type == ValueType::list && right_type == ValueType::list) {
        compatible =
            left->inferred_type == ValueType::list && right->inferred_type == ValueType::list &&
            left->shape.size() == right->shape.size() && left->element_type == right->element_type;
      }
      if (!compatible) {
        add_error(diagnostics, expression->location.line, "MPF2032",
                  "C++17 requires statically compatible Python and/or result types");
      }
    }
  }
  if (semantics.truthiness == semantic::Truthiness::dynamic &&
      expression->kind == ExpressionKind::binary &&
      expression->comparison != ComparisonOperator::none && expression->children.size() == 2) {
    const auto* left = mir::expression(program, expression->children[0]);
    const auto* right = mir::expression(program, expression->children[1]);
    if (left != nullptr && right != nullptr &&
        !cpp_comparison_supported(program, expression->comparison, *left, *right,
                                  function_returns)) {
      add_error(diagnostics, expression->location.line, "MPF2044",
                comparison_is_identity(expression->comparison)
                    ? "C++17 cannot preserve Python sequence object identity with value containers"
                    : "C++17 cannot compare these Python operand types without changing semantics");
    }
  }
  if (semantics.truthiness == semantic::Truthiness::dynamic &&
      expression->kind == ExpressionKind::comparison_chain) {
    for (std::size_t index = 0; index + 1 < expression->children.size(); ++index) {
      const auto* left = mir::expression(program, expression->children[index]);
      const auto* right = mir::expression(program, expression->children[index + 1U]);
      if (index >= expression->comparisons.size() || left == nullptr || right == nullptr ||
          !cpp_comparison_supported(program, expression->comparisons[index], *left, *right,
                                    function_returns)) {
        add_error(
            diagnostics, expression->location.line, "MPF2044",
            index < expression->comparisons.size() &&
                    comparison_is_identity(expression->comparisons[index])
                ? "C++17 cannot preserve Python sequence object identity in a comparison chain"
                : "C++17 cannot compare these Python chain operand types without changing "
                  "semantics");
        break;
      }
    }
  }
  if (semantics.truthiness == semantic::Truthiness::dynamic &&
      expression->kind == ExpressionKind::conditional && expression->children.size() == 3) {
    const auto* when_true = mir::expression(program, expression->children[1]);
    const auto* when_false = mir::expression(program, expression->children[2]);
    if (when_true != nullptr && when_false != nullptr &&
        !cpp_expression_types_compatible(program, *when_true, *when_false, function_returns)) {
      add_error(diagnostics, expression->location.line, "MPF2044",
                "C++17 requires compatible Python conditional-expression result types");
    }
  }
}

void collect_return_type(const mir::Program& program, const std::vector<MirStatementId>& statements,
                         ValueType& result, bool& has_value, bool& has_empty, bool& incompatible) {
  for (const auto statement_id : statements) {
    const auto* statement = mir::statement(program, statement_id);
    if (statement == nullptr || statement->kind == StatementKind::function) continue;
    if (statement->kind == StatementKind::return_statement) {
      const auto* value = mir::expression(program, statement->expression);
      if (!statement->has_expression || value == nullptr) {
        has_empty = true;
      } else {
        has_value = true;
        const auto joined = join_types(result, value->inferred_type);
        if (result != ValueType::unknown && value->inferred_type != ValueType::unknown &&
            joined == ValueType::unknown) {
          incompatible = true;
        }
        result = joined;
      }
    }
    collect_return_type(program, statement->body, result, has_value, has_empty, incompatible);
    collect_return_type(program, statement->alternative, result, has_value, has_empty,
                        incompatible);
  }
}

bool statements_terminate(const mir::Program& program,
                          const std::vector<MirStatementId>& statements);

bool statement_terminates(const mir::Program& program, const mir::Statement& statement) {
  if (statement.kind == StatementKind::return_statement) return true;
  if (statement.kind == StatementKind::if_statement) {
    return !statement.alternative.empty() && statements_terminate(program, statement.body) &&
           statements_terminate(program, statement.alternative);
  }
  if (statement.kind == StatementKind::select_case) {
    bool has_default = false;
    for (const auto clause_id : statement.body) {
      const auto* clause = mir::statement(program, clause_id);
      if (clause == nullptr) return false;
      has_default = has_default || clause->default_case;
      if (!statements_terminate(program, clause->body)) return false;
    }
    return has_default;
  }
  return false;
}

bool statements_terminate(const mir::Program& program,
                          const std::vector<MirStatementId>& statements) {
  for (const auto statement_id : statements) {
    const auto* statement = mir::statement(program, statement_id);
    if (statement != nullptr && statement_terminates(program, *statement)) return true;
  }
  return false;
}

void validate_statements(const mir::Program& program, const std::vector<MirStatementId>& statements,
                         const semantic::Profile& semantics,
                         const FunctionReturnTypes& function_returns,
                         std::vector<Diagnostic>& diagnostics) {
  for (const auto statement_id : statements) {
    const auto* statement = mir::statement(program, statement_id);
    if (statement == nullptr) continue;
    if (statement->has_expression) {
      validate_expression(program, statement->expression, semantics, function_returns, diagnostics);
    }
    if (statement->has_target_expression) {
      validate_expression(program, statement->target_expression, semantics, function_returns,
                          diagnostics);
    }
    if (statement->has_secondary_expression) {
      validate_expression(program, statement->secondary_expression, semantics, function_returns,
                          diagnostics);
    }
    if (statement->has_tertiary_expression) {
      validate_expression(program, statement->tertiary_expression, semantics, function_returns,
                          diagnostics);
    }
    for (const auto& selector : statement->case_selectors) {
      if (selector.has_lower) {
        validate_expression(program, selector.lower, semantics, function_returns, diagnostics);
      }
      if (selector.has_upper) {
        validate_expression(program, selector.upper, semantics, function_returns, diagnostics);
      }
    }

    const auto* value = mir::expression(program, statement->expression);
    if (statement->kind == StatementKind::assignment && value != nullptr) {
      const auto current_type = value->inferred_type;
      if (statement->previous_type != ValueType::unknown && current_type != ValueType::unknown &&
          join_types(statement->previous_type, current_type) == ValueType::unknown) {
        add_error(diagnostics, statement->line, "MPF2007",
                  "C++17 target cannot represent variable '" + statement->name +
                      "' changing from " + to_string(statement->previous_type) + " to " +
                      to_string(current_type));
      }
      if (current_type == ValueType::list &&
          statement->previous_element_type != ValueType::unknown &&
          value->element_type != ValueType::unknown &&
          join_types(statement->previous_element_type, value->element_type) == ValueType::unknown) {
        add_error(diagnostics, statement->line, "MPF2020",
                  "C++17 target cannot change an array/list element type");
      }
    }

    if (statement->kind == StatementKind::multi_assignment) {
      for (std::size_t index = 0; index < statement->target_names.size(); ++index) {
        const auto previous = index < statement->target_previous_types.size()
                                  ? statement->target_previous_types[index]
                                  : ValueType::unknown;
        const auto current = index < statement->target_types.size() ? statement->target_types[index]
                                                                    : ValueType::unknown;
        if (previous != ValueType::unknown && current != ValueType::unknown &&
            join_types(previous, current) == ValueType::unknown) {
          add_error(diagnostics, statement->line, "MPF2007",
                    "C++17 target cannot represent variable '" + statement->target_names[index] +
                        "' changing from " + to_string(previous) + " to " + to_string(current));
        }
        const auto previous_element = index < statement->target_previous_element_types.size()
                                          ? statement->target_previous_element_types[index]
                                          : ValueType::unknown;
        const auto current_element = index < statement->target_element_types.size()
                                         ? statement->target_element_types[index]
                                         : ValueType::unknown;
        if (previous_element != ValueType::unknown && current_element != ValueType::unknown &&
            join_types(previous_element, current_element) == ValueType::unknown) {
          add_error(diagnostics, statement->line, "MPF2020",
                    "C++17 target cannot change an array/list element type for variable '" +
                        statement->target_names[index] + "'");
        }
      }
      if (statement->has_target_pattern) {
        std::vector<const AssignmentPattern*> leaves;
        collect_assignment_leaves(statement->target_pattern, leaves);
        for (const auto* leaf : leaves) {
          if (leaf->kind == AssignmentPatternKind::starred_name && !leaf->captured_paths.empty() &&
              leaf->element_type == ValueType::unknown) {
            add_error(diagnostics, statement->line, "MPF2020",
                      "C++17 target requires a homogeneous starred capture for variable '" +
                          leaf->name + "'");
          }
        }
      }
    }

    const auto* target = mir::expression(program, statement->target_expression);
    if (statement->kind == StatementKind::indexed_assignment && target != nullptr &&
        value != nullptr && has_direct_slice(program, *target) &&
        value->inferred_type == ValueType::list) {
      if (semantics.resizable_sections && target->element_type != ValueType::unknown &&
          value->element_type != ValueType::unknown &&
          target->element_type != value->element_type) {
        add_error(diagnostics, statement->line, "MPF2020",
                  "C++17 cannot preserve a Python slice assignment that changes element type");
      }
      auto replacement_rank = value->shape.size();
      if (target->column_major && target->shape.size() == 1 && replacement_rank == 2 &&
          (value->shape[0] == 1 || value->shape[1] == 1)) {
        replacement_rank = 1;
      }
      if (replacement_rank != target->shape.size()) {
        add_error(diagnostics, statement->line, "MPF2020",
                  "C++17 section assignment cannot change the container nesting rank");
      }
    }

    if (statement->kind == StatementKind::function) {
      ValueType return_type = ValueType::unknown;
      bool has_value = false;
      bool has_empty = false;
      bool incompatible = false;
      collect_return_type(program, statement->body, return_type, has_value, has_empty,
                          incompatible);
      if (incompatible) {
        add_error(diagnostics, statement->line, "MPF2008",
                  "C++17 target requires all value-returning paths to have a compatible type");
      }
      if (has_value && (has_empty || !statements_terminate(program, statement->body)) &&
          statement->return_names.empty()) {
        add_error(
            diagnostics, statement->line, "MPF2009",
            "C++17 target cannot mix a value return with an empty or fallthrough return path");
      }
    }

    validate_statements(program, statement->body, semantics, function_returns, diagnostics);
    validate_statements(program, statement->alternative, semantics, function_returns, diagnostics);
  }
}

MirFunctionId function_for_origin(const mir::Program& program, const HirNodeId origin) noexcept {
  for (std::size_t index = 1; index < program.functions.size(); ++index) {
    if (program.functions[index].origin == origin) return program.functions[index].id;
  }
  return {};
}

bool function_reaches(const mir::Program& program, const MirFunctionId current,
                      const MirFunctionId target, std::vector<bool>& visited) {
  if (!current.valid() || current.value() >= visited.size() || visited[current.value()]) {
    return false;
  }
  visited[current.value()] = true;
  for (const auto& call : program.calls) {
    if (call.caller != current || !call.callee.valid()) continue;
    if (call.callee == target || function_reaches(program, call.callee, target, visited)) {
      return true;
    }
  }
  return false;
}

bool recursive_function(const mir::Program& program, const MirFunctionId function) {
  std::vector<bool> visited(program.functions.size(), false);
  return function_reaches(program, function, function, visited);
}

}  // namespace

std::vector<Diagnostic> validate_cpp_capabilities(const mir::Program& program,
                                                  const mir::AliasEffectTable& alias_effects) {
  auto diagnostics = mir::alias_effects_current(program, alias_effects)
                         ? std::vector<Diagnostic>{}
                         : mir::verify_alias_effects(program, alias_effects, "cpp-capabilities");
  if (!diagnostics.empty()) return diagnostics;

  FunctionReturnTypes function_returns;
  for (const auto root : program.roots) {
    const auto* function = mir::statement(program, root);
    if (function == nullptr || function->kind != StatementKind::function) continue;
    const auto function_id = function_for_origin(program, function->origin);
    if (recursive_function(program, function_id) && !representable_recursive_return(*function)) {
      add_error(diagnostics, function->line, "MPF2035",
                "C++17 requires a statically representable return type for recursive function '" +
                    function->name + "'");
    }
    ValueType return_type = ValueType::unknown;
    bool has_value = false;
    bool has_empty = false;
    bool incompatible = false;
    collect_return_type(program, function->body, return_type, has_value, has_empty, incompatible);
    if (has_value && !incompatible) {
      function_returns.insert_or_assign(function->name, return_type);
    }
  }
  validate_statements(program, program.roots, program.semantics, function_returns, diagnostics);
  return diagnostics;
}

}  // namespace mpf::detail
