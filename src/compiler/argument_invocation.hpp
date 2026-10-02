#pragma once

#include <limits>

#include "argument_validation.hpp"
#include "mpf/transpiler.hpp"
#include "statement_kind.hpp"

namespace mpf::detail {

// A common ownership contract, not a common AST: lookup adapts arena IDs or HIR expressions.
// Builtin availability, arity and operand ABI are semantic rules, never grammar restrictions.
template <typename Statement, typename Lookup>
[[nodiscard]] bool valid_argument_validator_calls(const Statement& statement,
                                                  const SourceLanguage language, Lookup lookup) {
  if ((!statement.argument_declarations.empty() || !statement.argument_validator_calls.empty()) &&
      (language != SourceLanguage::matlab || statement.kind != StatementKind::function)) {
    return false;
  }
  std::size_t cursor = 0U;
  for (std::size_t declaration_index = 0U;
       declaration_index < statement.argument_declarations.size(); ++declaration_index) {
    const auto& declaration = statement.argument_declarations[declaration_index];
    for (std::size_t validator_index = 0U; validator_index < declaration.validators.size();
         ++validator_index) {
      if (cursor >= statement.argument_validator_calls.size()) return false;
      const auto& invocation = statement.argument_validator_calls[cursor++];
      if (invocation.declaration != declaration_index || invocation.validator != validator_index)
        return false;
      const auto& syntax = declaration.validators[validator_index];
      const auto* call = lookup(invocation.expression);
      if (call == nullptr || call->kind != ExpressionKind::call ||
          syntax.argument_count == std::numeric_limits<std::size_t>::max() ||
          call->children.size() != 1U + syntax.argument_count) {
        return false;
      }
      const auto* callee = lookup(call->children.front());
      if (callee == nullptr || callee->kind != ExpressionKind::identifier ||
          !callee->children.empty() || callee->value != syntax.name)
        return false;
      if (!syntax.explicit_call) {
        const auto* formal = lookup(call->children[1]);
        if (formal == nullptr || formal->kind != ExpressionKind::identifier ||
            !formal->children.empty() || formal->value != declaration.name)
          return false;
      }
    }
  }
  return cursor == statement.argument_validator_calls.size();
}

}  // namespace mpf::detail
