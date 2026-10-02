#include "analyzer_internal.hpp"

namespace mpf::detail::semantic_internal {

void Analyzer::analyze_matlab_validator_calls(Statement& function,
                                              const ArgumentDirection direction) {
  // These are source-owned invocations, not ordinary value-producing intrinsics. Entry/exit
  // execution is still represented by the standard validation contract until validation-sequence
  // MIR is available. Analyze every operand once; do not rescan the inventory per formal.
  for (auto& invocation : function.argument_validator_calls) {
    if (invocation.declaration >= function.argument_declarations.size() ||
        function.argument_declarations[invocation.declaration].direction != direction) {
      continue;
    }
    auto& expression = invocation.expression;
    for (auto& child : expression.children) analyze_expression(child);
    // Recursive analysis may grow the facts arena. Acquire the parent only after its children.
    auto& facts = semantic(semantics_, expression);
    facts.inferred_type = ValueType::null_value;
    facts.numeric_type = no_numeric_type;
    facts.procedure_has_result = false;
    if (!expression.children.empty()) {
      const auto* use = names_.reference(expression.children.front().id);
      facts.argument_validator = use == nullptr ? std::nullopt : use->argument_validator;
    }
  }
}

}  // namespace mpf::detail::semantic_internal
