#include "cpp_lir_representation.hpp"

#include <algorithm>
#include <string>
#include <utility>

namespace mpf::detail::cpp {
namespace {

void add_error(std::vector<Diagnostic>& diagnostics, const SourceLocation location,
               std::string message) {
  diagnostics.push_back({DiagnosticSeverity::error, "MPF0008", std::move(message), location});
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
  return 10;
}

std::string comparison_token(const std::string& token) {
  if (token == "===" || token == "==") return "std::equal_to<>{}";
  if (token == "!==" || token == "!=") return "std::not_equal_to<>{}";
  if (token == "<") return "std::less<>{}";
  if (token == "<=") return "std::less_equal<>{}";
  if (token == ">") return "std::greater<>{}";
  if (token == ">=") return "std::greater_equal<>{}";
  return {};
}

std::string operator_token(const std::string& token) {
  if (token == "===") return "==";
  if (token == "!==") return "!=";
  return token;
}

lir::ComparisonPlan comparison_plan(const std::string& token, const lir::EmissionPlan& emission) {
  lir::ComparisonPlan result;
  result.token = operator_token(token);
  if (emission.dynamic_truthiness) {
    const auto dynamic = comparison_token(token);
    if (!dynamic.empty()) {
      result.form = lir::ComparisonForm::dynamic_compare;
      result.token = dynamic;
    }
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

lir::ExpressionPlan expected_plan(const lir::Expression& expression,
                                  const lir::EmissionPlan& emission) {
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
    case ExpressionKind::identifier:
      result.form = expression.binding == BindingKind::builtin ? lir::ExpressionForm::target_symbol
                                                               : lir::ExpressionForm::variable;
      result.token = expression.binding == BindingKind::builtin
                         ? std::string(expression.target_binding.code)
                         : expression.value;
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
      result.precedence = 6;
      result.token = expression.value;
      result.form = emission.dynamic_truthiness && expression.value == "!"
                        ? lir::ExpressionForm::unary_truthiness
                        : lir::ExpressionForm::unary_operator;
      break;
    case ExpressionKind::binary: {
      result.precedence = binary_precedence(expression.value);
      result.token = operator_token(expression.value);
      const auto comparator =
          emission.dynamic_truthiness ? comparison_token(expression.value) : std::string{};
      if (emission.operand_logical_result && expression.value == "&&") {
        result.form = lir::ExpressionForm::binary_lazy_and;
      } else if (emission.operand_logical_result && expression.value == "||") {
        result.form = lir::ExpressionForm::binary_lazy_or;
      } else if (expression.value == "**") {
        result.form = lir::ExpressionForm::binary_power;
      } else if (expression.value == "//") {
        result.form = lir::ExpressionForm::binary_floor_divide;
      } else if (expression.value == "/" && emission.real_division) {
        result.form = lir::ExpressionForm::binary_real_divide;
      } else if (!comparator.empty()) {
        result.form = lir::ExpressionForm::binary_dynamic_compare;
        result.token = comparator;
      } else {
        result.form = lir::ExpressionForm::binary_operator;
      }
      break;
    }
    case ExpressionKind::comparison_chain:
      result.form = lir::ExpressionForm::comparison_chain;
      result.precedence = 3;
      result.comparisons.reserve(expression.operators.size());
      for (const auto& token : expression.operators) {
        result.comparisons.push_back(comparison_plan(token, emission));
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
      result.first_result = expression.multi_output_call && expression.requested_outputs == 1;
      result.call_arguments.reserve(expression.argument_transfers.size());
      for (const auto transfer : expression.argument_transfers) {
        result.call_arguments.push_back(
            argument_transfer_forwards_optional(transfer) ? lir::CallArgumentForm::forward_optional
            : argument_transfer_copies(transfer)          ? lir::CallArgumentForm::copy_section
                                                          : lir::CallArgumentForm::value);
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
      if (std::any_of(result.selector_slices.begin(), result.selector_slices.end(),
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
      break;
    case ExpressionKind::slice: result.form = lir::ExpressionForm::slice; break;
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

bool same_plan(const lir::ExpressionPlan& left, const lir::ExpressionPlan& right) noexcept {
  if (left.valid != right.valid || left.form != right.form || left.precedence != right.precedence ||
      left.token != right.token || left.comparisons.size() != right.comparisons.size() ||
      left.call != right.call || left.call_arguments != right.call_arguments ||
      left.index != right.index || left.selector_slices != right.selector_slices ||
      left.flatten_base != right.flatten_base || left.first_result != right.first_result ||
      left.string_value != right.string_value || left.concrete_type != right.concrete_type ||
      left.widen_children != right.widen_children) {
    return false;
  }
  for (std::size_t index = 0; index < left.comparisons.size(); ++index) {
    if (!same_comparison(left.comparisons[index], right.comparisons[index])) return false;
  }
  return true;
}

void plan_expression(lir::Expression& expression, const lir::EmissionPlan& emission) {
  if (!expression.valid()) return;
  expression.plan = expected_plan(expression, emission);
  for (auto& child : expression.children) plan_expression(child, emission);
}

void verify_expression(const lir::Expression& expression, const lir::EmissionPlan& emission,
                       std::vector<Diagnostic>& diagnostics) {
  if (!expression.valid()) return;
  if (!same_plan(expression.plan, expected_plan(expression, emission))) {
    add_error(diagnostics, expression.location,
              "cpp LIR expression representation plan is inconsistent");
  }
  for (const auto& child : expression.children) verify_expression(child, emission, diagnostics);
}

template <typename Function>
void visit_statement_expressions(std::vector<lir::Statement>& statements,
                                 const Function& function) {
  for (auto& statement : statements) {
    function(statement.expression);
    function(statement.secondary_expression);
    function(statement.tertiary_expression);
    function(statement.target_expression);
    for (auto& expression : statement.parameter_defaults) function(expression);
    for (auto& selector : statement.case_selectors) {
      function(selector.lower);
      function(selector.upper);
    }
    visit_statement_expressions(statement.body, function);
    visit_statement_expressions(statement.alternative, function);
  }
}

template <typename Function>
void visit_statement_expressions(const std::vector<lir::Statement>& statements,
                                 const Function& function) {
  for (const auto& statement : statements) {
    function(statement.expression);
    function(statement.secondary_expression);
    function(statement.tertiary_expression);
    function(statement.target_expression);
    for (const auto& expression : statement.parameter_defaults) function(expression);
    for (const auto& selector : statement.case_selectors) {
      function(selector.lower);
      function(selector.upper);
    }
    visit_statement_expressions(statement.body, function);
    visit_statement_expressions(statement.alternative, function);
  }
}

}  // namespace

void plan_lir_representation(lir::SemanticProgram& program) {
  visit_statement_expressions(program.statements, [&](lir::Expression& expression) {
    plan_expression(expression, program.emission);
  });
}

void verify_lir_representation(const lir::SemanticProgram& program,
                               std::vector<Diagnostic>& diagnostics) {
  visit_statement_expressions(program.statements, [&](const lir::Expression& expression) {
    verify_expression(expression, program.emission, diagnostics);
  });
}

}  // namespace mpf::detail::cpp
