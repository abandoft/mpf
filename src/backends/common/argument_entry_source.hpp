#pragma once

#include "compiler/statement_kind.hpp"
#include "ir/argument_entry_flow.hpp"

namespace mpf::detail {

template <typename Statement>
bool valid_argument_entry_sources(const Statement& statement,
                                  const SourceLanguage language) noexcept {
  if (language != SourceLanguage::matlab || statement.kind != StatementKind::function)
    return statement.source_argument_entries.empty();
  std::size_t ordinal = 0U;
  for (const auto& declaration : statement.argument_validations) {
    if (declaration.direction != ArgumentDirection::input) continue;
    if (ordinal >= statement.source_argument_entries.size()) return false;
    const auto& source = statement.source_argument_entries[ordinal];
    const auto& flow = source.flow;
    if (declaration.ordinal != ordinal || flow.parameter != ordinal || !flow.raw_storage.valid() ||
        !flow.storage.valid() || flow.raw_storage == flow.storage || !flow.normalization.valid() ||
        !flow.initialization.valid() || flow.normalization == flow.initialization ||
        !flow.selected.valid() || !flow.result.valid() || flow.selected == flow.result ||
        !flow.block.valid() || !flow.continuation.valid() || flow.block == flow.continuation ||
        source.class_constraint != declaration.class_constraint ||
        source.dimensions_declared != declaration.dimensions_declared ||
        source.dimensions != declaration.dimensions || source.rank != declaration.validated_rank ||
        source.validators != declaration.validators ||
        flow.validators.size() != source.validators.size())
      return false;
    InstructionId previous = flow.initialization;
    if (!(flow.normalization < previous)) return false;
    for (const auto instruction : flow.validators) {
      if (!instruction.valid() || !(previous < instruction)) return false;
      previous = instruction;
    }
    ++ordinal;
  }
  return ordinal == statement.source_argument_entries.size() &&
         (ordinal == 0U || ordinal == statement.parameters.size());
}

}  // namespace mpf::detail
