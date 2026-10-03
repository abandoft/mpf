#include "control_value_join.hpp"

#include <deque>
#include <string>
#include <unordered_map>
#include <utility>

#include "control_value_join_internal.hpp"

namespace mpf::detail::mir {
namespace {
using namespace control_join_internal;

struct Node {
  BlockId block{};
  std::size_t ordinal{0U};
  ValueId value{};
  std::vector<ValueId> inputs;
};

std::string shape_key(const ShapeData& shape) {
  std::string result = shape.dynamic_rank ? "dynamic" : "fixed";
  result += shape.layout == semantic::IndexLayout::column_major ? ":c" : ":r";
  for (const auto extent : shape.extents) result += ":" + std::to_string(extent);
  result += ":strides";
  for (const auto stride : shape.strides) result += ":" + std::to_string(stride);
  return result;
}
}  // namespace

void normalize_control_value_joins(Program& program) {
  std::vector<std::size_t> incoming(program.blocks.size());
  for (const auto& block : program.blocks)
    for (const auto successor : block.terminator.successors)
      if (successor.valid() && successor.value() < incoming.size()) ++incoming[successor.value()];
  std::vector<Node> nodes;
  std::unordered_map<ValueId, std::size_t> index;
  std::unordered_map<ValueId, Contract> definitions;
  definitions.reserve(program.instructions.size());
  for (const auto& instruction : program.instructions)
    if (instruction.result.valid())
      definitions.emplace(instruction.result, Contract{instruction.type, instruction.shape});
  for (const auto& block : program.blocks)
    for (std::size_t ordinal = 0U; ordinal < block.arguments.size(); ++ordinal) {
      const auto& argument = block.arguments[ordinal];
      definitions.emplace(argument.value, Contract{argument.type, argument.shape});
      if (argument.storage.valid() && block.id.valid() && block.id.value() < incoming.size() &&
          incoming[block.id.value()] != 0U) {
        index.emplace(argument.value, nodes.size());
        nodes.push_back({block.id, ordinal, argument.value, {}});
      }
    }
  if (nodes.empty()) return;
  for (const auto& block : program.blocks)
    for (std::size_t edge = 0U; edge < block.terminator.successors.size(); ++edge) {
      const auto successor = block.terminator.successors[edge];
      if (!successor.valid() || successor.value() >= program.blocks.size() ||
          edge >= block.terminator.successor_arguments.size())
        continue;
      const auto& arguments = program.blocks[successor.value()].arguments;
      const auto& actuals = block.terminator.successor_arguments[edge];
      for (std::size_t ordinal = 0U; ordinal < arguments.size() && ordinal < actuals.size();
           ++ordinal) {
        const auto found = index.find(arguments[ordinal].value);
        if (found != index.end()) nodes[found->second].inputs.push_back(actuals[ordinal]);
      }
    }

  std::vector<std::vector<std::size_t>> users(nodes.size());
  std::vector<Fact> facts(nodes.size());
  for (std::size_t node = 0U; node < nodes.size(); ++node)
    for (const auto input : nodes[node].inputs) {
      const auto dependency = index.find(input);
      if (dependency != index.end()) users[dependency->second].push_back(node);
    }
  std::deque<std::size_t> pending;
  std::vector<bool> queued(nodes.size(), true);
  for (std::size_t node = 0U; node < nodes.size(); ++node) pending.push_back(node);
  while (!pending.empty()) {
    const auto node = pending.front();
    pending.pop_front();
    queued[node] = false;
    Fact next;
    for (const auto input : nodes[node].inputs) {
      const auto dependency = index.find(input);
      if (dependency != index.end())
        next = join(program, std::move(next), facts[dependency->second]);
      else if (const auto source = definitions.find(input); source != definitions.end())
        next = join(program, std::move(next), fact(program, source->second));
    }
    if (same_fact(next, facts[node])) continue;
    facts[node] = std::move(next);
    for (const auto user : users[node])
      if (!queued[user]) {
        queued[user] = true;
        pending.push_back(user);
      }
  }

  TypeId dynamic_type;
  for (std::size_t id = 1U; id < program.types.size(); ++id)
    if (program.types[id].kind == TypeKind::scalar &&
        program.types[id].value_type == ValueType::unknown) {
      dynamic_type = TypeId{static_cast<TypeId::value_type>(id)};
      break;
    }
  std::unordered_map<std::string, ShapeId> shapes;
  shapes.reserve(program.shapes.size() + nodes.size());
  for (std::size_t id = 1U; id < program.shapes.size(); ++id)
    shapes.emplace(shape_key(program.shapes[id]), ShapeId{static_cast<ShapeId::value_type>(id)});
  bool changed = false;
  for (std::size_t node = 0U; node < nodes.size(); ++node) {
    const auto& resolved = facts[node];
    if (!resolved.defined) continue;  // No anchor: unreachable cyclic values remain unconsumed.
    auto& argument = program.blocks[nodes[node].block.value()].arguments[nodes[node].ordinal];
    auto type = resolved.type;
    if (!type.valid()) {
      if (!dynamic_type.valid()) {
        dynamic_type = TypeId{static_cast<TypeId::value_type>(program.types.size())};
        program.types.emplace_back();
      }
      type = dynamic_type;
    }
    const auto key = shape_key(resolved.shape);
    auto found = shapes.find(key);
    ShapeId shape;
    if (found == shapes.end()) {
      shape = ShapeId{static_cast<ShapeId::value_type>(program.shapes.size())};
      program.shapes.push_back(resolved.shape);
      shapes.emplace(key, shape);
    } else {
      shape = found->second;
    }
    changed = changed || argument.type != type || argument.shape != shape;
    argument.type = type;
    argument.shape = shape;
  }
  if (changed) ++program.revision;
}

}  // namespace mpf::detail::mir
