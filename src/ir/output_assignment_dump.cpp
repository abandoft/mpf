#include <sstream>

#include "output_assignment.hpp"

namespace mpf::detail::mir {
namespace {

std::string_view name(const OutputAssignmentState state) noexcept {
  switch (state) {
    case OutputAssignmentState::unreachable: return "unreachable";
    case OutputAssignmentState::unassigned: return "unassigned";
    case OutputAssignmentState::assigned: return "assigned";
    case OutputAssignmentState::path_dependent: return "path-dependent";
    case OutputAssignmentState::invalid: return "invalid";
  }
  return "invalid";
}

template <typename Query>
void states(std::ostream& output, const std::size_t width, Query query) {
  output << '[';
  for (std::size_t index = 0U; index < width; ++index) {
    if (index != 0U) output << ',';
    output << name(query(index));
  }
  output << ']';
}

}  // namespace

std::string dump_output_assignments(const OutputAssignmentTable& table) {
  std::ostringstream output;
  output << "output-assignment-v1 revision=" << table.mir_revision << " complete=" << table.complete
         << " states=" << table.states.size() << " writes=" << table.writes.size() << '\n';
  for (std::size_t index = 1U; index < table.functions.size(); ++index) {
    const auto& function = table.functions[index];
    if (function.outputs.empty()) continue;
    output << "function#" << function.origin.value() << " owner#" << function.owner.value() << '\n';
    for (const auto& slot : function.outputs)
      output << " output[" << slot.ordinal << "] symbol#" << slot.symbol.value() << " workspace#"
             << slot.workspace.value() << " workspace-type#" << slot.workspace_type.value()
             << " workspace-shape#" << slot.workspace_shape.value() << " result-type#"
             << slot.result_type.value() << " result-shape#" << slot.result_shape.value()
             << " entry=" << (slot.assigned_at_entry ? "assigned" : "unassigned") << '\n';
    for (const auto& block : table.blocks) {
      if (block.function != function.origin) continue;
      output << " block#" << block.origin.value() << " entry=";
      states(output, function.outputs.size(),
             [&](const std::size_t slot) { return table.block_entry(block.origin, slot); });
      output << " normal=";
      states(output, function.outputs.size(),
             [&](const std::size_t slot) { return table.block_exit(block.origin, slot); });
      output << " exceptional=";
      states(output, function.outputs.size(),
             [&](const std::size_t slot) { return table.block_exception(block.origin, slot); });
      output << '\n';
    }
    for (const auto& instruction : table.instructions) {
      if (instruction.function != function.origin) continue;
      output << " instruction#" << instruction.origin.value() << " block#"
             << instruction.block.value() << " before=";
      states(output, function.outputs.size(),
             [&](const std::size_t slot) { return table.before(instruction.origin, slot); });
      output << " after=";
      states(output, function.outputs.size(),
             [&](const std::size_t slot) { return table.after(instruction.origin, slot); });
      output << " writes=[";
      for (std::size_t index_in_row = 0U; index_in_row < instruction.write_count; ++index_in_row) {
        if (index_in_row != 0U) output << ',';
        if (instruction.write_offset >= table.writes.size() ||
            index_in_row >= table.writes.size() - instruction.write_offset) {
          output << "invalid";
          break;
        }
        const auto& write = table.writes[instruction.write_offset + index_in_row];
        output << write.output << "@access" << write.memory_access;
      }
      output << "]\n";
    }
  }
  return output.str();
}

}  // namespace mpf::detail::mir
