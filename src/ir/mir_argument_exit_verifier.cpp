#include <algorithm>
#include <unordered_map>

#include "mir_argument_entry.hpp"
#include "mir_argument_exit.hpp"

namespace mpf::detail::mir {
namespace {
template <typename Id, typename Items>
bool valid(const Id id, const Items& items) noexcept {
  return id.valid() && id.value() < items.size();
}

class ExitVerifier final {
 public:
  ExitVerifier(const Program& program, std::vector<Diagnostic>& diagnostics,
               const std::string_view stage)
      : program_(program),
        diagnostics_(diagnostics),
        stage_(stage),
        used_(program.argument_operations.size()),
        storages_(program.storages.size()),
        blocks_(program.instructions.size()),
        incoming_(program.blocks.size()),
        block_owners_(program.blocks.size()),
        protected_(program.blocks.size()) {
    for (const auto& node : program.statements)
      if (node.kind == StatementKind::function) owners_.emplace(node.origin, &node);
    for (const auto& function : program.functions)
      for (const auto block : function.blocks)
        if (valid(block, block_owners_)) block_owners_[block.value()] = function.id;
    for (const auto& region : program.exception_regions) {
      if (valid(region.handler, protected_)) protected_[region.handler.value()] = true;
      for (const auto block : region.protected_blocks)
        if (valid(block, protected_)) protected_[block.value()] = true;
    }
    for (const auto& block : program.blocks) {
      for (const auto instruction : block.instructions)
        if (valid(instruction, blocks_)) blocks_[instruction.value()] = block.id;
      for (const auto successor : block.terminator.successors)
        if (valid(successor, incoming_)) incoming_[successor.value()].push_back(block.id);
    }
  }

  void verify() {
    for (const auto& function : program_.functions) verify_function(function);
    for (std::size_t index = 1U; index < used_.size(); ++index)
      if (program_.argument_operations[index].direction == ArgumentDirection::output &&
          !used_[index])
        fail({1U, 1U}, "orphan output argument operation");
  }

 private:
  bool owned(const BlockId block, const Function& function) const noexcept {
    return valid(block, block_owners_) && block_owners_[block.value()] == function.id;
  }

  void fail(const SourceLocation location, const std::string_view message) {
    diagnostics_.push_back({DiagnosticSeverity::error, "MPF0006",
                            "invalid MIR argument-exit sequence at '" + std::string(stage_) +
                                "': " + std::string(message),
                            location});
  }

  MemoryAccess access(const StorageId storage, const MemoryAccessMode mode,
                      const bool workspace) const {
    if (!valid(storage, program_.storages)) return {};
    auto root = storage;
    for (std::size_t count = 0U; count < program_.storages.size(); ++count) {
      const auto base = program_.storages[root.value()].base;
      if (!base.valid()) break;
      if (!valid(base, program_.storages)) return {};
      root = base;
    }
    const auto* shape = mir::shape(program_, program_.storages[root.value()].shape);
    return {storage, root,
            workspace || shape == nullptr ? StorageRegion{} : full_storage_region(shape->extents),
            mode};
  }

  bool accesses_are(const InstructionId instruction,
                    const std::vector<MemoryAccess>& accesses) const {
    const auto* row = attributes(program_, instruction);
    return row != nullptr && row->memory_accesses == accesses;
  }

  bool plain(const ArgumentOperation& operation) const {
    return operation.class_constraint == ArgumentClassConstraint::none &&
           !operation.dimensions_declared && operation.dimensions.empty() && operation.rank == 0U &&
           operation.validator == ArgumentValidatorPlan{};
  }

  const ArgumentOperation* consume(const InstructionId instruction,
                                   const ArgumentOperationKind kind, const Function& function,
                                   const ArgumentOutputFlow& flow, std::size_t& cursor,
                                   const SourceLocation location) {
    const auto* operation = argument_operation(program_, instruction);
    const auto& block = program_.blocks[flow.block.value()];
    if (!valid(instruction, program_.instructions) || operation == nullptr ||
        operation->direction != ArgumentDirection::output || operation->kind != kind ||
        operation->owner != function.origin || operation->parameter != flow.output ||
        blocks_[instruction.value()] != flow.block || cursor >= block.instructions.size() ||
        block.instructions[cursor++] != instruction) {
      fail(location, "operation identity, output owner, direction or order is invalid");
      return nullptr;
    }
    const auto id = attributes(program_, instruction)->argument_operation;
    if (used_[id.value()]) fail(location, "output operation has multiple owners");
    used_[id.value()] = true;
    return operation;
  }

  void verify_function(const Function& function) {
    const auto found = owners_.find(function.origin);
    const auto* owner = found == owners_.end() ? nullptr : found->second;
    std::vector<const ArgumentValidationPlan*> declarations;
    if (owner != nullptr)
      for (const auto& plan : owner->argument_validations)
        if (plan.direction == ArgumentDirection::output) declarations.push_back(&plan);
    if (declarations.empty()) {
      if (!(function.argument_exit == ArgumentExitFlow{}) || !function.argument_outputs.empty())
        fail({1U, 1U}, "output exit has no declaration owner");
      return;
    }
    const auto& exit = function.argument_exit;
    const SourceLocation location{owner->line, 1U};
    if (program_.source_language != SourceLanguage::matlab || owner->origin != function.origin ||
        owner->kind != StatementKind::function || owner->id != exit.owner ||
        exit.origin != function.origin || declarations.size() != owner->return_names.size() ||
        declarations.size() != owner->return_symbols.size() ||
        declarations.size() != function.result_types.size() ||
        declarations.size() != function.result_shapes.size() ||
        declarations.size() != function.argument_outputs.size() ||
        !valid(exit.merge, program_.blocks) || !valid(exit.continuation, program_.blocks) ||
        exit.returns.empty() || !exit.returned.valid()) {
      fail(location, "exit, output and signature inventories disagree");
      return;
    }
    if (!owned(exit.merge, function) || !owned(exit.continuation, function) ||
        !program_.blocks[exit.merge.value()].arguments.empty())
      fail(location, "exit merge is foreign or has undocumented value arguments");
    verify_returns(function, *owner, location);
    std::vector<ValueId> results(function.result_types.size());
    std::vector<MemoryAccess> reads;
    for (std::size_t index = 0U; index < declarations.size(); ++index) {
      const auto& flow = function.argument_outputs[index];
      const auto& plan = *declarations[index];
      if (flow.output != plan.ordinal || flow.output >= function.result_types.size() ||
          !valid(flow.source_storage, program_.storages) ||
          !valid(flow.storage, program_.storages) || flow.storage == flow.source_storage ||
          !valid(flow.selection, program_.instructions) ||
          !valid(flow.normalization, program_.instructions) ||
          !valid(flow.initialization, program_.instructions) ||
          !valid(flow.block, program_.blocks) || !valid(flow.continuation, program_.blocks) ||
          !owned(flow.block, function) || !owned(flow.continuation, function) ||
          flow.validators.size() != plan.validators.size()) {
        fail(location, "output flow has invalid identities or validator inventory");
        continue;
      }
      const auto expected_block =
          index == 0U ? exit.merge : function.argument_outputs[index - 1U].continuation;
      const auto& block = program_.blocks[flow.block.value()];
      if (flow.block != expected_block || flow.block == flow.continuation ||
          block.exception_handler.valid() || block.terminator.kind != TerminatorKind::branch ||
          block.terminator.successors != std::vector<BlockId>{flow.continuation} ||
          block.terminator.successor_arguments != std::vector<std::vector<ValueId>>{{}} ||
          incoming_[flow.continuation.value()] != std::vector<BlockId>{flow.block})
        fail(location, "output sequence can be bypassed or caught by a body handler");
      if (protected_[flow.block.value()])
        fail(location, "output operations reside inside a body exception region");
      const auto& storage = program_.storages[flow.storage.value()];
      const auto& source = program_.storages[flow.source_storage.value()];
      if (storages_[flow.storage.value()] || storage.kind != StorageKind::temporary ||
          storage.lifetime != StorageLifetime::function || storage.symbol.valid() ||
          storage.origin != function.origin || storage.base.valid() ||
          storage.view != StorageViewKind::none || !storage.writable || storage.optional ||
          storage.type != function.result_types[flow.output] ||
          storage.shape != function.result_shapes[flow.output] ||
          source.symbol != owner->return_symbols[flow.output])
        fail(location, "normalized return storage is conflated with workspace storage");
      storages_[flow.storage.value()] = true;
      verify_output(function, *owner, flow, plan, location);
      results[flow.output] = flow.result;
      reads.push_back(access(flow.storage, MemoryAccessMode::read, false));
    }
    if (function.argument_outputs.back().continuation != exit.continuation)
      fail(location, "terminal return does not follow the last output validator");
    verify_terminal(function, results, reads, location);
  }

  void verify_returns(const Function& function, const Statement& owner,
                      const SourceLocation location) {
    const auto& exit = function.argument_exit;
    std::vector<BlockId> predecessors;
    std::vector<HirNodeId> origins;
    std::unordered_map<HirNodeId, const ArgumentReturnSource*> sources;
    std::size_t implicit = 0U;
    for (const auto& source : exit.returns) {
      if (!owned(source.block, function)) {
        fail(location, "return source is outside the function");
        continue;
      }
      const auto& terminator = program_.blocks[source.block.value()].terminator;
      if (terminator.kind != TerminatorKind::branch ||
          terminator.successors != std::vector<BlockId>{exit.merge} ||
          terminator.successor_arguments != std::vector<std::vector<ValueId>>{{}} ||
          !terminator.operands.empty() || terminator.origin != source.origin)
        fail(location, "normal return does not branch directly to the shared exit");
      predecessors.push_back(source.block);
      if (source.implicit) {
        ++implicit;
        if (source.origin != function.origin)
          fail(location, "implicit return has a foreign origin");
      } else {
        origins.push_back(source.origin);
        if (!sources.emplace(source.origin, &source).second)
          fail(location, "explicit return has duplicated source ownership");
      }
    }
    auto incoming = incoming_[exit.merge.value()];
    std::sort(incoming.begin(), incoming.end());
    std::sort(predecessors.begin(), predecessors.end());
    if (implicit > 1U || incoming != predecessors ||
        std::adjacent_find(predecessors.begin(), predecessors.end()) != predecessors.end())
      fail(location, "shared exit has an unauthorized or duplicated incoming path");
    std::vector<HirNodeId> expected;
    std::vector<MirStatementId> pending(owner.body);
    pending.insert(pending.end(), owner.alternative.begin(), owner.alternative.end());
    for (std::size_t index = 0U; index < pending.size(); ++index) {
      const auto* node = statement(program_, pending[index]);
      if (node == nullptr || node->kind == StatementKind::function) continue;
      if (node->kind == StatementKind::return_statement) {
        expected.push_back(node->origin);
        const auto source = sources.find(node->origin);
        if (source == sources.end() || !valid(node->instruction, blocks_) ||
            blocks_[node->instruction.value()] != source->second->block || node->has_expression)
          fail(location, "explicit return lost its source ownership or block");
      }
      pending.insert(pending.end(), node->body.begin(), node->body.end());
      pending.insert(pending.end(), node->alternative.begin(), node->alternative.end());
    }
    std::sort(expected.begin(), expected.end());
    std::sort(origins.begin(), origins.end());
    if (origins != expected)
      fail(location, "explicit return inventory is incomplete or duplicated");
    for (const auto block : function.blocks)
      if (valid(block, program_.blocks)) {
        const auto& data = program_.blocks[block.value()];
        if (data.exception_handler == exit.merge ||
            (data.terminator.kind == TerminatorKind::return_value && block != exit.continuation))
          fail(location, "exception or direct return bypasses output validation");
      }
  }

  void verify_output(const Function& function, const Statement& owner,
                     const ArgumentOutputFlow& flow, const ArgumentValidationPlan& plan,
                     const SourceLocation location) {
    const auto& block = program_.blocks[flow.block.value()];
    std::size_t cursor = 0U;
    const auto* selection =
        consume(flow.selection, ArgumentOperationKind::selection, function, flow, cursor, location);
    const auto& select = program_.instructions[flow.selection.value()];
    const auto* source_shape = mir::shape(program_, select.shape);
    if (selection == nullptr || !plain(*selection) || !selection->literal.empty() ||
        select.opcode != Opcode::identifier || select.origin != function.origin ||
        select.storage != flow.source_storage || !select.operands.empty() ||
        select.result != flow.selected || !flow.selected.valid() ||
        value_type(program_, select.type) != ValueType::unknown || source_shape == nullptr ||
        !source_shape->dynamic_rank || !source_shape->extents.empty() || select.callee.valid() ||
        select.intrinsic != IntrinsicId::none ||
        !accesses_are(flow.selection, {access(flow.source_storage, MemoryAccessMode::read, true)}))
      fail(location, "output does not read the current workspace value");
    const auto* operation = consume(flow.normalization, ArgumentOperationKind::normalization,
                                    function, flow, cursor, location);
    const auto& normalize = program_.instructions[flow.normalization.value()];
    if (operation == nullptr || normalize.opcode != Opcode::argument_normalize ||
        normalize.origin != function.origin || normalize.storage != flow.storage ||
        normalize.type != function.result_types[flow.output] ||
        normalize.shape != function.result_shapes[flow.output] || !normalize.result.valid() ||
        normalize.operands != std::vector<ValueId>{flow.selected} || normalize.callee.valid() ||
        normalize.intrinsic != IntrinsicId::none ||
        operation->class_constraint != plan.class_constraint ||
        operation->dimensions_declared != plan.dimensions_declared ||
        operation->dimensions != plan.dimensions || operation->rank != plan.validated_rank ||
        !operation->literal.empty() || !(operation->validator == ArgumentValidatorPlan{}) ||
        !accesses_are(flow.normalization,
                      {access(flow.source_storage, MemoryAccessMode::read, true)}))
      fail(location, "output normalization disagrees with its declared return signature");
    const auto& initialize = program_.instructions[flow.initialization.value()];
    if (cursor >= block.instructions.size() ||
        block.instructions[cursor++] != flow.initialization || initialize.opcode != Opcode::store ||
        initialize.storage != flow.storage || initialize.origin != function.origin ||
        initialize.callee.valid() || initialize.intrinsic != IntrinsicId::none ||
        initialize.type != normalize.type || initialize.shape != normalize.shape ||
        initialize.operands != std::vector<ValueId>{normalize.result} ||
        initialize.result != flow.result || !flow.result.valid() ||
        !accesses_are(flow.initialization, {access(flow.storage, MemoryAccessMode::write, false)}))
      fail(location, "converted output is not published before its validators");
    for (std::size_t index = 0U; index < plan.validators.size(); ++index) {
      const auto id = flow.validators[index];
      if (!valid(id, program_.instructions)) {
        fail(location, "validator has no resident instruction");
        continue;
      }
      const auto& validator = plan.validators[index];
      const auto& instruction = program_.instructions[id.value()];
      if (instruction.operands.size() != validator.operands.size() + 1U ||
          instruction.operands.front() != flow.result) {
        fail(location, "validator does not consume the converted output");
        continue;
      }
      for (std::size_t operand = 0U; operand < validator.operands.size(); ++operand) {
        const auto threshold_id =
            cursor < block.instructions.size() ? block.instructions[cursor] : InstructionId{};
        const auto* threshold = consume(threshold_id, ArgumentOperationKind::threshold, function,
                                        flow, cursor, location);
        if (threshold == nullptr) continue;
        const auto& load = program_.instructions[threshold_id.value()];
        const auto& expected = validator.operands[operand];
        if (!plain(*threshold) || load.result != instruction.operands[operand + 1U] ||
            !load.result.valid() || load.origin != validator.source_call ||
            !load.operands.empty() || load.callee.valid() || load.intrinsic != IntrinsicId::none)
          fail(location, "threshold has a corrupt output-owned SSA definition");
        if (expected.kind == ArgumentValidatorOperandKind::numeric_literal) {
          const auto* shape = mir::shape(program_, load.shape);
          if (load.opcode != Opcode::literal || threshold->literal != expected.numeric_literal ||
              load.storage.valid() || value_type(program_, load.type) != ValueType::real ||
              numeric_type(program_, load.type) != real_numeric_type || shape == nullptr ||
              shape->dynamic_rank || !shape->extents.empty() || !accesses_are(threshold_id, {}))
            fail(location, "literal threshold lost its binary64 value or scalar shape");
        } else if (expected.kind == ArgumentValidatorOperandKind::input_argument) {
          const auto ordinal = expected.input_ordinal;
          if (ordinal >= owner.parameter_symbols.size() ||
              ordinal >= function.parameter_types.size() ||
              ordinal >= function.parameter_shapes.size() ||
              !valid(load.storage, program_.storages) || load.opcode != Opcode::identifier ||
              !threshold->literal.empty() ||
              program_.storages[load.storage.value()].symbol != owner.parameter_symbols[ordinal] ||
              load.type != function.parameter_types[ordinal] ||
              load.shape != function.parameter_shapes[ordinal] ||
              !accesses_are(threshold_id, {access(load.storage, MemoryAccessMode::read, true)}) ||
              (!function.argument_entries.empty() &&
               (ordinal >= function.argument_entries.size() ||
                load.storage != function.argument_entries[ordinal].storage)))
            fail(location,
                 "input threshold does not read the current initialized formal workspace");
        } else {
          fail(location, "output validator has an unknown threshold source");
        }
      }
      const auto* call =
          consume(id, ArgumentOperationKind::validation, function, flow, cursor, location);
      if (call == nullptr || !(call->validator == validator) ||
          call->class_constraint != ArgumentClassConstraint::none || call->dimensions_declared ||
          !call->dimensions.empty() || call->rank != 0U || !call->literal.empty() ||
          instruction.opcode != Opcode::argument_validate ||
          instruction.origin != validator.source_call || instruction.storage != flow.storage ||
          instruction.result.valid() || instruction.type.valid() || instruction.shape.valid() ||
          instruction.callee.valid() || instruction.intrinsic != IntrinsicId::none ||
          !accesses_are(id, {access(flow.storage, MemoryAccessMode::read, false)}))
        fail(location, "validator operation disagrees with its source call or memory access");
    }
    if (cursor != block.instructions.size())
      fail(location, "unowned operations appear inside an output stage");
  }

  void verify_terminal(const Function& function, const std::vector<ValueId>& results,
                       const std::vector<MemoryAccess>& reads, const SourceLocation location) {
    const auto& exit = function.argument_exit;
    const auto& block = program_.blocks[exit.continuation.value()];
    if (block.exception_handler.valid() || protected_[exit.continuation.value()] ||
        !block.arguments.empty() || block.terminator.kind != TerminatorKind::return_value ||
        block.terminator.operands != std::vector<ValueId>{exit.returned} ||
        !block.terminator.successors.empty())
      fail(location, "terminal return is not an isolated output boundary");
    if (results.size() == 1U) {
      if (exit.aggregation.valid() || !block.instructions.empty() ||
          exit.returned != results.front())
        fail(location, "scalar return is not the initialized converted output");
      return;
    }
    if (!valid(exit.aggregation, program_.instructions) ||
        block.instructions != std::vector<InstructionId>{exit.aggregation}) {
      fail(location, "multiple outputs have no unique resident aggregation");
      return;
    }
    const auto& aggregate = program_.instructions[exit.aggregation.value()];
    const auto* type =
        valid(aggregate.type, program_.types) ? &program_.types[aggregate.type.value()] : nullptr;
    const auto* shape = mir::shape(program_, aggregate.shape);
    if (aggregate.opcode != Opcode::aggregate || aggregate.origin != function.origin ||
        aggregate.result != exit.returned || aggregate.operands != results ||
        aggregate.storage.valid() || aggregate.callee.valid() ||
        aggregate.intrinsic != IntrinsicId::none || type == nullptr ||
        type->kind != TypeKind::tuple || type->elements != function.result_types ||
        shape == nullptr || shape->dynamic_rank || !shape->extents.empty() ||
        !accesses_are(exit.aggregation, reads))
      fail(location, "tuple return lost normalized result order, types or storage reads");
  }

  const Program& program_;
  std::vector<Diagnostic>& diagnostics_;
  std::string_view stage_;
  std::vector<bool> used_;
  std::vector<bool> storages_;
  std::vector<BlockId> blocks_;
  std::vector<std::vector<BlockId>> incoming_;
  std::vector<MirFunctionId> block_owners_;
  std::vector<bool> protected_;
  std::unordered_map<HirNodeId, const Statement*> owners_;
};
}  // namespace

void verify_argument_outputs(const Program& program, std::vector<Diagnostic>& diagnostics,
                             const std::string_view stage) {
  const bool flows =
      std::any_of(program.functions.begin(), program.functions.end(), [](const auto& function) {
        return !(function.argument_exit == ArgumentExitFlow{}) ||
               !function.argument_outputs.empty();
      });
  const bool declarations =
      std::any_of(program.statements.begin(), program.statements.end(), [](const auto& node) {
        return std::any_of(
            node.argument_validations.begin(), node.argument_validations.end(),
            [](const auto& plan) { return plan.direction == ArgumentDirection::output; });
      });
  const bool operations = std::any_of(
      program.argument_operations.begin(), program.argument_operations.end(),
      [](const auto& operation) { return operation.direction == ArgumentDirection::output; });
  if (!flows && !declarations && !operations) return;
  ExitVerifier(program, diagnostics, stage).verify();
}

}  // namespace mpf::detail::mir
