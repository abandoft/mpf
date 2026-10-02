#include "argument_output_plan.hpp"

#include <algorithm>
#include <utility>

namespace mpf::detail::javascript {
std::vector<lir::ArgumentOutputPlan> plan_argument_outputs(const lir::Statement& statement) {
  std::vector<lir::ArgumentOutputPlan> result;
  for (std::size_t declaration = 0U; declaration < statement.argument_validations.size();
       ++declaration) {
    const auto& validation = statement.argument_validations[declaration];
    if (validation.direction != ArgumentDirection::output) continue;
    if (result.size() >= statement.source_argument_outputs.size()) return {};
    const auto& source = statement.source_argument_outputs[result.size()];
    lir::ArgumentOutputPlan plan;
    plan.declaration = declaration;
    plan.ordinal = source.flow.output;
    plan.class_opcode = static_cast<std::uint8_t>(source.class_constraint);
    plan.rank = source.rank;
    plan.dimensions = source.dimensions;
    plan.source = source;
    result.push_back(std::move(plan));
  }
  return result;
}

lir::ArgumentExitPlan plan_argument_exit(const lir::Statement& statement) {
  lir::ArgumentExitPlan result;
  if (!statement.source_argument_exit.merge.valid()) return result;
  result.source = statement.source_argument_exit;
  const bool early = std::any_of(result.source.returns.begin(), result.source.returns.end(),
                                 [](const auto& source) { return !source.implicit; });
  result.form = early ? lir::ArgumentExitForm::labeled_scope : lir::ArgumentExitForm::shared_return;
  return result;
}
}  // namespace mpf::detail::javascript
