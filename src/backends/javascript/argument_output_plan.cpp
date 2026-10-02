#include "argument_output_plan.hpp"

namespace mpf::detail::javascript {
std::vector<lir::ArgumentOutputPlan> plan_argument_outputs(const lir::Statement& statement) {
  std::vector<lir::ArgumentOutputPlan> result;
  for (std::size_t declaration = 0U; declaration < statement.argument_validations.size();
       ++declaration) {
    const auto& validation = statement.argument_validations[declaration];
    if (validation.direction != ArgumentDirection::output) continue;
    result.push_back({declaration, validation.ordinal,
                      static_cast<std::uint8_t>(validation.class_constraint),
                      validation.validated_rank, validation.dimensions});
  }
  return result;
}
}  // namespace mpf::detail::javascript
