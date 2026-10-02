#pragma once

#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "compiler/intrinsic.hpp"
#include "ir/invocation_context.hpp"
#include "mpf/transpiler.hpp"

namespace mpf::detail {

// Reconstruct invocation ownership from target-owned functions and expression contexts.
// This checks source provenance, not either target's private ABI or representation choices.
template <typename Program>
void verify_invocation_sources(const Program& program, std::vector<Diagnostic>& diagnostics) {
  using SourceStatement = typename std::decay_t<decltype(program.statements)>::value_type;
  using SourceExpression = std::decay_t<decltype(std::declval<SourceStatement>().expression)>;
  struct StatementWork {
    const SourceStatement* statement;
    const SourceStatement* function;
  };
  struct ExpressionWork {
    const SourceExpression* expression;
    const SourceStatement* function;
  };
  std::vector<StatementWork> statements;
  std::vector<ExpressionWork> expressions;
  std::unordered_map<SymbolId, const SourceStatement*> functions;
  std::unordered_set<ValueId> frame_values;
  const auto fail = [&](const SourceLocation location, const char* message) {
    diagnostics.push_back({DiagnosticSeverity::error, "MPF0007", message, location});
  };
  const bool matlab = program.source_language == SourceLanguage::matlab;
  statements.reserve(program.statements.size());
  for (const auto& statement : program.statements) statements.push_back({&statement, nullptr});
  const auto append = [&](const SourceExpression& expression, const SourceStatement* function) {
    if (expression.valid()) expressions.push_back({&expression, function});
  };
  for (std::size_t index = 0U; index < statements.size(); ++index) {
    const auto work = statements[index];
    const auto& statement = *work.statement;
    const bool function = statement.kind == StatementKind::function;
    const auto* owner = function ? &statement : work.function;
    const auto& frame = statement.source_invocation_frame;
    if (function && matlab) {
      if (!frame.active() || !frame.type.valid() || !frame.shape.valid() ||
          !frame_values.insert(frame.output_count).second)
        fail({statement.line, 1U}, "target function has no unique typed invocation formal");
      if (!functions.emplace(statement.symbol_id, &statement).second)
        fail({statement.line, 1U}, "target invocation function binding is duplicated");
    } else if (frame != mir::InvocationFrame{}) {
      fail({statement.line, 1U}, "target non-Matlab-function retains invocation provenance");
    }
    append(statement.expression, owner);
    append(statement.secondary_expression, owner);
    append(statement.tertiary_expression, owner);
    append(statement.target_expression, owner);
    for (const auto& expression : statement.parameter_defaults) append(expression, owner);
    for (const auto& selector : statement.case_selectors) {
      append(selector.lower, owner);
      append(selector.upper, owner);
    }
    for (const auto& child : statement.body) statements.push_back({&child, owner});
    for (const auto& child : statement.alternative) statements.push_back({&child, owner});
  }
  for (std::size_t index = 0U; index < expressions.size(); ++index) {
    const auto work = expressions[index];
    const auto& expression = *work.expression;
    const auto* callee = expression.kind == ExpressionKind::call && !expression.children.empty()
                             ? &expression.children.front()
                             : nullptr;
    const bool query = (expression.kind == ExpressionKind::identifier &&
                        expression.binding == BindingKind::builtin &&
                        expression.intrinsic == IntrinsicId::matlab_nargout &&
                        expression.inferred_type == ValueType::real) ||
                       (callee != nullptr && callee->binding == BindingKind::builtin &&
                        callee->intrinsic == IntrinsicId::matlab_nargout);
    if (query) {
      if (!matlab || work.function == nullptr || !expression.source_invocation_query.valid() ||
          expression.source_invocation_query !=
              work.function->source_invocation_frame.output_count ||
          expression.inferred_type != ValueType::real ||
          expression.numeric_type != real_numeric_type || !expression.shape.empty() ||
          (callee != nullptr && expression.children.size() != 1U))
        fail(expression.location, "target nargout query does not belong to its invocation frame");
    } else if (expression.source_invocation_query.valid()) {
      fail(expression.location, "target non-query retains an invocation count read");
    }
    mir::InvocationDemand expected;
    if (matlab && callee != nullptr && callee->binding == BindingKind::function) {
      const auto function = functions.find(callee->symbol_id);
      if (function == functions.end()) {
        fail(expression.location, "target call has no invocation callee binding");
      } else {
        const auto& frame = function->second->source_invocation_frame;
        expected = {frame.type, frame.shape, expression.output_demand.count};
        if (expression.children.size() != function->second->parameters.size() + 1U)
          fail(expression.location, "target invocation count would shift an omitted source formal");
      }
    }
    if (expression.source_invocation != expected)
      fail(expression.location,
           "target invocation actual disagrees with its source demand and callee");
    for (const auto& child : expression.children) append(child, work.function);
  }
}

}  // namespace mpf::detail
