#include "mir_copy_propagation.hpp"

#include <unordered_map>
#include <utility>

namespace mpf::detail::mir {
namespace {

struct Edge {
  std::size_t block;
  std::size_t successor;
};

struct Candidate {
  ValueId value;
  std::vector<ValueId> inputs;
  bool compatible{true};
  bool queued{true};
  bool removed{false};
};

using Substitutions = std::unordered_map<ValueId, ValueId>;

ValueId resolve(ValueId value, Substitutions& substitutions) {
  const auto original = value;
  auto found = substitutions.find(value);
  while (found != substitutions.end()) {
    value = found->second;
    found = substitutions.find(value);
  }
  auto cursor = original;
  found = substitutions.find(cursor);
  while (found != substitutions.end()) {
    const auto next = found->second;
    found->second = value;
    cursor = next;
    found = substitutions.find(cursor);
  }
  return value;
}

void rewrite_values(Program& program, Substitutions& substitutions) {
  const auto replace = [&](ValueId& value) { value = resolve(value, substitutions); };
  for (auto& function : program.functions) {
    replace(function.argument_exit.returned);
    for (auto& flow : function.argument_outputs) {
      replace(flow.selected);
      replace(flow.result);
    }
    for (auto& flow : function.argument_entries) {
      replace(flow.selected);
      replace(flow.result);
    }
    for (auto& flow : function.parameter_defaults) replace(flow.result);
  }
  for (auto& instruction : program.instructions)
    for (auto& operand : instruction.operands) replace(operand);
  for (auto& block : program.blocks) {
    for (auto& operand : block.terminator.operands) replace(operand);
    for (auto& edge : block.terminator.successor_arguments)
      for (auto& operand : edge) replace(operand);
  }
}

}  // namespace

std::vector<Diagnostic> propagate_block_arguments(Program& program,
                                                  OptimizationStatistics& statistics) {
  std::vector<std::vector<Edge>> incoming(program.blocks.size());
  for (std::size_t block = 1U; block < program.blocks.size(); ++block) {
    const auto& terminator = program.blocks[block].terminator;
    for (std::size_t edge = 0U; edge < terminator.successors.size(); ++edge) {
      const auto target = terminator.successors[edge];
      if (target.valid() && target.value() < incoming.size())
        incoming[target.value()].push_back({block, edge});
    }
  }
  std::vector<Candidate> candidates;
  std::unordered_map<ValueId, std::vector<std::size_t>> users;
  for (std::size_t block = 1U; block < program.blocks.size(); ++block) {
    const auto& target = program.blocks[block];
    for (std::size_t offset = target.arguments.size(); offset != 0U; --offset) {
      const auto index = offset - 1U;
      const auto& argument = target.arguments[index];
      if (!argument.storage.valid() || !argument.value.valid()) continue;
      Candidate candidate;
      candidate.value = argument.value;
      candidate.inputs.reserve(incoming[block].size());
      const auto id = candidates.size();
      for (const auto& edge : incoming[block]) {
        const auto& terminator = program.blocks[edge.block].terminator;
        if (edge.successor >= terminator.successor_arguments.size() ||
            index >= terminator.successor_arguments[edge.successor].size()) {
          candidate.compatible = false;
          continue;
        }
        const auto actual = terminator.successor_arguments[edge.successor][index];
        candidate.inputs.push_back(actual);
        users[actual].push_back(id);
      }
      candidates.push_back(std::move(candidate));
    }
  }
  Substitutions substitutions;
  substitutions.reserve(candidates.size());
  std::vector<std::size_t> worklist;
  worklist.reserve(candidates.size());
  for (std::size_t id = 0U; id < candidates.size(); ++id) worklist.push_back(id);
  for (std::size_t cursor = 0U; cursor < worklist.size(); ++cursor) {
    auto& candidate = candidates[worklist[cursor]];
    candidate.queued = false;
    if (candidate.removed || !candidate.compatible || candidate.inputs.empty()) continue;
    const auto common = resolve(candidate.inputs.front(), substitutions);
    if (!common.valid() || common == candidate.value) continue;
    bool equal = true;
    for (const auto actual : candidate.inputs)
      if (resolve(actual, substitutions) != common) {
        equal = false;
        break;
      }
    if (!equal) continue;
    candidate.removed = true;
    substitutions.emplace(candidate.value, common);
    ++statistics.propagated_block_arguments;
    // Transfer dependencies to the representative so later substitutions wake transitive users.
    auto dependents = std::move(users[candidate.value]);
    auto& destination = users[common];
    for (const auto user : dependents) {
      auto& dependent = candidates[user];
      if (dependent.removed) continue;
      destination.push_back(user);
      if (!dependent.queued) {
        dependent.queued = true;
        worklist.push_back(user);
      }
    }
  }
  if (substitutions.empty()) return {};
  // Keep original edge positions throughout analysis, then compact each block and edge once.
  for (std::size_t block = 1U; block < program.blocks.size(); ++block) {
    auto& arguments = program.blocks[block].arguments;
    std::vector<bool> removed(arguments.size());
    std::size_t kept = 0U;
    for (std::size_t index = 0U; index < arguments.size(); ++index) {
      removed[index] = substitutions.count(arguments[index].value) != 0U;
      if (!removed[index]) arguments[kept++] = arguments[index];
    }
    arguments.resize(kept);
    for (const auto& edge : incoming[block]) {
      auto& actuals = program.blocks[edge.block].terminator.successor_arguments;
      if (edge.successor >= actuals.size()) continue;
      auto& values = actuals[edge.successor];
      kept = 0U;
      for (std::size_t index = 0U; index < values.size(); ++index)
        if (index >= removed.size() || !removed[index]) values[kept++] = values[index];
      values.resize(kept);
    }
  }
  rewrite_values(program, substitutions);
  return {};
}

}  // namespace mpf::detail::mir
