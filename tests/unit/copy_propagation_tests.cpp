#include <algorithm>
#include <random>
#include <vector>

#include "ir/mir_copy_propagation.hpp"
#include "test_framework.hpp"

namespace {
using namespace mpf::detail;

// Local pass fixtures exercise edge/value equations, not complete compiler artifacts.
mir::Program fixture() {
  mir::Program program;
  program.blocks.resize(1U);
  program.instructions.resize(2U);
  program.functions.resize(2U);
  return program;
}

BlockId block(mir::Program& program) {
  const auto id = BlockId{static_cast<BlockId::value_type>(program.blocks.size())};
  mir::BasicBlock item;
  item.id = id;
  program.blocks.push_back(std::move(item));
  return id;
}

ValueId argument(mir::Program& program, const BlockId owner, const std::uint32_t value,
                 const bool storage = true) {
  const auto id = ValueId{value};
  program.blocks[owner.value()].arguments.push_back(
      {id, {}, {}, storage ? StorageId{1U} : StorageId{}});
  return id;
}

void edge(mir::Program& program, const BlockId source, const BlockId target,
          std::vector<ValueId> actuals) {
  auto& terminator = program.blocks[source.value()].terminator;
  terminator.successors.push_back(target);
  terminator.successor_arguments.push_back(std::move(actuals));
}

std::size_t propagate(mir::Program& program) {
  mir::OptimizationStatistics statistics;
  REQUIRE(mir::propagate_block_arguments(program, statistics).empty());
  return statistics.propagated_block_arguments;
}
}  // namespace

TEST_CASE("copy worklist compacts each edge while retaining distinct and semantic merges") {
  auto program = fixture();
  const auto join = block(program);
  const auto first = argument(program, join, 100U);
  const auto removed = argument(program, join, 101U);
  const auto semantic = argument(program, join, 102U, false);
  const auto left = block(program);
  const auto right = block(program);
  edge(program, left, join, {ValueId{1U}, ValueId{3U}, ValueId{4U}});
  edge(program, right, join, {ValueId{2U}, ValueId{3U}, ValueId{4U}});
  program.instructions[1].operands = {first, removed, semantic};
  REQUIRE(propagate(program) == 1U);
  REQUIRE(program.blocks[join.value()].arguments.size() == 2U);
  REQUIRE(program.blocks[join.value()].arguments[1].value == semantic);
  REQUIRE((program.blocks[left.value()].terminator.successor_arguments[0] ==
           std::vector<ValueId>{ValueId{1U}, ValueId{4U}}));
  REQUIRE((program.blocks[right.value()].terminator.successor_arguments[0] ==
           std::vector<ValueId>{ValueId{2U}, ValueId{4U}}));
  REQUIRE((program.instructions[1].operands == std::vector<ValueId>{first, ValueId{3U}, semantic}));
}

TEST_CASE("copy worklist propagates ten thousand forwarding arguments without global rescans") {
  auto program = fixture();
  auto previous = block(program);
  ValueId value{1U};
  for (std::uint32_t index = 0U; index < 10000U; ++index) {
    const auto target = block(program);
    const auto next = argument(program, target, 100U + index);
    edge(program, previous, target, {value});
    value = next;
    previous = target;
  }
  program.instructions[1].operands = {value};
  program.blocks[previous.value()].terminator.operands = {value};
  REQUIRE(propagate(program) == 10000U);
  REQUIRE(program.instructions[1].operands.front() == ValueId{1U});
  REQUIRE(program.blocks[previous.value()].terminator.operands.front() == ValueId{1U});
  REQUIRE(propagate(program) == 0U);
}

TEST_CASE("copy worklist wakes reverse ordered transitive merge users") {
  auto program = fixture();
  std::vector<BlockId> owners;
  std::vector<ValueId> values;
  for (std::uint32_t index = 0U; index < 8U; ++index) {
    owners.push_back(block(program));
    values.push_back(argument(program, owners.back(), 100U + index));
  }
  for (std::size_t index = 0U; index < owners.size(); ++index) {
    edge(program, block(program), owners[index],
         {index + 1U < values.size() ? values[index + 1U] : ValueId{1U}});
    edge(program, block(program), owners[index], {ValueId{1U}});
  }
  program.instructions[1].operands = values;
  REQUIRE(propagate(program) == values.size());
  for (const auto value : program.instructions[1].operands) REQUIRE(value == ValueId{1U});
}

TEST_CASE("copy worklist updates entry default and shared output provenance in one batch") {
  auto program = fixture();
  const auto first = block(program);
  const auto target = block(program);
  const auto value = argument(program, target, 100U);
  edge(program, first, target, {ValueId{1U}});
  auto& function = program.functions[1];
  function.argument_exit.returned = value;
  function.argument_outputs.resize(1U);
  function.argument_outputs[0].selected = value;
  function.argument_outputs[0].result = value;
  function.argument_entries.resize(1U);
  function.argument_entries[0].selected = value;
  function.argument_entries[0].result = value;
  function.parameter_defaults.resize(1U);
  function.parameter_defaults[0].result = value;
  REQUIRE(propagate(program) == 1U);
  REQUIRE(function.argument_exit.returned == ValueId{1U});
  REQUIRE(function.argument_outputs[0].selected == ValueId{1U});
  REQUIRE(function.argument_outputs[0].result == ValueId{1U});
  REQUIRE(function.argument_entries[0].selected == ValueId{1U});
  REQUIRE(function.argument_entries[0].result == ValueId{1U});
  REQUIRE(function.parameter_defaults[0].result == ValueId{1U});
}

TEST_CASE("copy worklist preserves anchored self merges rather than dropping loop state") {
  auto program = fixture();
  const auto target = block(program);
  const auto value = argument(program, target, 100U);
  edge(program, block(program), target, {ValueId{1U}});
  edge(program, target, target, {value});
  REQUIRE(propagate(program) == 0U);
  REQUIRE(program.blocks[target.value()].arguments.front().value == value);
}

TEST_CASE("copy worklist terminates on cycles and retains an unresolved representative") {
  auto program = fixture();
  const auto left = block(program);
  const auto right = block(program);
  const auto first = argument(program, left, 100U);
  const auto second = argument(program, right, 101U);
  edge(program, left, right, {first});
  edge(program, right, left, {second});
  program.instructions[1].operands = {first, second};
  REQUIRE(propagate(program) == 1U);
  REQUIRE(program.instructions[1].operands[0] == program.instructions[1].operands[1]);
  REQUIRE(program.blocks[left.value()].arguments.size() +
              program.blocks[right.value()].arguments.size() ==
          1U);
  REQUIRE(propagate(program) == 0U);
}

TEST_CASE("copy worklist leaves missing invalid and unconnected incoming values intact") {
  auto program = fixture();
  const auto missing = block(program);
  argument(program, missing, 100U);
  edge(program, block(program), missing, {});
  const auto invalid = block(program);
  argument(program, invalid, 101U);
  edge(program, block(program), invalid, {ValueId{}});
  const auto unconnected = block(program);
  argument(program, unconnected, 102U);
  REQUIRE(propagate(program) == 0U);
  REQUIRE(program.blocks[missing.value()].arguments.size() == 1U);
  REQUIRE(program.blocks[invalid.value()].arguments.size() == 1U);
  REQUIRE(program.blocks[unconnected.value()].arguments.size() == 1U);
}

TEST_CASE("copy worklist agrees with independent acyclic value equation oracles") {
  std::mt19937 random(0x4d5046U);
  for (std::size_t graph = 0U; graph < 128U; ++graph) {
    auto program = fixture();
    std::vector<ValueId> originals{ValueId{1U}, ValueId{2U}};
    auto expected = originals;
    std::size_t removable = 0U;
    for (std::uint32_t vertex = 0U; vertex < 32U; ++vertex) {
      const auto owner = block(program);
      const auto value = argument(program, owner, 100U + vertex);
      const auto left = static_cast<std::size_t>(random()) % originals.size();
      const auto right = static_cast<std::size_t>(random()) % originals.size();
      edge(program, block(program), owner, {originals[left]});
      edge(program, block(program), owner, {originals[right]});
      originals.push_back(value);
      const auto resolved = expected[left] == expected[right] ? expected[left] : value;
      expected.push_back(resolved);
      if (resolved != value) ++removable;
    }
    program.instructions[1].operands = originals;
    REQUIRE(propagate(program) == removable);
    REQUIRE(program.instructions[1].operands == expected);
    REQUIRE(propagate(program) == 0U);
    REQUIRE(program.instructions[1].operands == expected);
  }
}
