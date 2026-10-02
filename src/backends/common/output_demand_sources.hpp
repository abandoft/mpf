#pragma once

#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "compiler/expression_ast.hpp"
#include "compiler/output_demand.hpp"
#include "compiler/statement_kind.hpp"
#include "ir/semantics.hpp"
#include "mpf/transpiler.hpp"

namespace mpf::detail {

// Reconstruct call context from owned target source nodes; never trust a mirrored MIR demand
// merely because the private representation plan was built from the same mirror.
template <typename Program>
void verify_output_demand_sources(const Program& program, std::vector<Diagnostic>& diagnostics) {
  using TargetStatement = typename std::decay_t<decltype(program.statements)>::value_type;
  using TargetExpression = std::decay_t<decltype(std::declval<TargetStatement>().expression)>;
  struct Pending {
    const TargetExpression* expression;
    SourceOutputDemand expected;
  };
  std::vector<const TargetStatement*> statements;
  std::vector<Pending> pending;
  statements.reserve(program.statements.size());
  pending.reserve(program.node_count);
  for (const auto& statement : program.statements) statements.push_back(&statement);
  const auto value = program.source_language == SourceLanguage::matlab
                         ? SourceOutputDemand{OutputDemandForm::expression, 1U, false}
                         : SourceOutputDemand{};
  const auto append = [&](const TargetExpression& expression, const SourceOutputDemand expected) {
    if (expression.valid()) pending.push_back({&expression, expected});
  };
  for (std::size_t index = 0U; index < statements.size(); ++index) {
    const auto& statement = *statements[index];
    auto root = value;
    if (program.source_language == SourceLanguage::matlab) {
      if (statement.kind == StatementKind::expression)
        root = {OutputDemandForm::statement, 0U,
                statement.implicit_result != semantic::ImplicitResultPolicy::none};
      else if (statement.kind == StatementKind::multi_assignment)
        root = {OutputDemandForm::prefix, statement.target_names.size(), false};
    }
    append(statement.expression, root);
    append(statement.secondary_expression, value);
    append(statement.tertiary_expression, value);
    append(statement.target_expression, value);
    for (const auto& expression : statement.parameter_defaults) append(expression, value);
    for (const auto& selector : statement.case_selectors) {
      append(selector.lower, value);
      append(selector.upper, value);
    }
    for (const auto& child : statement.body) statements.push_back(&child);
    for (const auto& child : statement.alternative) statements.push_back(&child);
  }
  for (std::size_t index = 0U; index < pending.size(); ++index) {
    const auto item = pending[index];
    const auto& expression = *item.expression;
    const auto expected =
        expression.kind == ExpressionKind::call ? item.expected : SourceOutputDemand{};
    if (!expression.output_demand.valid() || expression.output_demand != expected ||
        (expected.form == OutputDemandForm::prefix &&
         expected.count != expression.requested_outputs))
      diagnostics.push_back({DiagnosticSeverity::error, "MPF0007",
                             "target source output demand disagrees with its owned call context",
                             expression.location});
    for (const auto& child : expression.children) append(child, value);
  }
}

}  // namespace mpf::detail
