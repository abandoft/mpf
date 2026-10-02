#include "mir_argument_exit.hpp"

#include <utility>

namespace mpf::detail::mir {
namespace {
class ExitBuilder final {
 public:
  explicit ExitBuilder(ArgumentExitContext context)
      : context_(std::move(context)), block_(context_.function.argument_exit.merge) {}

  void lower() {
    auto& function = context_.function;
    std::vector<ValueId> results(function.result_types.size());
    std::vector<MemoryAccess> result_reads;
    function.argument_outputs.reserve(results.size());
    function.blocks.reserve(function.blocks.size() + results.size());
    context_.program.blocks.reserve(context_.program.blocks.size() + results.size());
    if (results.size() > 1U) result_reads.reserve(results.size());
    for (const auto& plan : context_.owner.argument_validations) {
      if (plan.direction != ArgumentDirection::output) continue;
      auto flow = lower_output(plan);
      results.at(flow.output) = flow.result;
      if (results.size() > 1U)
        result_reads.push_back(access(flow.storage, MemoryAccessMode::read, false));
      function.argument_outputs.push_back(std::move(flow));
    }
    function.argument_exit.continuation = block_;
    auto returned = results.front();
    if (results.size() > 1U) {
      Instruction aggregate;
      aggregate.id = context_.instructions.next();
      function.argument_exit.aggregation = aggregate.id;
      aggregate.opcode = Opcode::aggregate;
      aggregate.origin = function.origin;
      aggregate.location = {context_.owner.line, 1U};
      aggregate.type = context_.tuple_type;
      aggregate.shape = context_.scalar_shape;
      aggregate.result = context_.values.next();
      aggregate.operands = std::move(results);
      returned = aggregate.result;
      append(std::move(aggregate), std::move(result_reads));
    }
    function.argument_exit.returned = returned;
    auto& terminator = context_.program.blocks.at(block_.value()).terminator;
    terminator.kind = TerminatorKind::return_value;
    terminator.origin = function.origin;
    terminator.operands = {returned};
  }

 private:
  MemoryAccess access(const StorageId storage, const MemoryAccessMode mode,
                      const bool workspace) const {
    auto root = storage;
    const auto& program = context_.program;
    for (std::size_t count = 0U; count < program.storages.size(); ++count) {
      const auto base = program.storages.at(root.value()).base;
      if (!base.valid()) break;
      root = base;
    }
    const auto* data = mir::shape(program, program.storages.at(root.value()).shape);
    // A workspace can have changed shape while the function body was executing.
    const auto region =
        workspace || data == nullptr ? StorageRegion{} : full_storage_region(data->extents);
    return {storage, root, region, mode};
  }

  void append(Instruction instruction, std::vector<MemoryAccess> accesses = {},
              ArgumentOperationId operation = {}) {
    auto& program = context_.program;
    const auto id = instruction.id;
    program.blocks.at(block_.value()).instructions.push_back(id);
    program.attributes.instructions.resize(static_cast<std::size_t>(id.value()) + 1U);
    program.attributes.instructions[id.value()] = {id, operation, std::move(accesses)};
    program.instructions.push_back(std::move(instruction));
  }

  void append_operation(Instruction instruction, ArgumentOperation operation,
                        std::vector<MemoryAccess> accesses = {}) {
    auto& program = context_.program;
    if (program.argument_operations.empty()) program.argument_operations.emplace_back();
    operation.instruction = instruction.id;
    operation.direction = ArgumentDirection::output;
    const ArgumentOperationId id{
        static_cast<ArgumentOperationId::value_type>(program.argument_operations.size())};
    program.argument_operations.push_back(std::move(operation));
    append(std::move(instruction), std::move(accesses), id);
  }

  ArgumentOutputFlow lower_output(const ArgumentValidationPlan& plan) {
    auto& program = context_.program;
    auto& function = context_.function;
    const auto source = context_.outputs.at(plan.ordinal);
    ArgumentOutputFlow flow;
    flow.validators.reserve(plan.validators.size());
    flow.output = plan.ordinal;
    flow.block = block_;
    flow.source_storage = source.storage;
    Instruction select;
    select.id = context_.instructions.next();
    flow.selection = select.id;
    select.opcode = Opcode::identifier;
    select.origin = function.origin;
    select.location = {plan.line, 1U};
    select.storage = source.storage;
    select.type = context_.workspace_type;
    select.shape = context_.workspace_shape;
    select.result = context_.values.next();
    flow.selected = select.result;
    ArgumentOperation selection;
    selection.owner = function.origin;
    selection.parameter = plan.ordinal;
    selection.kind = ArgumentOperationKind::selection;
    append_operation(std::move(select), std::move(selection),
                     {access(source.storage, MemoryAccessMode::read, true)});
    flow.storage = StorageId{static_cast<StorageId::value_type>(program.storages.size())};
    program.storages.push_back({"$output" + std::to_string(plan.ordinal),
                                {},
                                function.origin,
                                function.result_types.at(plan.ordinal),
                                function.result_shapes.at(plan.ordinal),
                                true,
                                false,
                                StorageKind::temporary,
                                StorageLifetime::function,
                                {},
                                StorageViewKind::none,
                                ParameterIntent::none});
    Instruction normalize;
    normalize.id = context_.instructions.next();
    flow.normalization = normalize.id;
    normalize.opcode = Opcode::argument_normalize;
    normalize.origin = function.origin;
    normalize.location = {plan.line, 1U};
    normalize.storage = flow.storage;
    normalize.type = function.result_types.at(plan.ordinal);
    normalize.shape = function.result_shapes.at(plan.ordinal);
    normalize.result = context_.values.next();
    normalize.operands = {flow.selected};
    const auto normalized = normalize.result;
    ArgumentOperation operation;
    operation.owner = function.origin;
    operation.parameter = plan.ordinal;
    operation.class_constraint = plan.class_constraint;
    operation.dimensions_declared = plan.dimensions_declared;
    operation.dimensions = plan.dimensions;
    operation.rank = plan.validated_rank;
    append_operation(std::move(normalize), std::move(operation),
                     {access(source.storage, MemoryAccessMode::read, true)});

    Instruction initialize;
    initialize.id = context_.instructions.next();
    flow.initialization = initialize.id;
    initialize.opcode = Opcode::store;
    initialize.origin = function.origin;
    initialize.location = {plan.line, 1U};
    initialize.storage = flow.storage;
    initialize.type = function.result_types.at(plan.ordinal);
    initialize.shape = function.result_shapes.at(plan.ordinal);
    initialize.result = context_.values.next();
    initialize.operands = {normalized};
    flow.result = initialize.result;
    append(std::move(initialize), {access(flow.storage, MemoryAccessMode::write, false)});

    for (const auto& validator : plan.validators) {
      std::vector<ValueId> operands{flow.result};
      std::vector<MemoryAccess> reads{access(flow.storage, MemoryAccessMode::read, false)};
      for (const auto& operand : validator.operands) {
        if (operand.kind == ArgumentValidatorOperandKind::numeric_literal) {
          Instruction literal;
          literal.id = context_.instructions.next();
          literal.opcode = Opcode::literal;
          literal.origin = validator.source_call;
          literal.location = {plan.line, 1U};
          literal.type = context_.literal_type;
          literal.shape = context_.scalar_shape;
          literal.result = context_.values.next();
          operands.push_back(literal.result);
          ArgumentOperation threshold;
          threshold.owner = function.origin;
          threshold.parameter = plan.ordinal;
          threshold.kind = ArgumentOperationKind::threshold;
          threshold.literal = operand.numeric_literal;
          append_operation(std::move(literal), std::move(threshold));
        } else {
          const auto input = context_.inputs.at(operand.input_ordinal);
          Instruction load;
          load.id = context_.instructions.next();
          load.opcode = Opcode::identifier;
          load.origin = validator.source_call;
          load.location = {plan.line, 1U};
          load.storage = input.storage;
          load.type = function.parameter_types.at(operand.input_ordinal);
          load.shape = function.parameter_shapes.at(operand.input_ordinal);
          load.result = context_.values.next();
          operands.push_back(load.result);
          ArgumentOperation threshold;
          threshold.owner = function.origin;
          threshold.parameter = plan.ordinal;
          threshold.kind = ArgumentOperationKind::threshold;
          append_operation(std::move(load), std::move(threshold),
                           {access(input.storage, MemoryAccessMode::read, true)});
        }
      }
      Instruction validate;
      validate.id = context_.instructions.next();
      flow.validators.push_back(validate.id);
      validate.opcode = Opcode::argument_validate;
      validate.origin = validator.source_call;
      validate.location = {plan.line, 1U};
      validate.storage = flow.storage;
      validate.operands = std::move(operands);
      ArgumentOperation call;
      call.owner = function.origin;
      call.parameter = plan.ordinal;
      call.kind = ArgumentOperationKind::validation;
      call.validator = validator;
      append_operation(std::move(validate), std::move(call), std::move(reads));
    }

    flow.continuation = context_.blocks.next();
    BasicBlock continuation;
    continuation.id = flow.continuation;
    program.blocks.push_back(std::move(continuation));
    function.blocks.push_back(flow.continuation);
    auto& terminator = program.blocks.at(block_.value()).terminator;
    terminator.kind = TerminatorKind::branch;
    terminator.origin = function.origin;
    terminator.successors = {flow.continuation};
    terminator.successor_arguments = {{}};
    block_ = flow.continuation;
    return flow;
  }

  ArgumentExitContext context_;
  BlockId block_{};
};
}  // namespace

void lower_argument_exit(ArgumentExitContext context) {
  ExitBuilder(std::move(context)).lower();
}

}  // namespace mpf::detail::mir
