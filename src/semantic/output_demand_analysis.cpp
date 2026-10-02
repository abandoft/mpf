#include "output_demand_analysis.hpp"

#include <vector>

namespace mpf::detail {

void analyze_output_demands(const hir::Program& program, hir::SemanticTable& semantics) {
  if (program.language != SourceLanguage::matlab) return;
  struct PendingExpression {
    const hir::Expression* expression;
    SourceOutputDemand demand;
  };
  std::vector<const hir::Statement*> statements;
  std::vector<PendingExpression> expressions;
  statements.reserve(program.statements.size());
  expressions.reserve(semantics.expressions.size());
  for (const auto& statement : program.statements) statements.push_back(&statement);
  const SourceOutputDemand value{OutputDemandForm::expression, 1U, false};
  const auto append = [&](const hir::Expression& expression, const SourceOutputDemand demand) {
    if (expression.valid()) expressions.push_back({&expression, demand});
  };
  for (std::size_t index = 0U; index < statements.size(); ++index) {
    const auto& statement = *statements[index];
    auto demand = value;
    if (statement.kind == StatementKind::expression)
      demand = {OutputDemandForm::statement, 0U,
                statement.implicit_result != semantic::ImplicitResultPolicy::none};
    else if (statement.kind == StatementKind::multi_assignment)
      demand = {OutputDemandForm::prefix, statement.target_names.size(), false};
    append(statement.expression, demand);
    append(statement.secondary_expression, value);
    append(statement.tertiary_expression, value);
    append(statement.target_expression, value);
    for (const auto& expression : statement.parameter_defaults) append(expression, value);
    for (const auto& call : statement.argument_validator_calls)
      append(call.expression, {OutputDemandForm::validation, 0U, false});
    for (const auto& selector : statement.case_selectors) {
      append(selector.lower, value);
      append(selector.upper, value);
    }
    for (const auto& child : statement.body) statements.push_back(&child);
    for (const auto& child : statement.alternative) statements.push_back(&child);
  }
  for (std::size_t index = 0U; index < expressions.size(); ++index) {
    const auto item = expressions[index];
    const auto& expression = *item.expression;
    if (auto* facts = semantics.expression(expression.id))
      facts->output_demand =
          expression.kind == ExpressionKind::call ? item.demand : SourceOutputDemand{};
    for (const auto& child : expression.children) append(child, value);
  }
}

}  // namespace mpf::detail
