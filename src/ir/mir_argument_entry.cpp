#include "mir_argument_entry.hpp"

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
                         "invalid MIR argument-entry sequence at '" + std::string(stage) +
                             "': " + std::string(message),
                         location});
}
}  // namespace

const ArgumentOperation* argument_operation(const Program& program,
                                            const InstructionId instruction) noexcept {
  const auto* row = attributes(program, instruction);
  if (row == nullptr || !valid(row->argument_operation, program.argument_operations))
    return nullptr;
  const auto& result = program.argument_operations[row->argument_operation.value()];
  return result.instruction == instruction ? &result : nullptr;
}

void verify_argument_entries(const Program& program, std::vector<Diagnostic>& diagnostics,
                             const std::string_view stage) {
  bool active = !program.argument_operations.empty();
  for (const auto& statement : program.statements) {
    if (statement.kind != StatementKind::function) continue;
    active = active || std::any_of(statement.argument_validations.begin(),
                                   statement.argument_validations.end(), [](const auto& plan) {
                                     return plan.direction == ArgumentDirection::input;
                                   });
  }
  for (const auto& function : program.functions)
    active = active || !function.argument_entries.empty() ||
             !function.raw_parameter_types.empty() || !function.raw_parameter_shapes.empty();
  if (!active) return;
  for (const auto& operation : program.argument_operations)
    if (operation.direction != ArgumentDirection::input &&
        operation.direction != ArgumentDirection::output)
      fail(diagnostics, {1U, 1U}, stage, "argument operation has an invalid direction");
  if (!program.argument_operations.empty()) {
    const auto& sentinel = program.argument_operations.front();
    if (sentinel.instruction.valid() || sentinel.owner.valid() || sentinel.parameter != 0U ||
        sentinel.kind != ArgumentOperationKind::normalization ||
        sentinel.direction != ArgumentDirection::input ||
        sentinel.class_constraint != ArgumentClassConstraint::none ||
        sentinel.dimensions_declared || !sentinel.dimensions.empty() || sentinel.rank != 0U ||
        !(sentinel.validator == ArgumentValidatorPlan{}) || !sentinel.literal.empty())
      fail(diagnostics, {1U, 1U}, stage, "operation sentinel is not empty");
  }
  for (const auto& instruction : program.instructions) {
    const auto* row = attributes(program, instruction.id);
    if (row != nullptr && row->argument_operation.valid() &&
        argument_operation(program, instruction.id) == nullptr)
      fail(diagnostics, instruction.location, stage,
           "operation attribute has no matching resident instruction");
  }
  std::unordered_map<HirNodeId, const Statement*> owners;
  for (const auto& statement : program.statements)
    if (statement.kind == StatementKind::function) owners.emplace(statement.origin, &statement);
  std::vector<bool> used(program.argument_operations.size(), false);
  std::vector<bool> formal_storages(program.storages.size(), false);
  std::vector<BlockId> instruction_blocks(program.instructions.size());
  std::vector<std::vector<BlockId>> incoming(program.blocks.size());
  for (const auto& block : program.blocks) {
    for (const auto instruction : block.instructions)
      if (valid(instruction, instruction_blocks))
        instruction_blocks[instruction.value()] = block.id;
    for (const auto successor : block.terminator.successors)
      if (valid(successor, incoming)) incoming[successor.value()].push_back(block.id);
    if (valid(block.exception_handler, incoming))
      incoming[block.exception_handler.value()].push_back(block.id);
  }
  for (const auto& function : program.functions) {
    const auto owner = owners.find(function.origin);
    std::vector<const ArgumentValidationPlan*> declarations;
    if (owner != owners.end())
      for (const auto& plan : owner->second->argument_validations)
        if (plan.direction == ArgumentDirection::input) declarations.push_back(&plan);
    if (declarations.empty()) {
      if (!function.argument_entries.empty() || !function.raw_parameter_types.empty() ||
          !function.raw_parameter_shapes.empty())
        fail(diagnostics, {1U, 1U}, stage, "entry sequence has no input declaration owner");
      continue;
    }
    const auto& statement = *owner->second;
    const SourceLocation location{statement.line, 1U};
    if (program.source_language != SourceLanguage::matlab ||
        declarations.size() != statement.parameters.size() ||
        function.argument_entries.size() != declarations.size() ||
        function.raw_parameter_types.size() != declarations.size() ||
        function.raw_parameter_shapes.size() != declarations.size() ||
        !valid(function.entry, program.blocks) ||
        program.blocks[function.entry.value()].arguments.size() !=
            declarations.size() + (function.invocation_frame.active() ? 1U : 0U)) {
      fail(diagnostics, location, stage, "raw/formal/declaration inventories disagree");
      continue;
    }
    const auto& entry = program.blocks[function.entry.value()];
    for (std::size_t parameter = 0U; parameter < declarations.size(); ++parameter) {
      const auto& flow = function.argument_entries[parameter];
      const auto& plan = *declarations[parameter];
      const auto& raw = entry.arguments[parameter];
      if (flow.parameter != parameter || plan.ordinal != parameter ||
          flow.raw_storage != raw.storage || !valid(flow.storage, program.storages) ||
          !valid(flow.raw_storage, program.storages) || flow.storage == flow.raw_storage ||
          !valid(flow.block, program.blocks) || !valid(flow.continuation, program.blocks) ||
          !valid(flow.normalization, program.instructions) ||
          !valid(flow.initialization, program.instructions) ||
          parameter >= function.parameter_types.size() ||
          parameter >= function.parameter_shapes.size() ||
          parameter >= statement.parameter_symbols.size() ||
          flow.validators.size() != plan.validators.size()) {
        fail(diagnostics, location, stage, "entry flow has invalid identities or validator arity");
        continue;
      }
      const auto& source = program.storages[flow.raw_storage.value()];
      const auto& formal = program.storages[flow.storage.value()];
      const auto access = [&](const StorageId storage, const MemoryAccessMode mode) {
        if (!valid(storage, program.storages)) return MemoryAccess{};
        const auto* data = mir::shape(program, program.storages[storage.value()].shape);
        return MemoryAccess{storage, storage,
                            data == nullptr ? StorageRegion{} : full_storage_region(data->extents),
                            mode};
      };
      const auto accesses_are = [&](const InstructionId id,
                                    const std::vector<MemoryAccess>& expected) {
        const auto* row = attributes(program, id);
        return row != nullptr && row->memory_accesses == expected;
      };
      const auto* shape = mir::shape(program, raw.shape);
      if (formal_storages[flow.storage.value()] || source.kind != StorageKind::parameter ||
          source.type != raw.type || source.shape != raw.shape ||
          raw.type != function.raw_parameter_types[parameter] ||
          raw.shape != function.raw_parameter_shapes[parameter] ||
          value_type(program, raw.type) != ValueType::unknown || shape == nullptr ||
          !shape->dynamic_rank || !shape->extents.empty() || formal.kind != StorageKind::local ||
          formal.optional || !formal.writable || formal.lifetime != StorageLifetime::function ||
          formal.base.valid() || formal.view != StorageViewKind::none ||
          formal.type != function.parameter_types[parameter] ||
          formal.shape != function.parameter_shapes[parameter] ||
          formal.symbol != statement.parameter_symbols[parameter] ||
          formal.symbol != source.symbol || formal.origin != function.origin ||
          source.origin != function.origin) {
        fail(diagnostics, location, stage, "raw input is conflated with normalized formal storage");
      }
      formal_storages[flow.storage.value()] = true;
      const auto default_flow =
          std::find_if(function.parameter_defaults.begin(), function.parameter_defaults.end(),
                       [&](const auto& item) { return item.parameter == parameter; });
      const auto start =
          parameter == 0U ? function.entry : function.argument_entries[parameter - 1U].continuation;
      const bool defaulted = default_flow != function.parameter_defaults.end();
      const auto expected_block = defaulted ? default_flow->merge_block : start;
      const auto expected_value = defaulted ? default_flow->result : raw.value;
      const auto& block = program.blocks[flow.block.value()];
      if (flow.block != expected_block || flow.selected != expected_value ||
          flow.block == flow.continuation || block.exception_handler.valid() ||
          block.terminator.kind != TerminatorKind::branch ||
          block.terminator.successors != std::vector<BlockId>{flow.continuation} ||
          incoming[flow.continuation.value()] != std::vector<BlockId>{flow.block})
        fail(diagnostics, location, stage,
             "entry operations are not sequenced before continuation");
      if (defaulted) {
        auto actual = incoming[flow.block.value()];
        auto expected =
            std::vector<BlockId>{default_flow->present_block, default_flow->default_exit};
        std::sort(actual.begin(), actual.end());
        std::sort(expected.begin(), expected.end());
        if (actual != expected || default_flow->test_block != start)
          fail(diagnostics, location, stage, "default merge can bypass its declared entry stage");
      } else if (parameter == 0U
                     ? !incoming[flow.block.value()].empty()
                     : incoming[flow.block.value()] !=
                           std::vector<BlockId>{function.argument_entries[parameter - 1U].block}) {
        fail(diagnostics, location, stage, "raw parameter is normalized from an unauthorized path");
      }
      std::size_t cursor = 0U;
      const auto consume = [&](const InstructionId id, const ArgumentOperationKind kind) {
        const auto* operation = argument_operation(program, id);
        if (!valid(id, program.instructions) || operation == nullptr || operation->kind != kind ||
            operation->direction != ArgumentDirection::input ||
            operation->owner != function.origin || operation->parameter != parameter ||
            instruction_blocks[id.value()] != flow.block || cursor >= block.instructions.size() ||
            block.instructions[cursor++] != id) {
          fail(diagnostics, location, stage,
               "resident operation identity, owner or order is invalid");
          return static_cast<const ArgumentOperation*>(nullptr);
        }
        const auto row = attributes(program, id)->argument_operation;
        if (used[row.value()])
          fail(diagnostics, location, stage, "operation is owned more than once");
        used[row.value()] = true;
        return operation;
      };
      const auto* normalization = consume(flow.normalization, ArgumentOperationKind::normalization);
      const auto& normalize = program.instructions[flow.normalization.value()];
      const auto& initialize = program.instructions[flow.initialization.value()];
      if (normalization == nullptr || normalize.opcode != Opcode::argument_normalize ||
          normalize.origin != function.origin || normalize.storage != flow.storage ||
          normalize.type != formal.type || normalize.shape != formal.shape ||
          !normalize.result.valid() || normalize.operands != std::vector<ValueId>{flow.selected} ||
          normalize.callee.valid() || normalize.intrinsic != IntrinsicId::none ||
          normalization->class_constraint != plan.class_constraint ||
          normalization->dimensions_declared != plan.dimensions_declared ||
          normalization->dimensions != plan.dimensions ||
          normalization->rank != plan.validated_rank || !normalization->literal.empty() ||
          !(normalization->validator == ArgumentValidatorPlan{}) ||
          !accesses_are(flow.normalization, {access(flow.raw_storage, MemoryAccessMode::read)}))
        fail(diagnostics, location, stage,
             "normalization does not implement the declared type and shape");
      if (cursor >= block.instructions.size() ||
          block.instructions[cursor++] != flow.initialization ||
          initialize.opcode != Opcode::store || initialize.storage != flow.storage ||
          initialize.type != formal.type || initialize.shape != formal.shape ||
          initialize.origin != function.origin || initialize.callee.valid() ||
          initialize.intrinsic != IntrinsicId::none ||
          initialize.operands != std::vector<ValueId>{normalize.result} ||
          initialize.result != flow.result || !flow.result.valid() ||
          !accesses_are(flow.initialization, {access(flow.storage, MemoryAccessMode::write)}))
        fail(diagnostics, location, stage, "normalized value is not published before validation");
      for (std::size_t index = 0U; index < plan.validators.size(); ++index) {
        const auto instruction = flow.validators[index];
        if (!valid(instruction, program.instructions)) {
          fail(diagnostics, location, stage, "validator has no resident instruction");
          continue;
        }
        const auto& validation = program.instructions[instruction.value()];
        const auto& expected = plan.validators[index];
        std::vector<MemoryAccess> reads{access(flow.storage, MemoryAccessMode::read)};
        if (validation.operands.size() != expected.operands.size() + 1U ||
            validation.operands.front() != flow.result) {
          fail(diagnostics, location, stage,
               "validator operands do not use the initialized formal");
          continue;
        }
        for (std::size_t operand_index = 0U; operand_index < expected.operands.size();
             ++operand_index) {
          const auto value = validation.operands[operand_index + 1U];
          const auto& operand = expected.operands[operand_index];
          if (operand.kind == ArgumentValidatorOperandKind::input_argument) {
            if (operand.input_ordinal >= parameter ||
                value != function.argument_entries[operand.input_ordinal].result ||
                !valid(function.argument_entries[operand.input_ordinal].storage, program.storages))
              fail(diagnostics, location, stage, "threshold is not a previously normalized formal");
            else {
              const auto storage = function.argument_entries[operand.input_ordinal].storage;
              if (std::none_of(reads.begin(), reads.end(),
                               [&](const auto& item) { return item.storage == storage; }))
                reads.push_back(access(storage, MemoryAccessMode::read));
            }
          } else if (operand.kind == ArgumentValidatorOperandKind::numeric_literal) {
            const auto literal_id =
                cursor < block.instructions.size() ? block.instructions[cursor] : InstructionId{};
            const auto* literal = consume(literal_id, ArgumentOperationKind::threshold);
            if (literal == nullptr) continue;
            const auto& literal_instruction = program.instructions[literal_id.value()];
            const auto* literal_shape = mir::shape(program, literal_instruction.shape);
            if (literal->literal != operand.numeric_literal ||
                literal_instruction.opcode != Opcode::literal ||
                literal_instruction.result != value ||
                literal_instruction.origin != expected.source_call ||
                !literal_instruction.result.valid() || literal_instruction.callee.valid() ||
                literal_instruction.intrinsic != IntrinsicId::none ||
                value_type(program, literal_instruction.type) != ValueType::real ||
                numeric_type(program, literal_instruction.type) != real_numeric_type ||
                !literal_instruction.operands.empty() || literal_instruction.storage.valid() ||
                literal->class_constraint != ArgumentClassConstraint::none ||
                literal->dimensions_declared || !literal->dimensions.empty() ||
                literal->rank != 0U || !(literal->validator == ArgumentValidatorPlan{}) ||
                literal_shape == nullptr || literal_shape->dynamic_rank ||
                !literal_shape->extents.empty() || !accesses_are(literal_id, {}))
              fail(diagnostics, location, stage,
                   "threshold literal has a corrupt typed definition");
          } else {
            fail(diagnostics, location, stage, "validator operand has an unknown source form");
          }
        }
        const auto* call = consume(instruction, ArgumentOperationKind::validation);
        if (call == nullptr || !(call->validator == expected) ||
            validation.opcode != Opcode::argument_validate ||
            validation.origin != expected.source_call || validation.storage != flow.storage ||
            validation.result.valid() || validation.callee.valid() || validation.type.valid() ||
            validation.shape.valid() || validation.intrinsic != IntrinsicId::none ||
            call->class_constraint != ArgumentClassConstraint::none || call->dimensions_declared ||
            !call->dimensions.empty() || call->rank != 0U || !call->literal.empty() ||
            !accesses_are(instruction, reads))
          fail(diagnostics, location, stage,
               "validator operation is inconsistent with its source binding");
      }
      if (cursor != block.instructions.size())
        fail(diagnostics, location, stage, "unowned instructions occur in an entry stage");
    }
    if (!valid(statement.instruction, instruction_blocks) ||
        instruction_blocks[statement.instruction.value()] !=
            function.argument_entries.back().continuation)
      fail(diagnostics, location, stage, "function body begins before validation completes");
  }
  for (std::size_t index = 1U; index < used.size(); ++index)
    if (!used[index] && program.argument_operations[index].direction == ArgumentDirection::input)
      fail(diagnostics, {1U, 1U}, stage, "orphan input argument operation");
}

}  // namespace mpf::detail::mir
