#pragma once

#include "compiler/statement_kind.hpp"
#include "ir/output_receiver_source.hpp"

namespace mpf::detail {

template <typename Statement>
bool valid_output_receiver_sources(const Statement& statement, const SourceLanguage language) {
  if (statement.receivers.size() != statement.target_symbols.size() ||
      statement.receivers.size() != statement.source_receivers.size() ||
      (!statement.receivers.empty() && statement.kind != StatementKind::multi_assignment) ||
      (statement.kind == StatementKind::multi_assignment && statement.receivers.empty()))
    return false;
  for (std::size_t index = 0U; index < statement.receivers.size(); ++index) {
    const auto& receiver = statement.receivers[index];
    const auto& source = statement.source_receivers[index];
    if (!receiver.valid() || source.owner != statement.origin || source.position != index ||
        source.kind != receiver.kind || source.symbol != statement.target_symbols[index] ||
        source.name != receiver.name || source.location.line != receiver.location.line ||
        source.location.column != receiver.location.column || !source.instruction.valid() ||
        !source.argument.valid() || source.argument != statement.expression.source_value ||
        (index != 0U && source.argument != statement.source_receivers.front().argument))
      return false;
    if (receiver.binds()) {
      if (source.opcode != mir::Opcode::store || !source.symbol.valid() ||
          !source.storage.valid() || !source.result.valid())
        return false;
    } else if (language != SourceLanguage::matlab || source.opcode != mir::Opcode::discard_output ||
               source.symbol.valid() || source.storage.valid() || source.result.valid()) {
      return false;
    }
  }
  return true;
}

template <typename Statement>
bool discards_all_outputs(const Statement& statement) {
  if (statement.kind != StatementKind::multi_assignment || statement.has_target_pattern ||
      statement.receivers.empty())
    return false;
  for (const auto& receiver : statement.receivers)
    if (receiver.binds()) return false;
  return true;
}

template <typename Statement>
bool needs_tuple_receiver_temporary(const Statement& statement) {
  return statement.kind == StatementKind::multi_assignment &&
         (statement.has_target_pattern ||
          (statement.receivers.size() > 1U && !discards_all_outputs(statement)));
}

}  // namespace mpf::detail
