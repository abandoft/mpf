#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "mir.hpp"

namespace mpf::detail::mir {
namespace {

template <typename Id, typename Item>
bool valid_index(const Id id, const std::vector<Item>& items) noexcept {
  return id.valid() && static_cast<std::size_t>(id.value()) < items.size();
}

void add_error(std::vector<Diagnostic>& diagnostics, const SourceLocation location,
               const std::string_view stage, std::string message) {
  diagnostics.push_back(
      {DiagnosticSeverity::error, "MPF0006",
       "invalid MIR alias/effect table at '" + std::string(stage) + "': " + std::move(message),
       location});
}

Effect intrinsic_effects(const IntrinsicId intrinsic) noexcept {
  switch (intrinsic) {
    case IntrinsicId::none:
      return Effect::read | Effect::write | Effect::may_fail | Effect::external_unknown;
    case IntrinsicId::absolute:
    case IntrinsicId::arc_cosine:
    case IntrinsicId::arc_sine:
    case IntrinsicId::arc_tangent:
    case IntrinsicId::square_root:
    case IntrinsicId::complex_value:
    case IntrinsicId::conjugate:
    case IntrinsicId::imaginary_part:
    case IntrinsicId::real_part:
    case IntrinsicId::imaginary_unit:
    case IntrinsicId::sine:
    case IntrinsicId::cosine:
    case IntrinsicId::tangent:
    case IntrinsicId::exponential:
    case IntrinsicId::logarithm:
    case IntrinsicId::maximum:
    case IntrinsicId::minimum:
    case IntrinsicId::round:
    case IntrinsicId::floor:
    case IntrinsicId::ceiling:
    case IntrinsicId::not_a_number:
    case IntrinsicId::infinity:
    case IntrinsicId::present: return Effect::none;
    case IntrinsicId::python_float:
    case IntrinsicId::python_length:
    case IntrinsicId::matlab_length:
    case IntrinsicId::element_count:
    case IntrinsicId::logical_all:
    case IntrinsicId::logical_any:
    case IntrinsicId::sum: return Effect::may_fail;
    case IntrinsicId::reshape: return Effect::allocate | Effect::may_fail;
    case IntrinsicId::count: break;
  }
  return Effect::read | Effect::write | Effect::may_fail | Effect::external_unknown;
}

Effect minimum_effects(const Instruction& instruction) noexcept {
  switch (instruction.opcode) {
    case Opcode::invalid:
    case Opcode::literal:
    case Opcode::compare:
    case Opcode::truthiness:
    case Opcode::unary:
    case Opcode::binary:
    case Opcode::member:
    case Opcode::expression:
    case Opcode::function:
    case Opcode::identifier: return Effect::none;
    case Opcode::load: return Effect::read;
    case Opcode::comparison_chain:
    case Opcode::conditional:
    case Opcode::return_value:
    case Opcode::selection:
    case Opcode::loop:
    case Opcode::control: return Effect::control;
    case Opcode::call:
      return instruction.callee.valid() ? Effect::none : intrinsic_effects(instruction.intrinsic);
    case Opcode::index:
    case Opcode::slice: return Effect::read | Effect::may_fail;
    case Opcode::aggregate:
    case Opcode::allocate: return Effect::allocate;
    case Opcode::store:
    case Opcode::store_indexed:
    case Opcode::writeback: return Effect::write;
    case Opcode::copy:
      return Effect::allocate |
             (instruction.transfer == ArgumentTransfer::copy_in_out ? Effect::read : Effect::none);
    case Opcode::output: return Effect::io;
  }
  return Effect::none;
}

bool effect_contains(const Effect set, const Effect required) noexcept {
  return (set.bits() & required.bits()) == required.bits();
}

template <typename Id>
bool insert_sorted(std::vector<Id>& values, const Id value) {
  if (!value.valid()) return false;
  const auto position = std::lower_bound(values.begin(), values.end(), value);
  if (position != values.end() && *position == value) return false;
  values.insert(position, value);
  return true;
}

StorageId storage_root(const Program& program, StorageId storage) noexcept {
  for (std::size_t depth = 0; depth < program.storages.size(); ++depth) {
    if (!valid_index(storage, program.storages)) return {};
    const auto& metadata = program.storages[storage.value()];
    if (metadata.kind != StorageKind::view) return storage;
    storage = metadata.base;
  }
  return {};
}

std::vector<MirFunctionId> instruction_owners(const Program& program) {
  std::vector<MirFunctionId> result(program.instructions.size());
  for (std::size_t function_index = 1; function_index < program.functions.size();
       ++function_index) {
    const auto& function = program.functions[function_index];
    for (const auto block_id : function.blocks) {
      if (!valid_index(block_id, program.blocks)) continue;
      for (const auto instruction_id : program.blocks[block_id.value()].instructions) {
        if (valid_index(instruction_id, result)) result[instruction_id.value()] = function.id;
      }
    }
  }
  return result;
}

std::vector<StorageId> value_storages(const Program& program) {
  std::size_t maximum = 0;
  for (std::size_t block_index = 1; block_index < program.blocks.size(); ++block_index) {
    for (const auto& argument : program.blocks[block_index].arguments) {
      maximum = std::max(maximum, static_cast<std::size_t>(argument.value.value()));
    }
  }
  for (std::size_t index = 1; index < program.instructions.size(); ++index) {
    maximum =
        std::max(maximum, static_cast<std::size_t>(program.instructions[index].result.value()));
  }
  std::vector<StorageId> result(maximum + 1U);
  for (std::size_t block_index = 1; block_index < program.blocks.size(); ++block_index) {
    for (const auto& argument : program.blocks[block_index].arguments) {
      if (argument.value.valid()) result[argument.value.value()] = argument.storage;
    }
  }
  for (std::size_t index = 1; index < program.instructions.size(); ++index) {
    const auto& instruction = program.instructions[index];
    if (instruction.result.valid()) result[instruction.result.value()] = instruction.storage;
  }
  return result;
}

std::vector<StorageId> function_parameter_storages(const Program& program,
                                                   const Function& function) {
  std::vector<StorageId> result(function.parameter_types.size());
  if (!valid_index(function.entry, program.blocks)) return result;
  const auto& arguments = program.blocks[function.entry.value()].arguments;
  for (std::size_t index = 0; index < result.size() && index < arguments.size(); ++index) {
    result[index] = arguments[index].storage;
  }
  return result;
}

MemoryAccessMode merged_mode(const MemoryAccessMode current, const bool read,
                             const bool write) noexcept {
  const auto reads = memory_access_reads(current) || read;
  const auto writes = memory_access_writes(current) || write;
  if (reads && writes) return MemoryAccessMode::read_write;
  if (reads) return MemoryAccessMode::read;
  if (writes) return MemoryAccessMode::write;
  return MemoryAccessMode::none;
}

bool apply_storage_access(const Program& program, InstructionEffectFacts& facts,
                          const StorageId storage, const StorageRegion& region, const bool read,
                          const bool write, const bool record_local = true) {
  if (!storage.valid()) return false;
  bool changed = false;
  if (read) {
    changed = insert_sorted(facts.reads, storage) || changed;
    if (record_local) facts.local |= Effect::read;
    facts.effects |= Effect::read;
  }
  if (write) {
    changed = insert_sorted(facts.writes, storage) || changed;
    if (record_local) facts.local |= Effect::write;
    facts.effects |= Effect::write;
  }
  const auto root = storage_root(program, storage);
  const auto found = std::find_if(
      facts.memory_accesses.begin(), facts.memory_accesses.end(), [&](const MemoryAccess& access) {
        return access.storage == storage && access.root == root && access.region == region;
      });
  if (found == facts.memory_accesses.end()) {
    facts.memory_accesses.push_back({storage, root, region, merged_mode({}, read, write)});
    return true;
  }
  const auto mode = merged_mode(found->mode, read, write);
  if (mode != found->mode) {
    found->mode = mode;
    changed = true;
  }
  return changed;
}

void apply_unknown_access(InstructionEffectFacts& facts, const bool read, const bool write) {
  if (read) {
    facts.reads_unknown = true;
    facts.local |= Effect::read;
    facts.effects |= Effect::read;
  }
  if (write) {
    facts.writes_unknown = true;
    facts.local |= Effect::write;
    facts.effects |= Effect::write;
  }
}

std::size_t parameter_for_storage(const Program& program, const std::vector<StorageId>& parameters,
                                  const StorageId storage) {
  const auto root = storage_root(program, storage);
  for (std::size_t index = 0; index < parameters.size(); ++index) {
    if (storage_root(program, parameters[index]) == root) return index;
  }
  return parameters.size();
}

void incorporate_instruction(const Program& program, const InstructionEffectFacts& instruction,
                             const std::vector<StorageId>& parameters,
                             FunctionEffectFacts& function) {
  const auto memory_bits = Effect::read.bits() | Effect::write.bits();
  function.effects |= Effect{static_cast<std::uint16_t>(instruction.effects.bits() & ~memory_bits)};
  function.reads_unknown = function.reads_unknown || instruction.reads_unknown;
  function.writes_unknown = function.writes_unknown || instruction.writes_unknown;
  if (instruction.reads_unknown) function.effects |= Effect::read;
  if (instruction.writes_unknown) function.effects |= Effect::write;
  for (const auto storage : instruction.reads) {
    const auto parameter = parameter_for_storage(program, parameters, storage);
    if (parameter < function.parameter_reads.size()) {
      function.parameter_reads[parameter] = true;
    } else {
      const auto root = storage_root(program, storage);
      if (valid_index(root, program.storages) &&
          program.storages[root.value()].kind == StorageKind::global) {
        function.effects |= Effect::read;
      }
    }
  }
  for (const auto storage : instruction.writes) {
    const auto parameter = parameter_for_storage(program, parameters, storage);
    if (parameter < function.parameter_writes.size()) {
      function.parameter_writes[parameter] = true;
    } else {
      const auto root = storage_root(program, storage);
      if (valid_index(root, program.storages) &&
          program.storages[root.value()].kind == StorageKind::global) {
        function.effects |= Effect::write;
      }
    }
  }
}

bool update_effect(Effect& target, const Effect source) noexcept {
  const auto merged = target | source;
  if (merged.bits() == target.bits()) return false;
  target = merged;
  return true;
}

template <typename BooleanReference>
bool update_flag(BooleanReference&& target, const bool source) noexcept {
  if (target || !source) return false;
  target = true;
  return true;
}

bool same_instruction(const InstructionEffectFacts& left,
                      const InstructionEffectFacts& right) noexcept {
  return left.origin == right.origin && left.local.bits() == right.local.bits() &&
         left.effects.bits() == right.effects.bits() && left.reads == right.reads &&
         left.writes == right.writes && left.memory_accesses == right.memory_accesses &&
         left.reads_unknown == right.reads_unknown && left.writes_unknown == right.writes_unknown;
}

bool same_function(const FunctionEffectFacts& left, const FunctionEffectFacts& right) noexcept {
  return left.origin == right.origin && left.effects.bits() == right.effects.bits() &&
         left.parameter_reads == right.parameter_reads &&
         left.parameter_writes == right.parameter_writes &&
         left.parameter_escapes == right.parameter_escapes &&
         left.reads_unknown == right.reads_unknown && left.writes_unknown == right.writes_unknown;
}

bool same_call(const CallEffectFacts& left, const CallEffectFacts& right) noexcept {
  if (left.instruction != right.instruction || left.caller != right.caller ||
      left.callee != right.callee || left.effects.bits() != right.effects.bits() ||
      left.reads != right.reads || left.writes != right.writes ||
      left.reads_unknown != right.reads_unknown || left.writes_unknown != right.writes_unknown ||
      left.arguments.size() != right.arguments.size() ||
      left.overlaps.size() != right.overlaps.size()) {
    return false;
  }
  for (std::size_t index = 0; index < left.arguments.size(); ++index) {
    const auto& actual = left.arguments[index];
    const auto& expected = right.arguments[index];
    if (actual.ordinal != expected.ordinal || actual.storage != expected.storage ||
        actual.root != expected.root || actual.transfer != expected.transfer ||
        actual.region != expected.region || actual.reads != expected.reads ||
        actual.writes != expected.writes || actual.escapes != expected.escapes) {
      return false;
    }
  }
  for (std::size_t index = 0; index < left.overlaps.size(); ++index) {
    const auto& actual = left.overlaps[index];
    const auto& expected = right.overlaps[index];
    if (actual.left != expected.left || actual.right != expected.right ||
        actual.relation != expected.relation ||
        actual.writable_conflict != expected.writable_conflict) {
      return false;
    }
  }
  return true;
}

}  // namespace

const InstructionEffectFacts* AliasEffectTable::instruction(const InstructionId id) const noexcept {
  return id.valid() && id.value() < instructions.size() ? &instructions[id.value()] : nullptr;
}

const StorageAliasFacts* AliasEffectTable::storage(const StorageId id) const noexcept {
  return id.valid() && id.value() < storages.size() ? &storages[id.value()] : nullptr;
}

const FunctionEffectFacts* AliasEffectTable::function(const MirFunctionId id) const noexcept {
  return id.valid() && id.value() < functions.size() ? &functions[id.value()] : nullptr;
}

const CallEffectFacts* AliasEffectTable::call(const InstructionId instruction_id) const noexcept {
  if (!instruction_id.valid()) return nullptr;
  const auto found = std::lower_bound(
      calls.begin(), calls.end(), instruction_id,
      [](const CallEffectFacts& facts, const InstructionId id) { return facts.instruction < id; });
  return found != calls.end() && found->instruction == instruction_id ? &*found : nullptr;
}

AliasClass alias_between(const AliasEffectTable& analysis, const StorageId left,
                         const StorageId right) noexcept {
  if (!left.valid() || !right.valid() || left.value() >= analysis.storages.size() ||
      right.value() >= analysis.storages.size()) {
    return AliasClass::may_alias;
  }
  if (left == right) return AliasClass::must_alias;
  const auto low = std::min(left, right);
  const auto high = std::max(left, right);
  const auto relation = std::lower_bound(
      analysis.aliases.begin(), analysis.aliases.end(), std::pair{low, high},
      [](const AliasRelation& candidate, const std::pair<StorageId, StorageId>& key) {
        return std::pair{candidate.left, candidate.right} < key;
      });
  if (relation != analysis.aliases.end() && relation->left == low && relation->right == high) {
    return relation->relation;
  }
  const auto& left_facts = analysis.storages[left.value()];
  const auto& right_facts = analysis.storages[right.value()];
  if (!left_facts.root.valid() || !right_facts.root.valid()) return AliasClass::may_alias;
  if (left_facts.root == right_facts.root) return AliasClass::may_alias;
  if (left_facts.root_kind == StorageKind::parameter ||
      right_facts.root_kind == StorageKind::parameter) {
    return AliasClass::may_alias;
  }
  return AliasClass::no_alias;
}

AliasClass alias_between(const AliasEffectTable& analysis, const MemoryAccess& left,
                         const MemoryAccess& right) noexcept {
  const auto* left_storage = analysis.storage(left.storage);
  const auto* right_storage = analysis.storage(right.storage);
  if (left_storage == nullptr || right_storage == nullptr || left.root != left_storage->root ||
      right.root != right_storage->root) {
    return AliasClass::may_alias;
  }
  if (left.root.valid() && left.root == right.root) {
    const auto relation = storage_region_relation(left.region, right.region);
    if (relation == StorageRegionRelation::disjoint) return AliasClass::no_alias;
    if (relation == StorageRegionRelation::identical) return AliasClass::must_alias;
    return AliasClass::may_alias;
  }
  return alias_between(analysis, left.storage, right.storage);
}

bool memory_accesses_conflict(const AliasEffectTable& analysis, const MemoryAccess& left,
                              const MemoryAccess& right) noexcept {
  return (memory_access_writes(left.mode) || memory_access_writes(right.mode)) &&
         alias_between(analysis, left, right) != AliasClass::no_alias;
}

AliasEffectTable analyze_alias_effects(const Program& program) {
  AliasEffectTable result;
  result.mir_revision = program.revision;
  result.storage_count = program.storages.size() - (program.storages.empty() ? 0U : 1U);
  result.instruction_count = program.instructions.size() - (program.instructions.empty() ? 0U : 1U);
  result.function_count = program.functions.size() - (program.functions.empty() ? 0U : 1U);
  result.call_count = program.calls.size();
  result.storages.resize(program.storages.size());
  result.instructions.resize(program.instructions.size());
  result.functions.resize(program.functions.size());

  for (std::size_t index = 1; index < program.storages.size(); ++index) {
    const auto id = StorageId{static_cast<StorageId::value_type>(index)};
    const auto root = storage_root(program, id);
    const auto root_kind = valid_index(root, program.storages) ? program.storages[root.value()].kind
                                                               : StorageKind::temporary;
    result.storages[index] = {id, root, root_kind, root_kind == StorageKind::global};
    if (program.storages[index].kind == StorageKind::view && root.valid()) {
      const auto low = std::min(id, root);
      const auto high = std::max(id, root);
      result.aliases.push_back({low, high, AliasClass::may_alias, program.storages[index].origin});
    }
  }
  std::sort(result.aliases.begin(), result.aliases.end(),
            [](const AliasRelation& left, const AliasRelation& right) {
              return std::pair{left.left, left.right} < std::pair{right.left, right.right};
            });
  result.aliases.erase(std::unique(result.aliases.begin(), result.aliases.end(),
                                   [](const AliasRelation& left, const AliasRelation& right) {
                                     return left.left == right.left && left.right == right.right;
                                   }),
                       result.aliases.end());

  const auto owners = instruction_owners(program);
  const auto value_storage = value_storages(program);
  std::vector<std::vector<StorageId>> parameters(program.functions.size());
  for (std::size_t index = 1; index < program.functions.size(); ++index) {
    const auto& function = program.functions[index];
    parameters[index] = function_parameter_storages(program, function);
    result.functions[index].origin = function.id;
    result.functions[index].parameter_reads.resize(parameters[index].size(), false);
    result.functions[index].parameter_writes.resize(parameters[index].size(), false);
    result.functions[index].parameter_escapes.resize(parameters[index].size(), false);
  }

  for (std::size_t index = 1; index < program.instructions.size(); ++index) {
    const auto& instruction = program.instructions[index];
    auto& facts = result.instructions[index];
    facts.origin = instruction.id;
    facts.local = minimum_effects(instruction);
    facts.effects = facts.local;
    if (const auto* instruction_attributes = attributes(program, instruction.id)) {
      for (const auto& access : instruction_attributes->memory_accesses) {
        (void)apply_storage_access(program, facts, access.storage, access.region,
                                   memory_access_reads(access.mode),
                                   memory_access_writes(access.mode));
      }
    }
    if (instruction.opcode == Opcode::call && !instruction.callee.valid()) {
      const bool external = instruction.intrinsic == IntrinsicId::none;
      if (external) apply_unknown_access(facts, true, true);
      for (const auto operand : instruction.operands) {
        const auto storage =
            operand.value() < value_storage.size() ? value_storage[operand.value()] : StorageId{};
        if (!storage.valid()) continue;
        (void)apply_storage_access(program, facts, storage, {}, true, external);
        if (external) {
          const auto root = storage_root(program, storage);
          if (root.valid()) result.storages[root.value()].escapes = true;
        }
      }
    }
    if (index < owners.size() && owners[index].valid()) {
      auto& function = result.functions[owners[index].value()];
      incorporate_instruction(program, facts, parameters[owners[index].value()], function);
      if (instruction.opcode == Opcode::call && instruction.intrinsic == IntrinsicId::none &&
          !instruction.callee.valid()) {
        for (const auto storage : facts.reads) {
          const auto parameter =
              parameter_for_storage(program, parameters[owners[index].value()], storage);
          if (parameter < function.parameter_escapes.size()) {
            function.parameter_escapes[parameter] = true;
          }
        }
      }
    }
  }

  const auto iteration_limit = program.functions.size() * 4U + 8U;
  for (std::size_t iteration = 0; iteration < iteration_limit; ++iteration) {
    bool changed = false;
    for (const auto& call : program.calls) {
      if (!valid_index(call.instruction, result.instructions) ||
          !valid_index(call.caller, result.functions) ||
          !valid_index(call.callee, result.functions)) {
        continue;
      }
      auto& instruction = result.instructions[call.instruction.value()];
      auto& caller = result.functions[call.caller.value()];
      const auto& callee = result.functions[call.callee.value()];
      changed = update_effect(instruction.effects, callee.effects) || changed;
      changed = update_effect(caller.effects, callee.effects) || changed;
      changed = update_flag(instruction.reads_unknown, callee.reads_unknown) || changed;
      changed = update_flag(instruction.writes_unknown, callee.writes_unknown) || changed;
      changed = update_flag(caller.reads_unknown, callee.reads_unknown) || changed;
      changed = update_flag(caller.writes_unknown, callee.writes_unknown) || changed;
      for (std::size_t argument = 0; argument < call.arguments.size(); ++argument) {
        if (call.arguments[argument].transfer == ArgumentTransfer::omitted) continue;
        const auto actual = call.arguments[argument].storage;
        if (!actual.valid()) continue;
        const bool reads =
            argument < callee.parameter_reads.size() && callee.parameter_reads[argument];
        const bool writes =
            argument < callee.parameter_writes.size() && callee.parameter_writes[argument];
        if (reads || writes) {
          changed = apply_storage_access(program, instruction, actual,
                                         call.arguments[argument].region, reads, writes, false) ||
                    changed;
        }
        if (reads) changed = update_effect(instruction.effects, Effect::read) || changed;
        if (writes) changed = update_effect(instruction.effects, Effect::write) || changed;
        const auto caller_parameter =
            parameter_for_storage(program, parameters[call.caller.value()], actual);
        if (caller_parameter < caller.parameter_reads.size()) {
          changed = update_flag(caller.parameter_reads[caller_parameter], reads) || changed;
          changed = update_flag(caller.parameter_writes[caller_parameter], writes) || changed;
          const bool escapes =
              argument < callee.parameter_escapes.size() && callee.parameter_escapes[argument];
          changed = update_flag(caller.parameter_escapes[caller_parameter], escapes) || changed;
          if (escapes) {
            const auto root = storage_root(program, actual);
            if (root.valid()) result.storages[root.value()].escapes = true;
          }
        } else {
          const auto root = storage_root(program, actual);
          if (valid_index(root, program.storages) &&
              program.storages[root.value()].kind == StorageKind::global) {
            if (reads) changed = update_effect(caller.effects, Effect::read) || changed;
            if (writes) changed = update_effect(caller.effects, Effect::write) || changed;
          }
        }
      }
    }
    if (!changed) break;
  }

  for (std::size_t index = 1; index < result.storages.size(); ++index) {
    const auto root = result.storages[index].root;
    if (valid_index(root, result.storages)) {
      result.storages[index].escapes = result.storages[root.value()].escapes;
    }
  }

  result.calls.reserve(program.calls.size());
  for (const auto& call : program.calls) {
    CallEffectFacts facts;
    facts.instruction = call.instruction;
    facts.caller = call.caller;
    facts.callee = call.callee;
    if (const auto* instruction = result.instruction(call.instruction)) {
      facts.effects = instruction->effects;
      facts.reads = instruction->reads;
      facts.writes = instruction->writes;
      facts.reads_unknown = instruction->reads_unknown;
      facts.writes_unknown = instruction->writes_unknown;
    }
    const auto* callee = result.function(call.callee);
    facts.arguments.reserve(call.arguments.size());
    for (std::size_t index = 0; index < call.arguments.size(); ++index) {
      const auto& argument = call.arguments[index];
      const auto reads = callee != nullptr && index < callee->parameter_reads.size() &&
                         callee->parameter_reads[index];
      const auto writes = callee != nullptr && index < callee->parameter_writes.size() &&
                          callee->parameter_writes[index];
      const auto escapes = callee != nullptr && index < callee->parameter_escapes.size() &&
                           callee->parameter_escapes[index];
      facts.arguments.push_back({static_cast<std::uint32_t>(index), argument.storage, argument.root,
                                 argument.transfer, argument.region, reads, writes, escapes});
    }
    for (std::size_t left = 0; left < call.arguments.size(); ++left) {
      if (!call.arguments[left].storage.valid()) continue;
      for (std::size_t right = left + 1U; right < call.arguments.size(); ++right) {
        if (!call.arguments[right].storage.valid()) continue;
        const MemoryAccess left_access{call.arguments[left].storage, call.arguments[left].root,
                                       call.arguments[left].region, MemoryAccessMode::read};
        const MemoryAccess right_access{call.arguments[right].storage, call.arguments[right].root,
                                        call.arguments[right].region, MemoryAccessMode::read};
        const auto relation = alias_between(result, left_access, right_access);
        if (relation == AliasClass::no_alias) continue;
        facts.overlaps.push_back({static_cast<std::uint32_t>(left),
                                  static_cast<std::uint32_t>(right), relation,
                                  argument_transfer_writes(call.arguments[left].transfer) &&
                                      argument_transfer_writes(call.arguments[right].transfer)});
      }
    }
    result.calls.push_back(std::move(facts));
  }
  std::sort(result.calls.begin(), result.calls.end(),
            [](const CallEffectFacts& left, const CallEffectFacts& right) {
              return left.instruction < right.instruction;
            });
  return result;
}

bool alias_effects_current(const Program& program, const AliasEffectTable& analysis) noexcept {
  return analysis.mir_revision == program.revision &&
         analysis.storage_count + 1U == program.storages.size() &&
         analysis.instruction_count + 1U == program.instructions.size() &&
         analysis.function_count + 1U == program.functions.size() &&
         analysis.call_count == program.calls.size() &&
         analysis.storages.size() == program.storages.size() &&
         analysis.instructions.size() == program.instructions.size() &&
         analysis.functions.size() == program.functions.size() &&
         analysis.calls.size() == program.calls.size();
}

std::vector<Diagnostic> verify_alias_effects(const Program& program,
                                             const AliasEffectTable& analysis,
                                             const std::string_view stage) {
  std::vector<Diagnostic> diagnostics;
  if (analysis.mir_revision != program.revision) {
    add_error(diagnostics, {1, 1}, stage, "MIR revision is stale");
  }
  if (!alias_effects_current(program, analysis)) {
    if (analysis.mir_revision == program.revision) {
      add_error(diagnostics, {1, 1}, stage, "dense analysis inventories disagree with MIR");
    }
    return diagnostics;
  }
  const auto expected = analyze_alias_effects(program);
  for (std::size_t index = 1; index < analysis.storages.size(); ++index) {
    const auto& facts = analysis.storages[index];
    if (facts.origin.value() != index || !valid_index(facts.root, program.storages) ||
        program.storages[facts.root.value()].kind == StorageKind::view ||
        facts.root_kind != program.storages[facts.root.value()].kind ||
        facts.origin != expected.storages[index].origin ||
        facts.root != expected.storages[index].root ||
        facts.root_kind != expected.storages[index].root_kind ||
        facts.escapes != expected.storages[index].escapes) {
      add_error(diagnostics, {1, 1}, stage, "storage alias facts are invalid or stale");
    }
  }
  if (analysis.aliases.size() != expected.aliases.size()) {
    add_error(diagnostics, {1, 1}, stage, "sparse alias relation count is incorrect");
  } else {
    for (std::size_t index = 0; index < analysis.aliases.size(); ++index) {
      const auto& actual = analysis.aliases[index];
      const auto& wanted = expected.aliases[index];
      if (actual.left != wanted.left || actual.right != wanted.right ||
          actual.relation != wanted.relation || actual.origin != wanted.origin) {
        add_error(diagnostics, {1, 1}, stage, "sparse alias relation is invalid or unsorted");
      }
    }
  }
  for (std::size_t index = 1; index < analysis.instructions.size(); ++index) {
    const auto& facts = analysis.instructions[index];
    const auto& instruction = program.instructions[index];
    if (!same_instruction(facts, expected.instructions[index]) ||
        !effect_contains(facts.local, minimum_effects(instruction)) ||
        !effect_contains(facts.effects, facts.local) ||
        ((!facts.reads.empty() || facts.reads_unknown) &&
         !has_effect(facts.effects, Effect::read)) ||
        ((!facts.writes.empty() || facts.writes_unknown) &&
         !has_effect(facts.effects, Effect::write))) {
      add_error(diagnostics, instruction.location, stage,
                "instruction effect facts are weaker than the operation or dataflow requires");
    }
    for (const auto storage : facts.reads) {
      if (!valid_index(storage, program.storages)) {
        add_error(diagnostics, instruction.location, stage,
                  "instruction read set references invalid storage");
      }
    }
    for (const auto storage : facts.writes) {
      if (!valid_index(storage, program.storages)) {
        add_error(diagnostics, instruction.location, stage,
                  "instruction write set references invalid storage");
      }
    }
    for (const auto& access : facts.memory_accesses) {
      if (!valid_index(access.storage, program.storages) ||
          !valid_index(access.root, program.storages) ||
          access.root != storage_root(program, access.storage) ||
          access.mode == MemoryAccessMode::none || !valid_storage_region(access.region) ||
          (memory_access_reads(access.mode) &&
           !std::binary_search(facts.reads.begin(), facts.reads.end(), access.storage)) ||
          (memory_access_writes(access.mode) &&
           !std::binary_search(facts.writes.begin(), facts.writes.end(), access.storage))) {
        add_error(diagnostics, instruction.location, stage,
                  "instruction memory-access facts are invalid or disagree with read/write sets");
      }
    }
  }
  for (std::size_t index = 1; index < analysis.functions.size(); ++index) {
    const auto& facts = analysis.functions[index];
    if (!same_function(facts, expected.functions[index]) ||
        facts.parameter_reads.size() != program.functions[index].parameter_types.size() ||
        facts.parameter_writes.size() != facts.parameter_reads.size() ||
        facts.parameter_escapes.size() != facts.parameter_reads.size()) {
      add_error(diagnostics, {1, 1}, stage,
                "function effect summary is not a fixed point over its call graph");
    }
  }
  for (std::size_t index = 0; index < analysis.calls.size(); ++index) {
    if (!same_call(analysis.calls[index], expected.calls[index])) {
      add_error(diagnostics, {1, 1}, stage,
                "call effect instantiation disagrees with its callee summary");
    }
    for (const auto& overlap : analysis.calls[index].overlaps) {
      if (overlap.writable_conflict) {
        add_error(diagnostics, {1, 1}, stage,
                  "multiple writable call arguments may overlap the same storage region");
      }
    }
  }
  return diagnostics;
}

}  // namespace mpf::detail::mir
