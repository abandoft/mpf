#include "matlab_bare_calls.hpp"

#include <utility>
#include <vector>

namespace mpf::detail {

bool normalize_matlab_bare_calls(hir::Program& program, const NameTable& names) {
  if (program.language != SourceLanguage::matlab) return false;
  struct Pending {
    hir::Expression* expression;
    hir::Statement* implicit_owner;
    bool callee;
  };
  std::vector<hir::Statement*> statements;
  std::vector<Pending> expressions;
  statements.reserve(program.statements.size());
  for (auto& statement : program.statements) statements.push_back(&statement);
  const auto append = [&](hir::Expression& expression, hir::Statement* implicit_owner = nullptr,
                          const bool callee = false) {
    if (expression.valid()) expressions.push_back({&expression, implicit_owner, callee});
  };
  for (std::size_t index = 0U; index < statements.size(); ++index) {
    auto& statement = *statements[index];
    append(statement.expression,
           statement.kind == StatementKind::expression ? &statement : nullptr);
    append(statement.secondary_expression);
    append(statement.tertiary_expression);
    append(statement.target_expression);
    for (auto& expression : statement.parameter_defaults) append(expression);
    for (auto& call : statement.argument_validator_calls) append(call.expression);
    for (auto& selector : statement.case_selectors) {
      append(selector.lower);
      append(selector.upper);
    }
    for (auto& child : statement.body) statements.push_back(&child);
    for (auto& child : statement.alternative) statements.push_back(&child);
  }
  bool changed = false;
  for (std::size_t index = 0U; index < expressions.size(); ++index) {
    const auto work = expressions[index];
    auto& expression = *work.expression;
    const auto* use = names.reference(expression.id);
    if (!work.callee && expression.kind == ExpressionKind::identifier && use != nullptr &&
        use->binding == BindingKind::function) {
      auto callee = std::move(expression);
      expression = {};
      expression.location = callee.location;
      expression.kind = ExpressionKind::call;
      expression.children.push_back(std::move(callee));
      if (work.implicit_owner != nullptr) {
        work.implicit_owner->name = "ans";
        work.implicit_owner->implicit_result = semantic::ImplicitResultPolicy::matlab_ans_if_value;
      }
      changed = true;
    }
    for (std::size_t child = 0U; child < expression.children.size(); ++child)
      append(expression.children[child], nullptr,
             expression.kind == ExpressionKind::call && child == 0U);
  }
  return changed;
}

}  // namespace mpf::detail
