#include "lir_representation.hpp"

#include <algorithm>
#include <string>
#include <utility>

#include "../target_lir_source_segments.hpp"

namespace mpf::detail::cpp {
namespace {

using AccessContext = std::vector<std::pair<std::string, lir::VariableAccess>>;

void add_error(std::vector<Diagnostic>& diagnostics, const SourceLocation location,
               std::string message) {
  diagnostics.push_back({DiagnosticSeverity::error, "MPF0008", std::move(message), location});
}

lir::VariableAccess variable_access(const AccessContext& context,
                                    const std::string& name) noexcept {
  const auto found = std::find_if(context.rbegin(), context.rend(),
                                  [&](const auto& entry) { return entry.first == name; });
  return found == context.rend() ? lir::VariableAccess::direct : found->second;
}

const char* scalar_type(const ValueType type) noexcept {
  switch (type) {
    case ValueType::integer: return "std::int64_t";
    case ValueType::real: return "double";
    case ValueType::boolean: return "bool";
    case ValueType::string: return "std::string";
    case ValueType::null_value: return "std::nullptr_t";
    case ValueType::unknown:
    case ValueType::list:
    case ValueType::tuple:
    case ValueType::function: return "double";
  }
  return "double";
}

std::string container_type(const ValueType element_type, const std::size_t dimensions) {
  std::string result;
  result.reserve(std::string_view(scalar_type(element_type)).size() +
                 dimensions * std::string_view("std::vector<>").size());
  for (std::size_t dimension = 0; dimension < dimensions; ++dimension) {
    result += "std::vector<";
  }
  result += scalar_type(element_type);
  result.append(dimensions, '>');
  return result;
}

int binary_precedence(const std::string& token) noexcept {
  if (token == "||") return 1;
  if (token == "&&") return 2;
  if (token == "===" || token == "!==" || token == "<" || token == "<=" || token == ">" ||
      token == ">=") {
    return 3;
  }
  if (token == "+" || token == "-") return 4;
  if (token == "*" || token == "/" || token == "%" || token == "//") return 5;
  if (token == "**" || token == "^" || token == ".^") return 7;
  return 10;
}

bool has_array_operand(const lir::Expression& expression) noexcept {
  return expression.inferred_type == ValueType::list ||
         std::any_of(
             expression.children.begin(), expression.children.end(),
             [](const lir::Expression& child) { return child.inferred_type == ValueType::list; });
}

std::string matlab_array_helper(const lir::Expression& expression) {
  const bool both_arrays = expression.children.size() == 2U &&
                           expression.children[0].inferred_type == ValueType::list &&
                           expression.children[1].inferred_type == ValueType::list;
  if (expression.operation == BinaryOperator::multiply && both_arrays) {
    return "mpf_runtime::matlab_mtimes";
  }
  if (expression.operation == BinaryOperator::add) return "mpf_runtime::matlab_add";
  if (expression.operation == BinaryOperator::subtract) return "mpf_runtime::matlab_subtract";
  if (expression.operation == BinaryOperator::multiply ||
      expression.operation == BinaryOperator::elementwise_multiply) {
    return "mpf_runtime::matlab_multiply";
  }
  if (expression.operation == BinaryOperator::elementwise_divide) {
    return "mpf_runtime::matlab_divide";
  }
  if (expression.operation == BinaryOperator::elementwise_left_divide) {
    return "mpf_runtime::matlab_left_divide";
  }
  if (expression.operation == BinaryOperator::elementwise_power) {
    return "mpf_runtime::matlab_power";
  }
  switch (expression.comparison) {
    case ComparisonOperator::equal: return "mpf_runtime::matlab_equal";
    case ComparisonOperator::not_equal: return "mpf_runtime::matlab_not_equal";
    case ComparisonOperator::less: return "mpf_runtime::matlab_less";
    case ComparisonOperator::less_equal: return "mpf_runtime::matlab_less_equal";
    case ComparisonOperator::greater: return "mpf_runtime::matlab_greater";
    case ComparisonOperator::greater_equal: return "mpf_runtime::matlab_greater_equal";
    case ComparisonOperator::none:
    case ComparisonOperator::identity:
    case ComparisonOperator::not_identity:
    case ComparisonOperator::contains:
    case ComparisonOperator::not_contains: break;
  }
  return {};
}

lir::ComparisonPlan comparison_plan(const ComparisonOperator operation,
                                    const lir::EmissionPlan& emission) {
  lir::ComparisonPlan result;
  switch (operation) {
    case ComparisonOperator::none: break;
    case ComparisonOperator::equal:
      result.form = emission.dynamic_truthiness ? lir::ComparisonForm::structural_equal
                                                : lir::ComparisonForm::infix;
      result.token = "==";
      break;
    case ComparisonOperator::not_equal:
      result.form = emission.dynamic_truthiness ? lir::ComparisonForm::structural_not_equal
                                                : lir::ComparisonForm::infix;
      result.token = "!=";
      break;
    case ComparisonOperator::less:
      result.form = emission.dynamic_truthiness ? lir::ComparisonForm::dynamic_compare
                                                : lir::ComparisonForm::infix;
      result.token = emission.dynamic_truthiness ? "std::less<>{}" : "<";
      break;
    case ComparisonOperator::less_equal:
      result.form = emission.dynamic_truthiness ? lir::ComparisonForm::dynamic_compare
                                                : lir::ComparisonForm::infix;
      result.token = emission.dynamic_truthiness ? "std::less_equal<>{}" : "<=";
      break;
    case ComparisonOperator::greater:
      result.form = emission.dynamic_truthiness ? lir::ComparisonForm::dynamic_compare
                                                : lir::ComparisonForm::infix;
      result.token = emission.dynamic_truthiness ? "std::greater<>{}" : ">";
      break;
    case ComparisonOperator::greater_equal:
      result.form = emission.dynamic_truthiness ? lir::ComparisonForm::dynamic_compare
                                                : lir::ComparisonForm::infix;
      result.token = emission.dynamic_truthiness ? "std::greater_equal<>{}" : ">=";
      break;
    case ComparisonOperator::identity: result.form = lir::ComparisonForm::identity; break;
    case ComparisonOperator::not_identity: result.form = lir::ComparisonForm::not_identity; break;
    case ComparisonOperator::contains: result.form = lir::ComparisonForm::membership; break;
    case ComparisonOperator::not_contains: result.form = lir::ComparisonForm::not_membership; break;
  }
  return result;
}

lir::CallForm call_form(const lir::Expression& expression) noexcept {
  if (expression.children.empty()) return lir::CallForm::direct;
  const auto& callee = expression.children.front();
  if (callee.kind != ExpressionKind::identifier || callee.binding != BindingKind::builtin) {
    return lir::CallForm::direct;
  }
  if (callee.intrinsic == IntrinsicId::python_float && expression.children.size() == 2) {
    return lir::CallForm::python_float;
  }
  if (callee.intrinsic == IntrinsicId::python_length && expression.children.size() == 2) {
    return lir::CallForm::python_length;
  }
  if (callee.intrinsic == IntrinsicId::matlab_length && expression.children.size() == 2) {
    return lir::CallForm::matlab_length;
  }
  if (callee.intrinsic == IntrinsicId::element_count && expression.children.size() == 2) {
    return lir::CallForm::element_count;
  }
  if (callee.intrinsic == IntrinsicId::sum && expression.children.size() == 2) {
    return lir::CallForm::sum;
  }
  if (callee.intrinsic == IntrinsicId::present && expression.children.size() == 2) {
    return lir::CallForm::present;
  }
  if (callee.intrinsic == IntrinsicId::reshape && expression.children.size() >= 3) {
    return lir::CallForm::reshape;
  }
  return lir::CallForm::direct;
}

lir::ExpressionPlan expected_expression_plan(const lir::Expression& expression,
                                             const lir::EmissionPlan& emission,
                                             const AccessContext& context) {
  lir::ExpressionPlan result;
  if (!expression.valid()) return result;
  result.valid = true;
  result.string_value = expression.inferred_type == ValueType::string;
  switch (expression.kind) {
    case ExpressionKind::invalid: break;
    case ExpressionKind::omitted_argument:
      result.form = lir::ExpressionForm::omitted;
      result.token = "std::nullopt";
      break;
    case ExpressionKind::end_index: result.form = lir::ExpressionForm::invalid; break;
    case ExpressionKind::identifier:
      result.form = expression.binding == BindingKind::builtin ? lir::ExpressionForm::target_symbol
                                                               : lir::ExpressionForm::variable;
      result.token = expression.binding == BindingKind::builtin
                         ? std::string(expression.target_binding.code)
                         : expression.value;
      if (result.form == lir::ExpressionForm::variable) {
        result.variable_access = variable_access(context, expression.value);
      }
      break;
    case ExpressionKind::number_literal:
    case ExpressionKind::boolean_literal:
      result.form = lir::ExpressionForm::scalar_literal;
      result.token = expression.value;
      break;
    case ExpressionKind::string_literal:
      result.form = lir::ExpressionForm::string_literal;
      result.token = expression.value;
      break;
    case ExpressionKind::null_literal:
      result.form = lir::ExpressionForm::null_literal;
      result.token = "nullptr";
      break;
    case ExpressionKind::unary:
      if (expression.unary_operation == UnaryOperator::transpose ||
          expression.unary_operation == UnaryOperator::conjugate_transpose) {
        result.precedence = 9;
        result.token = "mpf_runtime::matlab_transpose";
        result.form = lir::ExpressionForm::matlab_transpose;
      } else {
        result.precedence = 6;
        result.token = expression.value;
        result.form = emission.dynamic_truthiness && expression.value == "!"
                          ? lir::ExpressionForm::unary_truthiness
                          : lir::ExpressionForm::unary_operator;
      }
      break;
    case ExpressionKind::binary: {
      if (expression.array_operation == semantic::ArrayOperation::matlab &&
          has_array_operand(expression)) {
        result.precedence = 9;
        result.token = matlab_array_helper(expression);
        result.form = lir::ExpressionForm::matlab_array_operation;
        result.broadcast = expression.broadcast;
      } else if (expression.comparison != ComparisonOperator::none) {
        result.form = lir::ExpressionForm::binary_comparison;
        result.evaluation = lir::EvaluationForm::binary_comparison_reference_lambda_iife;
        result.precedence = 3;
        result.comparisons.push_back(comparison_plan(expression.comparison, emission));
      } else if (emission.operand_logical_result &&
                 expression.operation == BinaryOperator::logical_and) {
        result.precedence = binary_precedence(expression.value);
        result.token = expression.value;
        result.form = lir::ExpressionForm::binary_lazy_and;
        result.evaluation = lir::EvaluationForm::lazy_reference_lambda_thunks;
      } else if (emission.operand_logical_result &&
                 expression.operation == BinaryOperator::logical_or) {
        result.precedence = binary_precedence(expression.value);
        result.token = expression.value;
        result.form = lir::ExpressionForm::binary_lazy_or;
        result.evaluation = lir::EvaluationForm::lazy_reference_lambda_thunks;
      } else if (expression.operation == BinaryOperator::power ||
                 expression.operation == BinaryOperator::elementwise_power) {
        result.precedence = binary_precedence(expression.value);
        result.token = expression.value;
        result.form = lir::ExpressionForm::binary_power;
      } else if (expression.operation == BinaryOperator::floor_divide) {
        result.precedence = binary_precedence(expression.value);
        result.token = expression.value;
        result.form = lir::ExpressionForm::binary_floor_divide;
      } else if (expression.operation == BinaryOperator::divide && emission.real_division) {
        result.precedence = binary_precedence(expression.value);
        result.token = expression.value;
        result.form = lir::ExpressionForm::binary_real_divide;
      } else if (expression.operation == BinaryOperator::elementwise_left_divide ||
                 expression.operation == BinaryOperator::left_divide) {
        result.precedence = 5;
        result.token = "/";
        result.form = lir::ExpressionForm::binary_reverse_divide;
      } else if (expression.operation == BinaryOperator::elementwise_divide) {
        result.precedence = 5;
        result.token = "/";
        result.form = lir::ExpressionForm::binary_real_divide;
      } else {
        result.precedence = binary_precedence(expression.value);
        result.token =
            expression.operation == BinaryOperator::elementwise_multiply ? "*" : expression.value;
        result.form = lir::ExpressionForm::binary_operator;
      }
      break;
    }
    case ExpressionKind::comparison_chain:
      result.form = lir::ExpressionForm::comparison_chain;
      result.evaluation = lir::EvaluationForm::comparison_reference_lambda_iife;
      result.precedence = 3;
      result.comparisons.reserve(expression.comparisons.size());
      for (const auto operation : expression.comparisons) {
        result.comparisons.push_back(comparison_plan(operation, emission));
      }
      break;
    case ExpressionKind::conditional:
      result.form = lir::ExpressionForm::conditional;
      result.precedence = 0;
      break;
    case ExpressionKind::call:
      result.form = lir::ExpressionForm::call;
      result.precedence = 9;
      result.call = call_form(expression);
      result.call_value = expression.multi_output_call && expression.requested_outputs == 1
                              ? lir::CallValueForm::first_tuple_result
                              : lir::CallValueForm::direct;
      result.call_outcome = expression.procedure_has_result ? lir::CallOutcomeForm::value
                                                            : lir::CallOutcomeForm::discard;
      if (result.call == lir::CallForm::reshape && expression.children.size() > 1U) {
        result.input_shape = expression.children[1].shape;
        result.result_shape = expression.shape;
      }
      result.call_arguments.reserve(expression.argument_transfers.size());
      for (const auto transfer : expression.argument_transfers) {
        lir::CallArgumentPlan argument;
        if (argument_transfer_forwards_optional(transfer)) {
          argument.form = lir::CallArgumentForm::forward_optional;
        } else if (argument_transfer_copies(transfer)) {
          argument.form = lir::CallArgumentForm::copy_section;
          argument.writeback = lir::WritebackForm::section;
          result.evaluation = lir::EvaluationForm::copy_call_reference_lambda_iife;
        }
        result.call_arguments.push_back(argument);
      }
      break;
    case ExpressionKind::index:
      result.form = lir::ExpressionForm::index;
      result.precedence = 9;
      result.selector_slices.reserve(
          expression.children.size() > 0 ? expression.children.size() - 1U : 0U);
      for (std::size_t index = 1; index < expression.children.size(); ++index) {
        result.selector_slices.push_back(expression.children[index].kind == ExpressionKind::slice);
      }
      if (expression.index_selection == semantic::IndexSelection::logical) {
        result.index = lir::IndexForm::logical;
        if (!expression.children.empty()) result.input_shape = expression.children.front().shape;
        if (expression.children.size() > 1U) result.result_shape = expression.children[1].shape;
      } else if (std::any_of(result.selector_slices.begin(), result.selector_slices.end(),
                             [](const bool slice) { return slice; })) {
        if (result.selector_slices.size() == 1) {
          result.index = lir::IndexForm::slice;
          result.flatten_base = expression.column_major && !expression.children.empty() &&
                                expression.children.front().shape.size() > 1;
        } else if (result.selector_slices.size() == 2) {
          if (!result.selector_slices[0]) {
            result.index = lir::IndexForm::row_slice;
          } else if (!result.selector_slices[1]) {
            result.index = lir::IndexForm::column;
          } else {
            result.index = lir::IndexForm::block;
          }
        } else {
          result.index = lir::IndexForm::section_nd;
        }
      } else if (expression.column_major && expression.children.size() == 2 &&
                 expression.children.front().shape.size() > 1) {
        result.index = lir::IndexForm::matrix_linear;
      } else {
        result.index = lir::IndexForm::nested;
      }
      result.index_base = expression.index_base;
      result.allow_negative_index = expression.allow_negative_index;
      result.column_major = expression.column_major;
      break;
    case ExpressionKind::slice:
      result.form = lir::ExpressionForm::slice;
      result.index_base = expression.index_base;
      result.allow_negative_index = expression.allow_negative_index;
      result.inclusive_slice_stop = expression.slice_stop_inclusive;
      break;
    case ExpressionKind::member:
      result.form = lir::ExpressionForm::member;
      result.precedence = 9;
      result.token = expression.value;
      break;
    case ExpressionKind::list:
      result.form = lir::ExpressionForm::list;
      result.concrete_type = container_type(expression.element_type, expression.shape.size());
      result.widen_children.reserve(expression.children.size());
      for (const auto& child : expression.children) {
        result.widen_children.push_back(expression.element_type == ValueType::real &&
                                        child.inferred_type != ValueType::list);
      }
      break;
    case ExpressionKind::tuple: result.form = lir::ExpressionForm::tuple; break;
  }
  return result;
}

bool same_comparison(const lir::ComparisonPlan& left, const lir::ComparisonPlan& right) noexcept {
  return left.form == right.form && left.token == right.token;
}

bool same_call_argument(const lir::CallArgumentPlan& left,
                        const lir::CallArgumentPlan& right) noexcept {
  return left.form == right.form && left.writeback == right.writeback;
}

bool same_plan(const lir::ExpressionPlan& left, const lir::ExpressionPlan& right) noexcept {
  if (left.valid != right.valid || left.form != right.form || left.precedence != right.precedence ||
      left.token != right.token || left.comparisons.size() != right.comparisons.size() ||
      left.broadcast.valid != right.broadcast.valid ||
      left.broadcast.left_shape != right.broadcast.left_shape ||
      left.broadcast.right_shape != right.broadcast.right_shape ||
      left.broadcast.result_shape != right.broadcast.result_shape ||
      left.broadcast.axes != right.broadcast.axes || left.call != right.call ||
      left.evaluation != right.evaluation || left.call_value != right.call_value ||
      left.call_outcome != right.call_outcome ||
      left.call_arguments.size() != right.call_arguments.size() || left.index != right.index ||
      left.selector_slices != right.selector_slices ||
      left.variable_access != right.variable_access || left.index_base != right.index_base ||
      left.allow_negative_index != right.allow_negative_index ||
      left.column_major != right.column_major ||
      left.inclusive_slice_stop != right.inclusive_slice_stop ||
      left.flatten_base != right.flatten_base || left.string_value != right.string_value ||
      left.concrete_type != right.concrete_type || left.widen_children != right.widen_children ||
      left.input_shape != right.input_shape || left.result_shape != right.result_shape) {
    return false;
  }
  for (std::size_t index = 0; index < left.comparisons.size(); ++index) {
    if (!same_comparison(left.comparisons[index], right.comparisons[index])) return false;
  }
  for (std::size_t index = 0; index < left.call_arguments.size(); ++index) {
    if (!same_call_argument(left.call_arguments[index], right.call_arguments[index])) return false;
  }
  return true;
}

void plan_expression(lir::Expression& expression, const lir::EmissionPlan& emission,
                     const AccessContext& context) {
  if (!expression.valid()) return;
  for (auto& child : expression.children) plan_expression(child, emission, context);
  expression.plan = expected_expression_plan(expression, emission, context);
}

void verify_expression(const lir::Expression& expression, const lir::EmissionPlan& emission,
                       const AccessContext& context, const SourceLanguage source_language,
                       std::vector<Diagnostic>& diagnostics) {
  if (!expression.valid()) return;
  const bool requires_matlab_array_operation = source_language == SourceLanguage::matlab &&
                                               expression.kind == ExpressionKind::binary &&
                                               expression.inferred_type == ValueType::list;
  if ((expression.array_operation == semantic::ArrayOperation::matlab) !=
      requires_matlab_array_operation) {
    add_error(diagnostics, expression.location,
              "cpp LIR Matlab array-operation identity is inconsistent");
  }
  if (!same_plan(expression.plan, expected_expression_plan(expression, emission, context))) {
    add_error(diagnostics, expression.location,
              "cpp LIR expression representation plan is inconsistent");
  }
  for (const auto& child : expression.children) {
    verify_expression(child, emission, context, source_language, diagnostics);
  }
}

lir::SelectorForm selector_form(const lir::CaseSelector& selector) noexcept {
  if (!selector.range) return lir::SelectorForm::value;
  if (selector.has_lower && selector.has_upper) return lir::SelectorForm::closed_range;
  return selector.has_lower ? lir::SelectorForm::lower_bound : lir::SelectorForm::upper_bound;
}

std::vector<lir::AssignmentLeafPlan> assignment_leaves(const AssignmentPattern& pattern,
                                                       const AccessContext& context) {
  std::vector<const AssignmentPattern*> leaves;
  collect_assignment_leaves(pattern, leaves);
  std::vector<lir::AssignmentLeafPlan> result;
  result.reserve(leaves.size());
  for (const auto* leaf : leaves) {
    lir::AssignmentLeafPlan plan;
    plan.name = leaf->name;
    plan.access = variable_access(context, leaf->name);
    plan.captured_sequence = leaf->kind != AssignmentPatternKind::name;
    const auto dimensions = std::max<std::size_t>(1, leaf->shape.size());
    if (plan.captured_sequence) {
      plan.concrete_type = container_type(leaf->element_type, dimensions);
      plan.widen_elements = leaf->element_type == ValueType::real && dimensions == 1;
    }
    plan.access_path = leaf->access_path;
    plan.captured_paths = leaf->captured_paths;
    result.push_back(std::move(plan));
  }
  return result;
}

lir::StatementPlan expected_statement_plan(const lir::Statement& statement,
                                           const lir::EmissionPlan& emission,
                                           const AccessContext& context) {
  lir::StatementPlan result;
  result.valid = true;
  switch (statement.kind) {
    case StatementKind::declaration:
      result.form = statement.has_expression ? lir::StatementForm::declaration_initializer
                                             : lir::StatementForm::discard;
      result.target_access = variable_access(context, statement.name);
      break;
    case StatementKind::assignment:
      result.form = lir::StatementForm::assignment;
      result.target_access = variable_access(context, statement.name);
      break;
    case StatementKind::multi_assignment:
      if (statement.has_target_pattern) {
        result.form = lir::StatementForm::multi_pattern;
        result.assignment_leaves = assignment_leaves(statement.target_pattern, context);
      } else {
        result.form = lir::StatementForm::multi_tuple;
        result.targets = statement.target_names;
        result.target_accesses.reserve(result.targets.size());
        for (const auto& name : result.targets) {
          result.target_accesses.push_back(variable_access(context, name));
        }
      }
      break;
    case StatementKind::indexed_assignment:
      result.form = statement.target_expression.plan.index == lir::IndexForm::logical
                        ? lir::StatementForm::indexed_logical_assignment
                    : statement.target_expression.plan.index == lir::IndexForm::slice ||
                            statement.target_expression.plan.index == lir::IndexForm::row_slice ||
                            statement.target_expression.plan.index == lir::IndexForm::column ||
                            statement.target_expression.plan.index == lir::IndexForm::block ||
                            statement.target_expression.plan.index == lir::IndexForm::section_nd
                        ? lir::StatementForm::indexed_section_assignment
                        : lir::StatementForm::indexed_element_assignment;
      result.flatten_replacement =
          statement.target_expression.column_major &&
          statement.target_expression.shape.size() == 1 && statement.expression.shape.size() == 2 &&
          (statement.expression.shape[0] == 1 || statement.expression.shape[1] == 1);
      result.resizable_section = result.form == lir::StatementForm::indexed_section_assignment &&
                                 emission.resizable_sections;
      break;
    case StatementKind::print:
      result.form = !statement.has_expression ? lir::StatementForm::print_empty
                    : statement.expression.plan.form == lir::ExpressionForm::tuple
                        ? lir::StatementForm::print_tuple
                        : lir::StatementForm::print_value;
      break;
    case StatementKind::return_statement:
      result.form = statement.has_expression ? lir::StatementForm::return_value
                                             : lir::StatementForm::return_void;
      break;
    case StatementKind::break_statement: result.form = lir::StatementForm::break_loop; break;
    case StatementKind::continue_statement: result.form = lir::StatementForm::continue_loop; break;
    case StatementKind::expression:
      result.form = statement.expression.valid() ? lir::StatementForm::expression
                                                 : lir::StatementForm::discard;
      break;
    case StatementKind::if_statement:
      result.form = lir::StatementForm::conditional;
      result.condition = emission.dynamic_truthiness ? lir::ConditionForm::runtime_truthy
                                                     : lir::ConditionForm::direct;
      result.has_alternative = !statement.alternative.empty();
      break;
    case StatementKind::select_case:
      result.form = lir::StatementForm::selection;
      result.character_selector =
          statement.expression.plan.string_value && emission.padded_character_selection;
      break;
    case StatementKind::case_clause:
      result.form = lir::StatementForm::case_clause;
      if (!statement.default_case) {
        result.selectors.reserve(statement.case_selectors.size());
        for (const auto& selector : statement.case_selectors) {
          result.selectors.push_back(selector_form(selector));
        }
      }
      break;
    case StatementKind::while_loop:
      result.form = lir::StatementForm::while_loop;
      result.condition = emission.dynamic_truthiness ? lir::ConditionForm::runtime_truthy
                                                     : lir::ConditionForm::direct;
      result.has_alternative = !statement.alternative.empty();
      break;
    case StatementKind::range_loop:
      result.form = lir::StatementForm::range_loop;
      result.target_access = variable_access(context, statement.name);
      result.has_alternative = !statement.alternative.empty();
      result.range_has_step = statement.has_tertiary_expression;
      result.retain_loop_value = statement.retain_last_loop_value;
      result.inclusive_stop = statement.inclusive_stop;
      break;
    case StatementKind::for_loop:
      result.form = lir::StatementForm::for_loop;
      result.condition = lir::ConditionForm::direct;
      result.target_access = variable_access(context, statement.name);
      break;
    case StatementKind::function:
      result.form = lir::StatementForm::function;
      result.return_names = statement.return_names;
      break;
  }
  return result;
}

bool same_access_path(const std::vector<AssignmentAccess>& left,
                      const std::vector<AssignmentAccess>& right) noexcept {
  if (left.size() != right.size()) return false;
  for (std::size_t index = 0; index < left.size(); ++index) {
    if (left[index].index != right[index].index || left[index].list != right[index].list) {
      return false;
    }
  }
  return true;
}

bool same_assignment_leaf(const lir::AssignmentLeafPlan& left,
                          const lir::AssignmentLeafPlan& right) noexcept {
  if (left.name != right.name || left.access != right.access ||
      left.captured_sequence != right.captured_sequence ||
      left.concrete_type != right.concrete_type || left.widen_elements != right.widen_elements ||
      !same_access_path(left.access_path, right.access_path) ||
      left.captured_paths.size() != right.captured_paths.size()) {
    return false;
  }
  for (std::size_t index = 0; index < left.captured_paths.size(); ++index) {
    if (!same_access_path(left.captured_paths[index], right.captured_paths[index])) return false;
  }
  return true;
}

bool same_statement_plan(const lir::StatementPlan& left, const lir::StatementPlan& right) noexcept {
  if (left.valid != right.valid || left.form != right.form || left.condition != right.condition ||
      left.target_access != right.target_access || left.has_alternative != right.has_alternative ||
      left.range_has_step != right.range_has_step ||
      left.retain_loop_value != right.retain_loop_value ||
      left.inclusive_stop != right.inclusive_stop ||
      left.resizable_section != right.resizable_section ||
      left.flatten_replacement != right.flatten_replacement ||
      left.character_selector != right.character_selector || left.targets != right.targets ||
      left.target_accesses != right.target_accesses ||
      left.assignment_leaves.size() != right.assignment_leaves.size() ||
      left.selectors != right.selectors || left.return_names != right.return_names) {
    return false;
  }
  for (std::size_t index = 0; index < left.assignment_leaves.size(); ++index) {
    if (!same_assignment_leaf(left.assignment_leaves[index], right.assignment_leaves[index])) {
      return false;
    }
  }
  return true;
}

AccessContext function_context(const lir::Statement& statement) {
  AccessContext result;
  result.reserve(statement.parameters.size());
  for (std::size_t index = 0; index < statement.parameters.size(); ++index) {
    const auto access = index < statement.function_abi.parameters.size() &&
                                statement.function_abi.parameters[index].passing ==
                                    lir::ParameterPassing::optional_reference
                            ? lir::VariableAccess::optional_value
                            : lir::VariableAccess::direct;
    result.emplace_back(statement.parameters[index], access);
  }
  return result;
}

void plan_statements(std::vector<lir::Statement>& statements, const lir::EmissionPlan& emission,
                     const AccessContext& context) {
  for (auto& statement : statements) {
    const auto nested_context =
        statement.kind == StatementKind::function ? function_context(statement) : context;
    const auto& expression_context =
        statement.kind == StatementKind::function ? nested_context : context;
    plan_expression(statement.expression, emission, expression_context);
    plan_expression(statement.secondary_expression, emission, expression_context);
    plan_expression(statement.tertiary_expression, emission, expression_context);
    plan_expression(statement.target_expression, emission, expression_context);
    for (auto& expression : statement.parameter_defaults) {
      plan_expression(expression, emission, expression_context);
    }
    for (auto& selector : statement.case_selectors) {
      plan_expression(selector.lower, emission, expression_context);
      plan_expression(selector.upper, emission, expression_context);
    }
    statement.plan = expected_statement_plan(statement, emission, context);
    plan_statements(statement.body, emission, nested_context);
    plan_statements(statement.alternative, emission, nested_context);
  }
}

void verify_statements(const std::vector<lir::Statement>& statements,
                       const lir::EmissionPlan& emission, const AccessContext& context,
                       const SourceLanguage source_language, std::vector<Diagnostic>& diagnostics) {
  for (const auto& statement : statements) {
    const auto nested_context =
        statement.kind == StatementKind::function ? function_context(statement) : context;
    const auto& expression_context =
        statement.kind == StatementKind::function ? nested_context : context;
    verify_expression(statement.expression, emission, expression_context, source_language,
                      diagnostics);
    verify_expression(statement.secondary_expression, emission, expression_context, source_language,
                      diagnostics);
    verify_expression(statement.tertiary_expression, emission, expression_context, source_language,
                      diagnostics);
    verify_expression(statement.target_expression, emission, expression_context, source_language,
                      diagnostics);
    for (const auto& expression : statement.parameter_defaults) {
      verify_expression(expression, emission, expression_context, source_language, diagnostics);
    }
    for (const auto& selector : statement.case_selectors) {
      verify_expression(selector.lower, emission, expression_context, source_language, diagnostics);
      verify_expression(selector.upper, emission, expression_context, source_language, diagnostics);
    }
    if (!same_statement_plan(statement.plan,
                             expected_statement_plan(statement, emission, context))) {
      add_error(diagnostics, {statement.line, 1},
                "cpp LIR statement representation plan is inconsistent");
    }
    verify_statements(statement.body, emission, nested_context, source_language, diagnostics);
    verify_statements(statement.alternative, emission, nested_context, source_language,
                      diagnostics);
  }
}

}  // namespace

void plan_lir_representation(lir::SemanticProgram& program) {
  plan_statements(program.statements, program.emission, {});
  program.source_segments = build_source_segment_plan(program.statements, program.node_count);
}

void verify_lir_representation(const lir::SemanticProgram& program,
                               std::vector<Diagnostic>& diagnostics) {
  verify_statements(program.statements, program.emission, {}, program.source_language, diagnostics);
  const auto expected = build_source_segment_plan(program.statements, program.node_count);
  if (!same_source_segment_plan(program.source_segments, expected)) {
    add_error(diagnostics, {1, 1}, "cpp LIR source segment plan is inconsistent");
  }
}

}  // namespace mpf::detail::cpp
