#include "javascript_lir_representation.hpp"

#include <algorithm>
#include <string>
#include <utility>

namespace mpf::detail::javascript {
namespace {

void add_error(std::vector<Diagnostic>& diagnostics, const SourceLocation location,
               std::string message) {
  diagnostics.push_back({DiagnosticSeverity::error, "MPF0007", std::move(message), location});
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
  if (token == "**") return 7;
  return 10;
}

lir::ComparisonPlan comparison_plan(const std::string& token, const lir::EmissionPlan& emission) {
  lir::ComparisonPlan result;
  result.token = token;
  if (emission.structural_equality && token == "===") {
    result.form = lir::ComparisonForm::structural_equal;
  } else if (emission.structural_equality && token == "!==") {
    result.form = lir::ComparisonForm::structural_not_equal;
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
      result.token = "undefined";
      break;
    case ExpressionKind::identifier:
      result.form = expression.binding == BindingKind::builtin ? lir::ExpressionForm::target_symbol
                                                               : lir::ExpressionForm::variable;
      result.token = expression.binding == BindingKind::builtin
                         ? std::string(expression.target_binding.code)
                         : expression.value;
      break;
    case ExpressionKind::number_literal:
    case ExpressionKind::string_literal:
    case ExpressionKind::boolean_literal:
    case ExpressionKind::null_literal:
      result.form = lir::ExpressionForm::literal;
      result.token = expression.value;
      break;
    case ExpressionKind::unary:
      result.precedence = 6;
      result.token = expression.value;
      result.form = emission.dynamic_truthiness && expression.value == "!"
                        ? lir::ExpressionForm::unary_truthiness
                        : lir::ExpressionForm::unary_operator;
      break;
    case ExpressionKind::binary:
      result.precedence = binary_precedence(expression.value);
      result.token = expression.value;
      if (emission.operand_logical_result && expression.value == "&&") {
        result.form = lir::ExpressionForm::binary_lazy_and;
      } else if (emission.operand_logical_result && expression.value == "||") {
        result.form = lir::ExpressionForm::binary_lazy_or;
      } else if (emission.structural_equality && expression.value == "===") {
        result.form = lir::ExpressionForm::binary_structural_equal;
      } else if (emission.structural_equality && expression.value == "!==") {
        result.form = lir::ExpressionForm::binary_structural_not_equal;
      } else if (expression.value == "//") {
        result.form = lir::ExpressionForm::binary_floor_divide;
      } else {
        result.form = lir::ExpressionForm::binary_operator;
      }
      break;
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
      for (std::size_t index = 0; index < expression.argument_transfers.size(); ++index) {
        const auto transfer = expression.argument_transfers[index];
        auto form = lir::CallArgumentForm::value;
        if (argument_transfer_forwards_optional(transfer)) {
          form = lir::CallArgumentForm::forward_optional;
        } else if (argument_transfer_writes(transfer)) {
          const auto scalar_out = (transfer == ArgumentTransfer::mutable_borrow_out ||
                                   transfer == ArgumentTransfer::copy_out) &&
                                  index + 1U < expression.children.size() &&
                                  expression.children[index + 1U].inferred_type != ValueType::list;
          form = scalar_out ? lir::CallArgumentForm::reference_box_uninitialized
                            : lir::CallArgumentForm::reference_box;
        }
        result.call_arguments.push_back(form);
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
      result.index = std::any_of(result.selector_slices.begin(), result.selector_slices.end(),
                                 [](const bool slice) { return slice; })
                         ? lir::IndexForm::section
                         : lir::IndexForm::element;
      break;
    case ExpressionKind::slice: result.form = lir::ExpressionForm::slice; break;
    case ExpressionKind::member:
      result.form = lir::ExpressionForm::member;
      result.precedence = 9;
      result.token = expression.value;
      break;
    case ExpressionKind::list: result.form = lir::ExpressionForm::array; break;
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
      left.first_result != right.first_result || left.string_value != right.string_value) {
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
              "JavaScript LIR expression representation plan is inconsistent");
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

}  // namespace mpf::detail::javascript
