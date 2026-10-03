#include "output_assignment.hpp"

#include <algorithm>
#include <deque>
#include <limits>
#include <unordered_map>
#include <utility>

#include "output_assignment_internal.hpp"

namespace mpf::detail::mir {
namespace {

template <typename Id, typename Item>
bool valid_index(const Id id, const std::vector<Item>& items) noexcept {
  return id.valid() && static_cast<std::size_t>(id.value()) < items.size();
}

bool defines_binding(const Opcode opcode) noexcept {
  return opcode == Opcode::store || opcode == Opcode::store_indexed ||
         opcode == Opcode::writeback || opcode == Opcode::loop || opcode == Opcode::catch_exception;
}

bool allocate_rows(OutputAssignmentTable& table, const std::size_t width, const std::size_t rows,
                   std::size_t& offset) {
  if (width == 0U) return true;
  if (width > (table.states.max_size() - table.states.size()) / rows) return false;
  offset = table.states.size();
  table.states.resize(offset + width * rows, OutputAssignmentState::unreachable);
  return true;
}

bool merge_row(OutputAssignmentTable& table, const std::size_t offset,
               const std::vector<OutputAssignmentState>& incoming) {
  bool changed = false;
  for (std::size_t output = 0U; output < incoming.size(); ++output) {
    auto& state = table.states[offset + output];
    const auto merged = join_output_assignment(state, incoming[output]);
    changed = changed || state != merged;
    state = merged;
  }
  return changed;
}

void analyze_function(const Program& program, const AliasEffectTable& effects,
                      const Function& function, OutputAssignmentTable& table) {
  const auto& outputs = table.functions[function.id.value()].outputs;
  if (outputs.empty()) return;
  const auto width = outputs.size();
  auto& entry = table.blocks[function.entry.value()];
  for (std::size_t output = 0U; output < width; ++output)
    table.states[entry.state_offset + output] = outputs[output].assigned_at_entry
                                                    ? OutputAssignmentState::assigned
                                                    : OutputAssignmentState::unassigned;

  std::vector<bool> queued(program.blocks.size(), false);
  std::deque<BlockId> pending{function.entry};
  queued[function.entry.value()] = true;
  std::vector<OutputAssignmentState> current(width);
  while (!pending.empty()) {
    const auto id = pending.front();
    pending.pop_front();
    queued[id.value()] = false;
    const auto& block = program.blocks[id.value()];
    const auto offset = table.blocks[id.value()].state_offset;
    std::copy_n(table.states.begin() + static_cast<std::ptrdiff_t>(offset), width, current.begin());
    const auto propagate = [&](const BlockId target) {
      const auto changed = merge_row(table, table.blocks[target.value()].state_offset, current);
      if (changed && !queued[target.value()]) {
        queued[target.value()] = true;
        pending.push_back(target);
      }
    };
    for (const auto instruction_id : block.instructions) {
      const auto& facts = table.instructions[instruction_id.value()];
      std::copy(current.begin(), current.end(),
                table.states.begin() + static_cast<std::ptrdiff_t>(facts.state_offset));
      // A failing instruction has not committed its binding write. Handler entry
      // receives the state at each actual may-fail site, not the block's final state.
      if (block.exception_handler.valid() &&
          has_effect(effects.instructions[instruction_id.value()].effects, Effect::may_fail)) {
        (void)merge_row(table, offset + 2U * width, current);
        propagate(block.exception_handler);
      }
      for (std::size_t index = 0U; index < facts.write_count; ++index)
        current[table.writes[facts.write_offset + index].output] = OutputAssignmentState::assigned;
      std::copy(current.begin(), current.end(),
                table.states.begin() + static_cast<std::ptrdiff_t>(facts.state_offset + width));
    }
    std::copy(current.begin(), current.end(),
              table.states.begin() + static_cast<std::ptrdiff_t>(offset + width));
    for (const auto target : block.terminator.successors) propagate(target);
  }
}

}  // namespace

namespace output_assignment_detail {

OutputAssignmentState state_at(const OutputAssignmentTable& table, const MirFunctionId function,
                               const std::size_t offset, const std::size_t row,
                               const std::size_t output) noexcept {
  if (!valid_index(function, table.functions) || offset == dynamic_extent)
    return OutputAssignmentState::unreachable;
  const auto width = table.functions[function.value()].outputs.size();
  if (output >= width || offset > table.states.size() ||
      width > (table.states.size() - offset) / (row + 1U))
    return OutputAssignmentState::unreachable;
  return table.states[offset + row * width + output];
}

OutputAssignmentTable prepare(const Program& program, const AliasEffectTable& effects) {
  OutputAssignmentTable table;
  table.mir_revision = program.revision;
  table.storage_count = program.storages.size();
  table.instruction_count = program.instructions.size();
  table.block_count = program.blocks.size();
  table.function_count = program.functions.size();
  table.functions.resize(table.function_count);
  table.blocks.resize(table.block_count);
  table.instructions.resize(table.instruction_count);
  if (!alias_effects_current(program, effects)) return table;

  std::unordered_map<HirNodeId, MirStatementId> owners;
  std::unordered_map<SymbolId, StorageId> workspaces;
  if (program.source_language == SourceLanguage::matlab) {
    for (std::size_t index = 1U; index < program.statements.size(); ++index) {
      const auto& statement = program.statements[index];
      if (statement.kind == StatementKind::function &&
          !owners.emplace(statement.origin, statement.id).second)
        return table;
    }
    for (std::size_t index = 1U; index < program.storages.size(); ++index) {
      const auto& storage = program.storages[index];
      if (storage.symbol.valid() && storage.kind != StorageKind::view)
        workspaces[storage.symbol] = StorageId{static_cast<StorageId::value_type>(index)};
    }
  }
  for (std::size_t index = 1U; index < program.functions.size(); ++index) {
    const auto& function = program.functions[index];
    if (function.id.value() != index || !valid_index(function.entry, program.blocks)) return table;
    auto& facts = table.functions[index];
    facts.origin = function.id;
    const auto found = owners.find(function.origin);
    if (found != owners.end()) {
      facts.owner = found->second;
      if (!valid_index(facts.owner, program.statements)) return table;
      const auto& owner = program.statements[facts.owner.value()];
      if (owner.return_symbols.size() != owner.return_names.size() ||
          function.result_types.size() != owner.return_symbols.size() ||
          function.result_shapes.size() != owner.return_symbols.size())
        return table;
      facts.outputs.reserve(owner.return_symbols.size());
      for (std::size_t output = 0U; output < owner.return_symbols.size(); ++output) {
        OutputAssignmentSlot slot;
        slot.ordinal = output;
        slot.symbol = owner.return_symbols[output];
        slot.result_type = function.result_types[output];
        slot.result_shape = function.result_shapes[output];
        if (!slot.symbol.valid() || !valid_index(slot.result_type, program.types) ||
            !valid_index(slot.result_shape, program.shapes))
          return table;
        const auto workspace = workspaces.find(slot.symbol);
        if (workspace != workspaces.end()) {
          slot.workspace = workspace->second;
          const auto& storage = program.storages[slot.workspace.value()];
          slot.workspace_type = storage.type;
          slot.workspace_shape = storage.shape;
          if (!valid_index(slot.workspace_type, program.types) ||
              !valid_index(slot.workspace_shape, program.shapes))
            return table;
        }
        const auto& arguments = program.blocks[function.entry.value()].arguments;
        for (std::size_t parameter = 0U; parameter < owner.parameter_symbols.size(); ++parameter) {
          if (owner.parameter_symbols[parameter] == slot.symbol && parameter < arguments.size())
            slot.assigned_at_entry =
                slot.workspace.valid() && arguments[parameter].storage == slot.workspace;
        }
        facts.outputs.push_back(slot);
      }
    }
    const auto width = facts.outputs.size();
    bool has_entry = false;
    std::unordered_map<StorageId, std::size_t> output_storage;
    for (const auto& slot : facts.outputs)
      if (slot.workspace.valid() && !output_storage.emplace(slot.workspace, slot.ordinal).second)
        return table;
    for (const auto block_id : function.blocks) {
      if (!valid_index(block_id, program.blocks)) return table;
      auto& row = table.blocks[block_id.value()];
      if (row.origin.valid() || program.blocks[block_id.value()].id != block_id) return table;
      has_entry = has_entry || block_id == function.entry;
      row.origin = block_id;
      row.function = function.id;
      if (!allocate_rows(table, width, 3U, row.state_offset)) return table;
      for (const auto instruction_id : program.blocks[block_id.value()].instructions) {
        if (!valid_index(instruction_id, program.instructions) ||
            program.instructions[instruction_id.value()].id != instruction_id ||
            table.instructions[instruction_id.value()].origin.valid())
          return table;
        auto& instruction_row = table.instructions[instruction_id.value()];
        instruction_row.origin = instruction_id;
        instruction_row.block = block_id;
        instruction_row.function = function.id;
        instruction_row.write_offset = table.writes.size();
        if (!allocate_rows(table, width, 2U, instruction_row.state_offset)) return table;
        const auto& instruction = program.instructions[instruction_id.value()];
        if (width == 0U || !defines_binding(instruction.opcode)) continue;
        const auto* metadata = mir::attributes(program, instruction_id);
        const auto* storage = effects.storage(instruction.storage);
        if (metadata == nullptr || storage == nullptr) continue;
        const auto output = output_storage.find(storage->root);
        if (output == output_storage.end()) continue;
        for (std::size_t access = 0U; access < metadata->memory_accesses.size(); ++access) {
          const auto& memory = metadata->memory_accesses[access];
          if (memory.root == storage->root && memory_access_writes(memory.mode)) {
            table.writes.push_back({instruction_id, output->second, access});
            ++instruction_row.write_count;
            break;
          }
        }
      }
    }
    if (!has_entry) return table;
  }
  for (std::size_t index = 1U; index < program.blocks.size(); ++index) {
    const auto& block = program.blocks[index];
    const auto function = table.blocks[index].function;
    if (!function.valid()) return table;
    const auto same_function = [&](const BlockId target) {
      return valid_index(target, table.blocks) && table.blocks[target.value()].function == function;
    };
    if (block.exception_handler.valid() && !same_function(block.exception_handler)) return table;
    for (const auto target : block.terminator.successors)
      if (!same_function(target)) return table;
  }
  for (std::size_t index = 1U; index < program.instructions.size(); ++index)
    if (!table.instructions[index].origin.valid()) return table;
  table.complete = true;
  return table;
}

}  // namespace output_assignment_detail

OutputAssignmentState OutputAssignmentTable::block_entry(const BlockId id,
                                                         const std::size_t output) const noexcept {
  if (!valid_index(id, blocks)) return OutputAssignmentState::unreachable;
  const auto& row = blocks[id.value()];
  return output_assignment_detail::state_at(*this, row.function, row.state_offset, 0U, output);
}

OutputAssignmentState OutputAssignmentTable::block_exit(const BlockId id,
                                                        const std::size_t output) const noexcept {
  if (!valid_index(id, blocks)) return OutputAssignmentState::unreachable;
  const auto& row = blocks[id.value()];
  return output_assignment_detail::state_at(*this, row.function, row.state_offset, 1U, output);
}

OutputAssignmentState OutputAssignmentTable::block_exception(
    const BlockId id, const std::size_t output) const noexcept {
  if (!valid_index(id, blocks)) return OutputAssignmentState::unreachable;
  const auto& row = blocks[id.value()];
  return output_assignment_detail::state_at(*this, row.function, row.state_offset, 2U, output);
}

OutputAssignmentState OutputAssignmentTable::before(const InstructionId id,
                                                    const std::size_t output) const noexcept {
  if (!valid_index(id, instructions)) return OutputAssignmentState::unreachable;
  const auto& row = instructions[id.value()];
  return output_assignment_detail::state_at(*this, row.function, row.state_offset, 0U, output);
}

OutputAssignmentState OutputAssignmentTable::after(const InstructionId id,
                                                   const std::size_t output) const noexcept {
  if (!valid_index(id, instructions)) return OutputAssignmentState::unreachable;
  const auto& row = instructions[id.value()];
  return output_assignment_detail::state_at(*this, row.function, row.state_offset, 1U, output);
}

OutputAssignmentTable analyze_output_assignments(const Program& program,
                                                 const AliasEffectTable& effects) {
  auto table = output_assignment_detail::prepare(program, effects);
  if (!table.complete) return table;
  for (std::size_t index = 1U; index < program.functions.size(); ++index)
    analyze_function(program, effects, program.functions[index], table);
  return table;
}

bool output_assignments_current(const Program& program, const AliasEffectTable& effects,
                                const OutputAssignmentTable& table) noexcept {
  return table.complete && alias_effects_current(program, effects) &&
         table.mir_revision == program.revision && table.storage_count == program.storages.size() &&
         table.instruction_count == program.instructions.size() &&
         table.block_count == program.blocks.size() &&
         table.function_count == program.functions.size() &&
         table.functions.size() == table.function_count &&
         table.blocks.size() == table.block_count &&
         table.instructions.size() == table.instruction_count;
}

}  // namespace mpf::detail::mir
