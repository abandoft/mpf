#include "argument_entry_plan.hpp"

namespace mpf::detail::javascript {
std::vector<lir::ArgumentEntryPlan> plan_argument_entries(const lir::Statement& statement) {
  std::vector<lir::ArgumentEntryPlan> result;
  result.reserve(statement.source_argument_entries.size());
  for (std::size_t index = 0U; index < statement.argument_validations.size(); ++index) {
    const auto& declaration = statement.argument_validations[index];
    if (declaration.direction != ArgumentDirection::input ||
        declaration.ordinal >= statement.source_argument_entries.size())
      continue;
    const auto& source = statement.source_argument_entries[declaration.ordinal];
    result.push_back({lir::ArgumentEntryForm::runtime_normalization, index, source,
                      static_cast<std::uint8_t>(source.class_constraint), source.dimensions,
                      source.rank});
  }
  return result;
}
}  // namespace mpf::detail::javascript
