#include "mir_output_receivers.hpp"

#include <string>

namespace mpf::detail::mir {

void verify_output_receivers(const Program& program, std::vector<Diagnostic>& diagnostics,
                             const std::string_view stage) {
  const auto fail = [&](const SourceLocation location, const char* message) {
    diagnostics.push_back({DiagnosticSeverity::error, "MPF0006",
                           "invalid MIR at '" + std::string(stage) + "': " + message, location});
  };
  std::vector<bool> owned(program.instructions.size());
  for (const auto& statement : program.statements) {
    if (!statement.id.valid()) continue;
    if (statement.kind != StatementKind::multi_assignment) {
      if (!statement.receivers.empty())
        fail({statement.line, 1U}, "output receivers belong only to multi-assignment");
      continue;
    }
    const auto* facts = attributes(program, statement.id);
    const auto* value = expression(program, statement.expression);
    if (statement.receivers.empty() ||
        statement.target_symbols.size() != statement.receivers.size() || facts == nullptr ||
        facts->targets.size() != statement.receivers.size() || !statement.instruction.valid() ||
        value == nullptr || !value->value_id.valid()) {
      fail({statement.line, 1U}, "output receiver inventory is incomplete");
      continue;
    }
    for (std::size_t index = 0U; index < statement.receivers.size(); ++index) {
      const auto instruction_index =
          static_cast<std::size_t>(statement.instruction.value()) + index;
      const auto& receiver = statement.receivers[index];
      if (instruction_index >= program.instructions.size()) {
        fail(receiver.location, "output receiver has no resident operation");
        continue;
      }
      const auto& operation = program.instructions[instruction_index];
      const auto& target = facts->targets[index];
      const auto* operation_facts = attributes(program, operation.id);
      const auto opcode = receiver.binds() ? Opcode::store : Opcode::discard_output;
      if (owned[instruction_index])
        fail(receiver.location, "output receiver operation is owned more than once");
      owned[instruction_index] = true;
      if (!receiver.valid() ||
          (!receiver.binds() && program.source_language != SourceLanguage::matlab) ||
          operation.origin != statement.origin || operation.opcode != opcode ||
          operation.result_index != index || operation.storage != target.storage ||
          operation.type != target.type || operation.shape != target.shape ||
          operation.operands.size() != 1U || operation.operands.front() != value->value_id ||
          operation.location.line != receiver.location.line ||
          operation.location.column != receiver.location.column ||
          (receiver.binds() != statement.target_symbols[index].valid()) ||
          (receiver.binds() != operation.storage.valid()) ||
          (receiver.binds() != operation.result.valid()))
        fail(receiver.location,
             "output receiver disagrees with its indexed store/discard operation");
      if (receiver.binds()) {
        if (!operation.storage.valid() || operation.storage.value() >= program.storages.size() ||
            program.storages[operation.storage.value()].symbol != statement.target_symbols[index])
          fail(receiver.location, "bound output receiver disagrees with its source storage symbol");
      } else if (operation_facts == nullptr || !operation_facts->memory_accesses.empty() ||
                 value_type(program, target.previous_type) != ValueType::unknown ||
                 operation.callee.valid() || operation.intrinsic != IntrinsicId::none ||
                 operation.transfer != ArgumentTransfer::value) {
        fail(receiver.location, "discarded output carries invented memory binding or call state");
      }
    }
  }
  for (std::size_t index = 1U; index < program.instructions.size(); ++index)
    if (program.instructions[index].opcode == Opcode::discard_output && !owned[index])
      fail(program.instructions[index].location, "orphan discard-output operation");
}

}  // namespace mpf::detail::mir
