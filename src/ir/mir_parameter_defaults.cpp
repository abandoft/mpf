#include "mir_parameter_defaults.hpp"

#include <algorithm>
#include <unordered_map>

namespace mpf::detail::mir {
namespace {

template <typename Id, typename Items>
bool valid(const Id id, const Items& items) noexcept {
  return id.valid() && id.value() < items.size();
}

void fail(std::vector<Diagnostic>& diagnostics, const SourceLocation location,
          const std::string_view stage, const std::string_view message) {
  diagnostics.push_back({DiagnosticSeverity::error, "MPF0006",
                         "invalid MIR parameter-default flow at '" + std::string(stage) +
                             "': " + std::string(message),
                         location});
}

bool branches_to(const BasicBlock& block, const BlockId successor) noexcept {
  return block.terminator.kind == TerminatorKind::branch &&
         block.terminator.successors.size() == 1U &&
         block.terminator.successors.front() == successor;
}

}  // namespace

void verify_parameter_defaults(const Program& program,
                               const std::vector<InstructionId>& presence_instructions,
                               std::vector<Diagnostic>& diagnostics, const std::string_view stage) {
  bool active = !presence_instructions.empty();
  for (std::size_t index = 1U; index < program.functions.size(); ++index)
    active = active || !program.functions[index].parameter_defaults.empty();
  if (program.source_language == SourceLanguage::matlab) {
    for (std::size_t index = 1U; index < program.statements.size(); ++index) {
      const auto& statement = program.statements[index];
      active = active || (statement.kind == StatementKind::function &&
                          std::any_of(statement.parameter_defaults.begin(),
                                      statement.parameter_defaults.end(),
                                      [](const MirExpressionId value) { return value.valid(); }));
    }
  }
  if (!active) return;
  std::vector<BlockId> instruction_blocks(program.instructions.size());
  std::vector<MirFunctionId> block_functions(program.blocks.size());
  for (std::size_t index = 1U; index < program.functions.size(); ++index)
    for (const auto block : program.functions[index].blocks)
      if (valid(block, block_functions))
        block_functions[block.value()] =
            MirFunctionId{static_cast<MirFunctionId::value_type>(index)};
  std::vector<std::vector<BlockId>> incoming(program.blocks.size());
  for (std::size_t index = 1U; index < program.blocks.size(); ++index) {
    const auto& block = program.blocks[index];
    const BlockId identity{static_cast<BlockId::value_type>(index)};
    for (const auto instruction : block.instructions)
      if (valid(instruction, instruction_blocks))
        instruction_blocks[instruction.value()] = identity;
    for (const auto successor : block.terminator.successors)
      if (valid(successor, incoming)) incoming[successor.value()].push_back(identity);
  }
  std::unordered_map<HirNodeId::value_type, const Statement*> owners;
  for (std::size_t index = 1U; index < program.statements.size(); ++index) {
    const auto& statement = program.statements[index];
    if (statement.kind == StatementKind::function)
      owners.emplace(statement.origin.value(), &statement);
  }
  std::vector<std::uint32_t> region_owner(program.blocks.size(), 0U);
  std::vector<std::uint32_t> expression_regions(program.expressions.size(), 0U);
  std::vector<bool> owned_presence(program.instructions.size(), false);
  std::vector<bool> owned_initialization(program.instructions.size(), false);
  std::uint32_t next_region = 0U;
  for (std::size_t index = 1U; index < program.functions.size(); ++index) {
    const auto& function = program.functions[index];
    if (program.source_language != SourceLanguage::matlab || !function.origin.valid()) {
      if (!function.parameter_defaults.empty())
        fail(diagnostics, {1U, 1U}, stage, "unexpected defaults on a non-Matlab function");
      continue;
    }
    const auto owner = owners.find(function.origin.value());
    if (owner == owners.end()) {
      if (!function.parameter_defaults.empty())
        fail(diagnostics, {1U, 1U}, stage, "default flow has no source function owner");
      continue;
    }
    const auto& statement = *owner->second;
    const SourceLocation location{statement.line, 1U};
    std::vector<std::size_t> expected;
    for (std::size_t ordinal = 0U; ordinal < statement.parameter_defaults.size(); ++ordinal)
      if (statement.parameter_defaults[ordinal].valid()) expected.push_back(ordinal);
    if (expected.size() != function.parameter_defaults.size()) {
      fail(diagnostics, location, stage, "default inventory does not match its source owner");
      continue;
    }
    BlockId previous_merge;
    for (std::size_t ordinal = 0U; ordinal < function.parameter_defaults.size(); ++ordinal) {
      const auto& flow = function.parameter_defaults[ordinal];
      if (flow.parameter != expected[ordinal] ||
          flow.parameter >= function.parameter_types.size() ||
          flow.parameter >= function.parameter_optional.size() ||
          !function.parameter_optional[flow.parameter] || !valid(flow.storage, program.storages) ||
          !valid(function.entry, program.blocks) ||
          flow.parameter >= program.blocks[function.entry.value()].arguments.size() ||
          !valid(flow.test_block, program.blocks) || !valid(flow.present_block, program.blocks) ||
          !valid(flow.default_exit, program.blocks) || !valid(flow.merge_block, program.blocks) ||
          flow.default_blocks.empty() || !valid(flow.presence, program.instructions) ||
          !valid(flow.initialization, program.instructions)) {
        fail(diagnostics, location, stage,
             "invalid formal, default region, or instruction identity");
        continue;
      }
      const auto* expression =
          mir::expression(program, statement.parameter_defaults[flow.parameter]);
      if (expression == nullptr) {
        fail(diagnostics, location, stage, "default flow has no owned source expression");
        continue;
      }
      if (owned_presence[flow.presence.value()] ||
          owned_initialization[flow.initialization.value()])
        fail(diagnostics, location, stage, "default instruction is shared between flows");
      owned_presence[flow.presence.value()] = true;
      owned_initialization[flow.initialization.value()] = true;
      const auto& storage = program.storages[flow.storage.value()];
      const auto& test = program.blocks[flow.test_block.value()];
      const auto& present = program.blocks[flow.present_block.value()];
      const auto& exit = program.blocks[flow.default_exit.value()];
      const auto& merge = program.blocks[flow.merge_block.value()];
      const auto& presence = program.instructions[flow.presence.value()];
      const auto& initialize = program.instructions[flow.initialization.value()];
      const auto& formal = program.blocks[function.entry.value()].arguments[flow.parameter];
      const auto* presence_type =
          valid(presence.type, program.types) ? &program.types[presence.type.value()] : nullptr;
      const auto* presence_shape =
          valid(presence.shape, program.shapes) ? &program.shapes[presence.shape.value()] : nullptr;
      if (flow.source != expression->origin || presence.origin != flow.source ||
          formal.storage != flow.storage ||
          block_functions[flow.test_block.value()] != function.id ||
          block_functions[flow.present_block.value()] != function.id ||
          block_functions[flow.default_exit.value()] != function.id ||
          block_functions[flow.merge_block.value()] != function.id ||
          storage.kind != StorageKind::parameter || !storage.optional ||
          storage.type != function.parameter_types[flow.parameter] ||
          flow.parameter >= function.parameter_shapes.size() ||
          storage.shape != function.parameter_shapes[flow.parameter] ||
          flow.parameter >= statement.parameter_symbols.size() ||
          storage.symbol != statement.parameter_symbols[flow.parameter] ||
          presence.opcode != Opcode::parameter_presence ||
          presence.intrinsic != IntrinsicId::none || presence.callee.valid() ||
          presence.storage != flow.storage || presence.operands.size() != 1U ||
          presence.operands.front() != formal.value || presence_type == nullptr ||
          presence_type->value_type != ValueType::boolean ||
          presence_type->numeric_type != logical_numeric_type || presence_shape == nullptr ||
          !presence_shape->extents.empty() || presence_shape->dynamic_rank ||
          !presence.result.valid() ||
          instruction_blocks[flow.presence.value()] != flow.test_block ||
          test.terminator.kind != TerminatorKind::conditional_branch ||
          test.terminator.operands.size() != 1U ||
          test.terminator.operands.front() != presence.result ||
          test.terminator.successors.size() != 2U ||
          test.terminator.successors[0] != flow.present_block ||
          test.terminator.successors[1] != flow.default_blocks.front() ||
          !present.instructions.empty() || !branches_to(present, flow.merge_block) ||
          !branches_to(exit, flow.merge_block) ||
          (previous_merge.valid() ? flow.test_block != previous_merge
                                  : flow.test_block != function.entry)) {
        fail(diagnostics, location, stage,
             "presence guard, formal identity, or declaration order is invalid");
      }
      previous_merge = flow.merge_block;
      const auto region = ++next_region;
      bool valid_region = true;
      for (const auto block : flow.default_blocks) {
        if (!valid(block, region_owner) || region_owner[block.value()] != 0U ||
            block == flow.test_block || block == flow.present_block || block == flow.merge_block ||
            block_functions[block.value()] != function.id) {
          valid_region = false;
          continue;
        }
        region_owner[block.value()] = region;
      }
      if (!valid_region || region_owner[flow.default_exit.value()] != region) {
        fail(diagnostics, location, stage,
             "default blocks have duplicate, foreign, or missing ownership");
        continue;
      }
      for (const auto block_id : flow.default_blocks) {
        const auto& block = program.blocks[block_id.value()];
        for (const auto predecessor : incoming[block_id.value()]) {
          if (region_owner[predecessor.value()] != region &&
              !(block_id == flow.default_blocks.front() && predecessor == flow.test_block))
            fail(diagnostics, location, stage,
                 "default region is reachable without its absence guard");
        }
        for (const auto successor : block.terminator.successors) {
          if (!valid(successor, region_owner) ||
              (region_owner[successor.value()] != region &&
               !(block_id == flow.default_exit && successor == flow.merge_block)))
            fail(diagnostics, location, stage, "default region escapes before initialization");
        }
      }
      if (initialize.opcode != Opcode::store || initialize.storage != flow.storage ||
          initialize.type != storage.type || initialize.shape != storage.shape ||
          initialize.operands.size() != 1U || initialize.operands.front() != expression->value_id ||
          !initialize.result.valid() || initialize.origin != expression->origin ||
          instruction_blocks[flow.initialization.value()] != flow.default_exit) {
        fail(diagnostics, location, stage, "default result is not published to its formal storage");
      }
      const auto argument =
          std::find_if(merge.arguments.begin(), merge.arguments.end(),
                       [&](const BlockArgument& item) { return item.value == flow.result; });
      if (argument == merge.arguments.end() || argument->storage != flow.storage ||
          argument->type != storage.type || argument->shape != storage.shape) {
        fail(diagnostics, location, stage,
             "default paths do not merge a typed formal storage version");
        continue;
      }
      const auto position = static_cast<std::size_t>(argument - merge.arguments.begin());
      if (presence.operands.size() != 1U || present.terminator.successor_arguments.size() != 1U ||
          exit.terminator.successor_arguments.size() != 1U ||
          position >= present.terminator.successor_arguments.front().size() ||
          position >= exit.terminator.successor_arguments.front().size() ||
          present.terminator.successor_arguments.front()[position] != presence.operands.front() ||
          exit.terminator.successor_arguments.front()[position] != initialize.result) {
        fail(diagnostics, location, stage,
             "present and absent edges carry incorrect storage versions");
      }
      std::vector<MirExpressionId> pending{expression->id};
      while (!pending.empty()) {
        const auto id = pending.back();
        pending.pop_back();
        const auto* node = mir::expression(program, id);
        if (node == nullptr || !valid(node->instruction, instruction_blocks)) continue;
        if (expression_regions[id.value()] != 0U) {
          fail(diagnostics, node->location, stage,
               "default expression has shared or cyclic ownership");
          continue;
        }
        expression_regions[id.value()] = region;
        const auto block = instruction_blocks[node->instruction.value()];
        if (!valid(block, region_owner) || region_owner[block.value()] != region)
          fail(diagnostics, node->location, stage,
               "default expression is evaluated outside its guarded region");
        pending.insert(pending.end(), node->children.begin(), node->children.end());
      }
    }
    if (previous_merge.valid() &&
        (!valid(statement.instruction, instruction_blocks) ||
         instruction_blocks[statement.instruction.value()] != previous_merge))
      fail(diagnostics, location, stage, "function body starts before its default sequence merges");
  }
  for (const auto instruction : presence_instructions)
    if (!valid(instruction, owned_presence) || !owned_presence[instruction.value()])
      fail(diagnostics, {1U, 1U}, stage,
           "parameter-presence instruction has no default flow owner");
}

}  // namespace mpf::detail::mir
