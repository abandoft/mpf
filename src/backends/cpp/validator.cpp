#include "validator.hpp"

#include <algorithm>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "semantic/analyzer.hpp"

namespace mpf::detail {
namespace {

using FunctionReturnTypes = std::unordered_map<std::string, ValueType>;
using AssignmentTypeProbes = std::unordered_map<SymbolId, const mir::Expression*>;
using IncompatibleAssignments = std::unordered_set<SymbolId>;

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

bool representable_type(const mir::Program& program, const TypeId id) {
  const auto* type = mir::type(program, id);
  if (type == nullptr) return false;
  if (type->kind != mir::TypeKind::tuple) return primitive(mir::value_type(program, id));
  return std::all_of(type->elements.begin(), type->elements.end(),
                     [&](const TypeId element) { return representable_type(program, element); });
}

bool representable_recursive_return(const mir::Program& program, const mir::Function& function,
                                    const mir::Statement& statement) {
  if (function.result_types.empty()) return true;
  if (!statement.return_names.empty() && function.result_types.size() > 1U &&
      function.result_types.size() != statement.return_names.size()) {
    return false;
  }
  return std::all_of(function.result_types.begin(), function.result_types.end(),
                     [&](const TypeId result) { return representable_type(program, result); });
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
    const auto child_type = mir::value_type(program, child->type_id);
    nested = nested && child_type == ValueType::list;
    has_nested = has_nested || child_type == ValueType::list;
    if (child_type == ValueType::list) {
      const auto* child_shape = mir::shape(program, child->shape_id);
      if (child_shape == nullptr) return false;
      if (nested_shape.empty())
        nested_shape = child_shape->extents;
      else if (nested_shape != child_shape->extents)
        return false;
    }
    const auto scalar_type =
        child_type == ValueType::list ? mir::element_type(program, child->type_id) : child_type;
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
                         const FunctionReturnTypes&) {
  return mir::value_type(program, expression.type_id);
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
    const auto* left_shape = mir::shape(program, left.shape_id);
    const auto* right_shape = mir::shape(program, right.shape_id);
    return left_shape != nullptr && right_shape != nullptr &&
           left_shape->extents.size() == right_shape->extents.size() &&
           mir::element_type(program, left.type_id) == mir::element_type(program, right.type_id);
  }
  if (left_type == ValueType::tuple && right_type == ValueType::tuple) {
    const auto* left_type_data = mir::type(program, left.type_id);
    const auto* right_type_data = mir::type(program, right.type_id);
    const auto* left_attributes = mir::attributes(program, left.id);
    const auto* right_attributes = mir::attributes(program, right.id);
    return left_type_data != nullptr && right_type_data != nullptr && left_attributes != nullptr &&
           right_attributes != nullptr && left_type_data->elements == right_type_data->elements &&
           left_attributes->tuple_shapes == right_attributes->tuple_shapes;
  }
  return false;
}

bool cpp_declaration_probes_compatible(const mir::Program& program, const mir::Expression& left,
                                       const mir::Expression& right,
                                       const FunctionReturnTypes& function_returns) {
  if (left.id == right.id) return true;
  if (left.kind == ExpressionKind::identifier && right.kind == ExpressionKind::identifier) {
    if (left.symbol_id.valid() || right.symbol_id.valid()) return left.symbol_id == right.symbol_id;
    const auto* left_attributes = mir::attributes(program, left.id);
    const auto* right_attributes = mir::attributes(program, right.id);
    return left_attributes != nullptr && right_attributes != nullptr &&
           left_attributes->spelling == right_attributes->spelling;
  }
  const auto left_type = effective_type(program, left, function_returns);
  const auto right_type = effective_type(program, right, function_returns);
  if (left_type != ValueType::unknown || right_type != ValueType::unknown) {
    return left_type != ValueType::unknown && right_type != ValueType::unknown &&
           cpp_expression_types_compatible(program, left, right, function_returns);
  }
  if (left.kind != ExpressionKind::call || right.kind != ExpressionKind::call ||
      left.children.size() != right.children.size() || left.children.empty()) {
    return false;
  }
  const auto* left_callee = mir::expression(program, left.children.front());
  const auto* right_callee = mir::expression(program, right.children.front());
  if (left_callee == nullptr || right_callee == nullptr ||
      !cpp_declaration_probes_compatible(program, *left_callee, *right_callee, function_returns)) {
    return false;
  }
  for (std::size_t index = 1; index < left.children.size(); ++index) {
    const auto* left_argument = mir::expression(program, left.children[index]);
    const auto* right_argument = mir::expression(program, right.children[index]);
    if (left_argument == nullptr || right_argument == nullptr ||
        left_argument->type_id != right_argument->type_id) {
      return false;
    }
    const auto* left_shape = mir::shape(program, left_argument->shape_id);
    const auto* right_shape = mir::shape(program, right_argument->shape_id);
    if ((left_shape == nullptr) != (right_shape == nullptr) ||
        (left_shape != nullptr && left_shape->extents.size() != right_shape->extents.size())) {
      return false;
    }
  }
  return true;
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
    const auto* right_attributes = mir::attributes(program, right.id);
    if (right_type == ValueType::list) {
      if (right_attributes != nullptr && !right_attributes->sequence_elements.empty()) {
        return std::any_of(right_attributes->sequence_elements.begin(),
                           right_attributes->sequence_elements.end(), [&](const auto& element) {
                             return compatible_value_types(left_type,
                                                           mir::value_type(program, element.type));
                           });
      }
      return compatible_value_types(left_type, mir::element_type(program, right.type_id));
    }
    if (right_type == ValueType::tuple) {
      if (right_attributes != nullptr && !right_attributes->sequence_elements.empty()) {
        return std::any_of(right_attributes->sequence_elements.begin(),
                           right_attributes->sequence_elements.end(), [&](const auto& element) {
                             return compatible_value_types(left_type,
                                                           mir::value_type(program, element.type));
                           });
      }
      const auto* tuple = mir::type(program, right.type_id);
      return tuple != nullptr &&
             std::any_of(tuple->elements.begin(), tuple->elements.end(), [&](const auto type) {
               return compatible_value_types(left_type, mir::value_type(program, type));
             });
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
  const auto* attributes = mir::attributes(program, expression_id);
  if (expression == nullptr || attributes == nullptr) return;
  for (const auto child : expression->children) {
    validate_expression(program, child, semantics, function_returns, diagnostics);
  }
  if (expression->kind == ExpressionKind::list && !list_is_homogeneous(program, *expression)) {
    add_error(diagnostics, expression->location.line, "MPF2020",
              "C++17 target requires a rectangular list with a homogeneous element type");
  }
  if (semantics.logical_result == semantic::LogicalResult::operand &&
      expression->kind == ExpressionKind::binary &&
      (attributes->operation == BinaryOperator::logical_and ||
       attributes->operation == BinaryOperator::logical_or) &&
      expression->children.size() == 2) {
    const auto* left = mir::expression(program, expression->children[0]);
    const auto* right = mir::expression(program, expression->children[1]);
    if (left != nullptr && right != nullptr) {
      const auto left_type = effective_type(program, *left, function_returns);
      const auto right_type = effective_type(program, *right, function_returns);
      bool compatible = mir::value_type(program, expression->type_id) != ValueType::unknown &&
                        left_type != ValueType::unknown && right_type != ValueType::unknown &&
                        (left_type == right_type || (numeric(left_type) && numeric(right_type)));
      if (left_type == ValueType::list && right_type == ValueType::list) {
        const auto* left_shape = mir::shape(program, left->shape_id);
        const auto* right_shape = mir::shape(program, right->shape_id);
        compatible =
            left_shape != nullptr && right_shape != nullptr &&
            left_shape->extents.size() == right_shape->extents.size() &&
            mir::element_type(program, left->type_id) == mir::element_type(program, right->type_id);
      }
      if (!compatible) {
        add_error(diagnostics, expression->location.line, "MPF2032",
                  "C++17 requires statically compatible Python and/or result types");
      }
    }
  }
  if (semantics.truthiness == semantic::Truthiness::dynamic &&
      expression->kind == ExpressionKind::binary &&
      attributes->comparison != ComparisonOperator::none && expression->children.size() == 2) {
    const auto* left = mir::expression(program, expression->children[0]);
    const auto* right = mir::expression(program, expression->children[1]);
    if (left != nullptr && right != nullptr &&
        !cpp_comparison_supported(program, attributes->comparison, *left, *right,
                                  function_returns)) {
      add_error(diagnostics, expression->location.line, "MPF2044",
                comparison_is_identity(attributes->comparison)
                    ? "C++17 cannot preserve Python sequence object identity with value containers"
                    : "C++17 cannot compare these Python operand types without changing semantics");
    }
  }
  if (semantics.truthiness == semantic::Truthiness::dynamic &&
      expression->kind == ExpressionKind::comparison_chain) {
    for (std::size_t index = 0; index + 1 < expression->children.size(); ++index) {
      const auto* left = mir::expression(program, expression->children[index]);
      const auto* right = mir::expression(program, expression->children[index + 1U]);
      if (index >= attributes->comparisons.size() || left == nullptr || right == nullptr ||
          !cpp_comparison_supported(program, attributes->comparisons[index], *left, *right,
                                    function_returns)) {
        add_error(
            diagnostics, expression->location.line, "MPF2044",
            index < attributes->comparisons.size() &&
                    comparison_is_identity(attributes->comparisons[index])
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
        const auto value_type = mir::value_type(program, value->type_id);
        const auto joined = join_types(result, value_type);
        if (result != ValueType::unknown && value_type != ValueType::unknown &&
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

void collect_assignment_leaves(const mir::AssignmentPattern& pattern,
                               std::vector<const mir::AssignmentPattern*>& leaves) {
  if (pattern.kind == AssignmentPatternKind::name ||
      pattern.kind == AssignmentPatternKind::starred_name) {
    leaves.push_back(&pattern);
    return;
  }
  for (const auto& child : pattern.children) collect_assignment_leaves(child, leaves);
}

void validate_statements(const mir::Program& program, const std::vector<MirStatementId>& statements,
                         const semantic::Profile& semantics,
                         const FunctionReturnTypes& function_returns,
                         AssignmentTypeProbes& assignment_type_probes,
                         IncompatibleAssignments& incompatible_assignments,
                         std::vector<Diagnostic>& diagnostics) {
  for (const auto statement_id : statements) {
    const auto* statement = mir::statement(program, statement_id);
    const auto* attributes = mir::attributes(program, statement_id);
    if (statement == nullptr || attributes == nullptr) continue;
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
    const bool implicit_result_assignment =
        attributes->implicit_result != semantic::ImplicitResultPolicy::none &&
        attributes->implicit_result_has_value;
    if ((statement->kind == StatementKind::assignment || implicit_result_assignment) &&
        value != nullptr) {
      if (statement->symbol_id.valid()) {
        const auto [probe, inserted] = assignment_type_probes.emplace(statement->symbol_id, value);
        if (!inserted && !attributes->previous_assigned &&
            !cpp_declaration_probes_compatible(program, *probe->second, *value, function_returns) &&
            incompatible_assignments.insert(statement->symbol_id).second) {
          add_error(diagnostics, statement->line, "MPF2007",
                    "C++17 target cannot represent variable '" + statement->name +
                        "' with incompatible types across control-flow paths");
        }
      }
      const auto current_type = mir::value_type(program, value->type_id);
      const auto previous_type = mir::value_type(program, attributes->previous_type);
      const bool incompatible_known_types =
          previous_type != ValueType::unknown && current_type != ValueType::unknown &&
          join_types(previous_type, current_type) == ValueType::unknown;
      const bool unresolved_previous_type_change =
          implicit_result_assignment && attributes->previous_assigned &&
          previous_type == ValueType::unknown && current_type != ValueType::unknown;
      if (incompatible_known_types || unresolved_previous_type_change) {
        add_error(diagnostics, statement->line, "MPF2007",
                  "C++17 target cannot represent variable '" + statement->name +
                      "' changing from " + to_string(previous_type) + " to " +
                      to_string(current_type));
      }
      const auto previous_element = mir::element_type(program, attributes->previous_type);
      const auto value_element = mir::element_type(program, value->type_id);
      if (current_type == ValueType::list && previous_element != ValueType::unknown &&
          value_element != ValueType::unknown &&
          join_types(previous_element, value_element) == ValueType::unknown) {
        add_error(diagnostics, statement->line, "MPF2020",
                  "C++17 target cannot change an array/list element type");
      }
    }

    if (statement->kind == StatementKind::multi_assignment) {
      for (std::size_t index = 0; index < statement->target_names.size(); ++index) {
        const auto previous =
            index < attributes->targets.size()
                ? mir::value_type(program, attributes->targets[index].previous_type)
                : ValueType::unknown;
        const auto current = index < attributes->targets.size()
                                 ? mir::value_type(program, attributes->targets[index].type)
                                 : ValueType::unknown;
        if (previous != ValueType::unknown && current != ValueType::unknown &&
            join_types(previous, current) == ValueType::unknown) {
          add_error(diagnostics, statement->line, "MPF2007",
                    "C++17 target cannot represent variable '" + statement->target_names[index] +
                        "' changing from " + to_string(previous) + " to " + to_string(current));
        }
        const auto previous_element =
            index < attributes->targets.size()
                ? mir::element_type(program, attributes->targets[index].previous_type)
                : ValueType::unknown;
        const auto current_element =
            index < attributes->targets.size()
                ? mir::element_type(program, attributes->targets[index].type)
                : ValueType::unknown;
        if (previous_element != ValueType::unknown && current_element != ValueType::unknown &&
            join_types(previous_element, current_element) == ValueType::unknown) {
          add_error(diagnostics, statement->line, "MPF2020",
                    "C++17 target cannot change an array/list element type for variable '" +
                        statement->target_names[index] + "'");
        }
      }
      if (statement->has_target_pattern) {
        std::vector<const mir::AssignmentPattern*> leaves;
        collect_assignment_leaves(attributes->target_pattern, leaves);
        for (const auto* leaf : leaves) {
          if (leaf->kind == AssignmentPatternKind::starred_name && !leaf->captured_paths.empty() &&
              mir::element_type(program, leaf->type) == ValueType::unknown) {
            add_error(diagnostics, statement->line, "MPF2020",
                      "C++17 target requires a homogeneous starred capture for variable '" +
                          leaf->name + "'");
          }
        }
      }
    }

    const auto* target = mir::expression(program, statement->target_expression);
    if (statement->kind == StatementKind::indexed_assignment && target != nullptr &&
        attributes->indexed_mutation.contract.kind != semantic::IndexedMutationKind::erase &&
        value != nullptr && has_direct_slice(program, *target) &&
        mir::value_type(program, value->type_id) == ValueType::list) {
      const auto target_element = mir::element_type(program, target->type_id);
      const auto value_element = mir::element_type(program, value->type_id);
      if (semantics.resizable_sections && target_element != ValueType::unknown &&
          value_element != ValueType::unknown && target_element != value_element) {
        add_error(diagnostics, statement->line, "MPF2020",
                  "C++17 cannot preserve a Python slice assignment that changes element type");
      }
      const auto* value_shape = mir::shape(program, value->shape_id);
      const auto* target_shape = mir::shape(program, target->shape_id);
      if (value_shape == nullptr || target_shape == nullptr) continue;
      auto replacement_rank = value_shape->extents.size();
      if (mir::column_major(program, target->shape_id) && target_shape->extents.size() == 1 &&
          replacement_rank == 2 && (value_shape->extents[0] == 1 || value_shape->extents[1] == 1)) {
        replacement_rank = 1;
      }
      if (replacement_rank != target_shape->extents.size()) {
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

    validate_statements(program, statement->body, semantics, function_returns,
                        assignment_type_probes, incompatible_assignments, diagnostics);
    validate_statements(program, statement->alternative, semantics, function_returns,
                        assignment_type_probes, incompatible_assignments, diagnostics);
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
    const auto* function_data =
        function_id.valid() && function_id.value() < program.functions.size()
            ? &program.functions[function_id.value()]
            : nullptr;
    if (function_data != nullptr && recursive_function(program, function_id) &&
        !representable_recursive_return(program, *function_data, *function)) {
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
  AssignmentTypeProbes assignment_type_probes;
  IncompatibleAssignments incompatible_assignments;
  validate_statements(program, program.roots, program.semantics, function_returns,
                      assignment_type_probes, incompatible_assignments, diagnostics);
  return diagnostics;
}

}  // namespace mpf::detail
