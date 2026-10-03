#include <array>
#include <deque>
#include <string>
#include <utility>

#include "output_assignment.hpp"
#include "output_assignment_internal.hpp"

namespace mpf::detail::mir {
namespace {

void add_error(std::vector<Diagnostic>& diagnostics, const std::string_view stage,
               std::string message) {
  diagnostics.push_back(
      {DiagnosticSeverity::error,
       "MPF0006",
       "invalid MIR output-assignment table at '" + std::string(stage) + "': " + std::move(message),
       {1U, 1U}});
}

// Explore the two possible Boolean states independently for every output. This
// is intentionally not the production lattice/worklist algorithm: at most two
// visits per (block, output) prove the path-sensitive fixed point, including
// backedges and pre-commit exceptional transitions.
void explore_paths(const Program& program, const AliasEffectTable& effects,
                   OutputAssignmentTable& expected) {
  std::vector<std::array<bool, 2U>> visited(program.blocks.size());
  std::vector<BlockId> touched;
  for (std::size_t index = 1U; index < program.functions.size(); ++index) {
    const auto& function = program.functions[index];
    const auto& outputs = expected.functions[index].outputs;
    for (std::size_t output = 0U; output < outputs.size(); ++output) {
      const auto width = outputs.size();
      std::deque<std::pair<BlockId, bool>> pending;
      const auto enqueue = [&](const BlockId block, const bool assigned) {
        auto& states = visited[block.value()];
        const auto ordinal = assigned ? 1U : 0U;
        if (states[ordinal]) return;
        if (!states[0U] && !states[1U]) touched.push_back(block);
        states[ordinal] = true;
        pending.emplace_back(block, assigned);
      };
      enqueue(function.entry, outputs[output].assigned_at_entry);
      while (!pending.empty()) {
        const auto path = pending.front();
        const auto id = path.first;
        bool assigned = path.second;
        pending.pop_front();
        const auto& block = program.blocks[id.value()];
        const auto block_offset = expected.blocks[id.value()].state_offset;
        const auto record = [&](const std::size_t offset) {
          auto& state = expected.states[offset + output];
          state = join_output_assignment(state, assigned ? OutputAssignmentState::assigned
                                                         : OutputAssignmentState::unassigned);
        };
        record(block_offset);
        for (const auto instruction : block.instructions) {
          const auto& row = expected.instructions[instruction.value()];
          record(row.state_offset);
          if (block.exception_handler.valid() &&
              has_effect(effects.instructions[instruction.value()].effects, Effect::may_fail)) {
            record(block_offset + 2U * width);
            enqueue(block.exception_handler, assigned);
          }
          for (std::size_t write = 0U; write < row.write_count; ++write)
            if (expected.writes[row.write_offset + write].output == output) assigned = true;
          record(row.state_offset + width);
        }
        record(block_offset + width);
        for (const auto target : block.terminator.successors) enqueue(target, assigned);
      }
      for (const auto block : touched) visited[block.value()] = {};
      touched.clear();
    }
  }
}

bool same_inventory(const OutputAssignmentTable& actual, const OutputAssignmentTable& expected) {
  if (actual.functions.size() != expected.functions.size() ||
      actual.blocks.size() != expected.blocks.size() ||
      actual.instructions.size() != expected.instructions.size() ||
      actual.states.size() != expected.states.size() || actual.writes != expected.writes)
    return false;
  for (std::size_t index = 0U; index < expected.functions.size(); ++index) {
    const auto& left = actual.functions[index];
    const auto& right = expected.functions[index];
    if (left.origin != right.origin || left.owner != right.owner || left.outputs != right.outputs)
      return false;
  }
  for (std::size_t index = 0U; index < expected.blocks.size(); ++index) {
    const auto& left = actual.blocks[index];
    const auto& right = expected.blocks[index];
    if (left.origin != right.origin || left.function != right.function ||
        left.state_offset != right.state_offset)
      return false;
  }
  for (std::size_t index = 0U; index < expected.instructions.size(); ++index) {
    const auto& left = actual.instructions[index];
    const auto& right = expected.instructions[index];
    if (left.origin != right.origin || left.function != right.function ||
        left.block != right.block || left.state_offset != right.state_offset ||
        left.write_offset != right.write_offset || left.write_count != right.write_count)
      return false;
  }
  return true;
}

}  // namespace

std::vector<Diagnostic> verify_output_assignments(const Program& program,
                                                  const AliasEffectTable& effects,
                                                  const OutputAssignmentTable& analysis,
                                                  const std::string_view stage) {
  std::vector<Diagnostic> diagnostics;
  if (!output_assignments_current(program, effects, analysis)) {
    add_error(diagnostics, stage, "stale, incomplete, or non-dense analysis/dependency");
    return diagnostics;
  }
  if (!verify_alias_effects(program, effects, stage).empty()) {
    add_error(diagnostics, stage,
              "analysis requires an independently verified alias/effect dependency");
    return diagnostics;
  }
  auto expected = output_assignment_detail::prepare(program, effects);
  if (!expected.complete || !same_inventory(analysis, expected)) {
    add_error(diagnostics, stage,
              "binding, type, storage, ownership, row, or write provenance differs from MIR");
    return diagnostics;
  }
  explore_paths(program, effects, expected);
  for (std::size_t index = 0U; index < expected.states.size(); ++index) {
    if (analysis.states[index] != expected.states[index]) {
      add_error(diagnostics, stage,
                "path state at cell " + std::to_string(index) +
                    " differs from independent Boolean-path exploration");
      break;
    }
  }
  return diagnostics;
}

}  // namespace mpf::detail::mir
