#include "mir_output_demand.hpp"

#include <string>

namespace mpf::detail::mir {

void verify_output_demands(const Program& program, std::vector<Diagnostic>& diagnostics,
                           const std::string_view stage) {
  const auto fail = [&](const SourceLocation location, const char* message) {
    diagnostics.push_back({DiagnosticSeverity::error, "MPF0006",
                           "invalid MIR at '" + std::string(stage) + "': " + message, location});
  };
  std::vector<SourceOutputDemand> expected(program.expressions.size());
  std::vector<MirExpressionId> calls(program.instructions.size());
  for (const auto& expression : program.expressions) {
    if (!expression.id.valid() || expression.id.value() >= expected.size() ||
        expression.kind != ExpressionKind::call)
      continue;
    if (program.source_language == SourceLanguage::matlab)
      expected[expression.id.value()] = {OutputDemandForm::expression, 1U, false};
    if (expression.instruction.valid() && expression.instruction.value() < calls.size())
      calls[expression.instruction.value()] = expression.id;
  }
  if (program.source_language == SourceLanguage::matlab)
    for (const auto& statement : program.statements) {
      const auto* expression = mir::expression(program, statement.expression);
      if (expression == nullptr || expression->kind != ExpressionKind::call ||
          !expression->id.valid() || expression->id.value() >= expected.size())
        continue;
      auto& demand = expected[expression->id.value()];
      if (statement.kind == StatementKind::multi_assignment)
        demand = {OutputDemandForm::prefix, statement.receivers.size(), false};
      else if (statement.kind == StatementKind::expression) {
        const auto* facts = attributes(program, statement.id);
        demand = {
            OutputDemandForm::statement, 0U,
            facts != nullptr && facts->implicit_result != semantic::ImplicitResultPolicy::none};
      }
    }
  for (const auto& expression : program.expressions) {
    if (!expression.id.valid() || expression.id.value() >= expected.size()) continue;
    const auto* facts = attributes(program, expression.id);
    if (facts == nullptr) continue;
    if (!facts->output_demand.valid() || facts->output_demand != expected[expression.id.value()])
      fail(expression.location, "source output demand disagrees with its resident context");
    if (facts->output_demand.form == OutputDemandForm::prefix &&
        facts->output_demand.count != facts->requested_results)
      fail(expression.location, "output demand prefix disagrees with selected result arity");
  }
  for (const auto& call : program.calls) {
    const auto* facts = call.instruction.valid() && call.instruction.value() < calls.size()
                            ? attributes(program, calls[call.instruction.value()])
                            : nullptr;
    if (facts == nullptr || call.output_demand != facts->output_demand ||
        !call.output_demand.valid())
      fail({1U, 1U}, "call-site output demand has no matching resident expression");
  }
}

}  // namespace mpf::detail::mir
