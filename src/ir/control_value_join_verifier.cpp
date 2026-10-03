#include <algorithm>
#include <deque>
#include <unordered_map>
#include <utility>

#include "control_value_join.hpp"
#include "control_value_join_internal.hpp"

namespace mpf::detail::mir {
namespace {
using namespace control_join_internal;
struct Slot {
  const BlockArgument* argument;
  std::vector<ValueId> actuals;
};
}  // namespace

void verify_control_value_joins(const Program& program, std::vector<Diagnostic>& diagnostics,
                                const std::string_view stage) {
  const auto fail = [&](const std::string_view message) {
    diagnostics.push_back(
        {DiagnosticSeverity::error,
         "MPF0006",
         "invalid MIR control-value join at '" + std::string(stage) + "': " + std::string(message),
         {1U, 1U}});
  };
  // Reconstruct original edges/SSA definitions. No normalized producer table is trusted.
  std::unordered_map<ValueId, Contract> definitions;
  for (const auto& instruction : program.instructions)
    if (instruction.result.valid())
      definitions.emplace(instruction.result, Contract{instruction.type, instruction.shape});
  std::vector<std::size_t> predecessors(program.blocks.size());
  for (const auto& block : program.blocks)
    for (const auto successor : block.terminator.successors)
      if (successor.valid() && successor.value() < predecessors.size())
        ++predecessors[successor.value()];
  std::vector<Slot> slots;
  std::unordered_map<ValueId, std::size_t> slots_by_value;
  for (const auto& block : program.blocks)
    for (const auto& argument : block.arguments) {
      definitions.emplace(argument.value, Contract{argument.type, argument.shape});
      if (argument.storage.valid() && block.id.valid() && block.id.value() < predecessors.size() &&
          predecessors[block.id.value()] != 0U) {
        slots_by_value.emplace(argument.value, slots.size());
        slots.push_back({&argument, {}});
      }
    }
  if (slots.empty()) return;
  for (const auto& block : program.blocks)
    for (std::size_t edge = 0U; edge < block.terminator.successors.size(); ++edge) {
      const auto successor = block.terminator.successors[edge];
      if (!successor.valid() || successor.value() >= program.blocks.size() ||
          edge >= block.terminator.successor_arguments.size())
        continue;
      const auto& parameters = program.blocks[successor.value()].arguments;
      const auto& actuals = block.terminator.successor_arguments[edge];
      for (std::size_t parameter = 0U; parameter < parameters.size() && parameter < actuals.size();
           ++parameter) {
        const auto slot = slots_by_value.find(parameters[parameter].value);
        if (slot != slots_by_value.end()) slots[slot->second].actuals.push_back(actuals[parameter]);
      }
    }

  std::vector<std::vector<std::size_t>> dependencies(slots.size()), users(slots.size());
  std::vector<Fact> anchors(slots.size());
  for (std::size_t slot = 0U; slot < slots.size(); ++slot)
    for (const auto actual : slots[slot].actuals) {
      const auto dependency = slots_by_value.find(actual);
      if (dependency != slots_by_value.end()) {
        dependencies[slot].push_back(dependency->second);
        users[dependency->second].push_back(slot);
      } else {
        const auto definition = definitions.find(actual);
        if (definition == definitions.end()) {
          fail("edge actual has no SSA definition");
          return;
        }
        const auto source = fact(program, definition->second);
        if (!source.defined) {
          fail("edge actual has no valid type/shape contract");
          return;
        }
        anchors[slot] = join(program, std::move(anchors[slot]), source);
      }
    }

  // Independent SCC proof: every value in a strongly-connected phi component
  // sees the same union of external anchors. Condense the graph, then evaluate
  // the acyclic component graph once. Iterative DFS avoids host-stack limits.
  std::vector<bool> visited(slots.size());
  std::vector<std::size_t> order;
  order.reserve(slots.size());
  struct Frame {
    std::size_t slot;
    std::size_t child;
  };
  std::vector<Frame> stack;
  for (std::size_t root = 0U; root < slots.size(); ++root) {
    if (visited[root]) continue;
    visited[root] = true;
    stack.push_back({root, 0U});
    while (!stack.empty()) {
      auto& frame = stack.back();
      if (frame.child < dependencies[frame.slot].size()) {
        const auto child = dependencies[frame.slot][frame.child++];
        if (!visited[child]) {
          visited[child] = true;
          stack.push_back({child, 0U});
        }
      } else {
        order.push_back(frame.slot);
        stack.pop_back();
      }
    }
  }
  std::vector<std::size_t> component(slots.size(), dynamic_extent);
  std::size_t component_count = 0U;
  std::vector<std::size_t> pending;
  for (auto item = order.rbegin(); item != order.rend(); ++item) {
    if (component[*item] != dynamic_extent) continue;
    component[*item] = component_count;
    pending.push_back(*item);
    while (!pending.empty()) {
      const auto slot = pending.back();
      pending.pop_back();
      for (const auto user : users[slot])
        if (component[user] == dynamic_extent) {
          component[user] = component_count;
          pending.push_back(user);
        }
    }
    ++component_count;
  }
  std::vector<Fact> resolved(component_count);
  std::vector<std::vector<std::size_t>> downstream(component_count);
  std::vector<std::size_t> remaining(component_count);
  for (std::size_t slot = 0U; slot < slots.size(); ++slot) {
    const auto owner = component[slot];
    resolved[owner] = join(program, std::move(resolved[owner]), anchors[slot]);
    for (const auto dependency : dependencies[slot])
      if (component[dependency] != owner) {
        downstream[component[dependency]].push_back(owner);
        ++remaining[owner];
      }
  }
  std::deque<std::size_t> ready;
  for (std::size_t owner = 0U; owner < component_count; ++owner)
    if (remaining[owner] == 0U) ready.push_back(owner);
  while (!ready.empty()) {
    const auto owner = ready.front();
    ready.pop_front();
    for (const auto next : downstream[owner]) {
      resolved[next] = join(program, std::move(resolved[next]), resolved[owner]);
      if (--remaining[next] == 0U) ready.push_back(next);
    }
  }
  for (std::size_t slot = 0U; slot < slots.size(); ++slot) {
    const auto& expected = resolved[component[slot]];
    if (!expected.defined)
      continue;  // Unanchored unreachable cycle; generic CFG checks still apply.
    const auto actual = fact(program, {slots[slot].argument->type, slots[slot].argument->shape});
    if (!actual.defined ||
        logical_type(program, actual.type) != logical_type(program, expected.type) ||
        !same_shape(actual.shape, expected.shape))
      fail("phi type/shape does not match its actual value-domain join");
  }
}

}  // namespace mpf::detail::mir
