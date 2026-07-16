#include <algorithm>
#include <string>
#include <unordered_map>
#include <utility>

#include "mir.hpp"
#include "mir_opcode.hpp"

namespace mpf::detail::mir {
namespace {

bool transfer_matches_intent(const ArgumentTransfer transfer,
                             const ParameterIntent intent) noexcept {
  switch (transfer) {
    case ArgumentTransfer::value:
      return intent == ParameterIntent::none || intent == ParameterIntent::in;
    case ArgumentTransfer::read_only_borrow:
    case ArgumentTransfer::optional_forward_in: return intent == ParameterIntent::in;
    case ArgumentTransfer::mutable_borrow_out:
    case ArgumentTransfer::copy_out:
    case ArgumentTransfer::optional_forward_out: return intent == ParameterIntent::out;
    case ArgumentTransfer::mutable_borrow_inout:
    case ArgumentTransfer::copy_in_out:
    case ArgumentTransfer::optional_forward_inout: return intent == ParameterIntent::inout;
    case ArgumentTransfer::omitted: return true;
  }
  return false;
}

void add_error(std::vector<Diagnostic>& diagnostics, const SourceLocation location,
               const std::string_view stage, std::string message) {
  diagnostics.push_back({DiagnosticSeverity::error, "MPF0006",
                         "invalid MIR at '" + std::string(stage) + "': " + std::move(message),
                         location});
}

template <typename Id, typename Item>
bool valid_index(const Id id, const std::vector<Item>& items) noexcept {
  return id.valid() && static_cast<std::size_t>(id.value()) < items.size();
}

const TypeData* type_data(const Program& program, const TypeId id) noexcept {
  return valid_index(id, program.types) ? &program.types[id.value()] : nullptr;
}

TypeId logical_type(const Program& program, const TypeId id) noexcept {
  const auto* type = type_data(program, id);
  return type != nullptr && type->kind == TypeKind::reference ? type->referent : id;
}

bool compatible_type(const Program& program, const TypeId actual, const TypeId expected) noexcept {
  const auto actual_logical = logical_type(program, actual);
  const auto expected_logical = logical_type(program, expected);
  if (actual_logical == expected_logical) return true;
  const auto* actual_data = type_data(program, actual_logical);
  const auto* expected_data = type_data(program, expected_logical);
  if (actual_data == nullptr || expected_data == nullptr) return false;
  if (actual_data->value_type == ValueType::unknown ||
      expected_data->value_type == ValueType::unknown) {
    return true;
  }
  if (actual_data->kind != expected_data->kind ||
      actual_data->value_type != expected_data->value_type) {
    return false;
  }
  if (actual_data->kind == TypeKind::sequence) {
    return actual_data->element_type == expected_data->element_type ||
           actual_data->element_type == ValueType::unknown ||
           expected_data->element_type == ValueType::unknown;
  }
  if (actual_data->kind == TypeKind::tuple) {
    if (actual_data->elements.size() != expected_data->elements.size()) return false;
    for (std::size_t index = 0; index < actual_data->elements.size(); ++index) {
      if (!compatible_type(program, actual_data->elements[index], expected_data->elements[index])) {
        return false;
      }
    }
  }
  return true;
}

bool compatible_shape(const Program& program, const ShapeId actual, const ShapeId expected,
                      const TypeId expected_type) noexcept {
  if (actual == expected) return true;
  if (!valid_index(actual, program.shapes) || !valid_index(expected, program.shapes)) return false;
  const auto& actual_data = program.shapes[actual.value()];
  const auto& expected_data = program.shapes[expected.value()];
  if (expected_data.dynamic_rank) return true;
  if (expected_data.extents.empty() && (value_type(program, expected_type) == ValueType::list ||
                                        value_type(program, expected_type) == ValueType::unknown)) {
    return true;
  }
  if (actual_data.layout != expected_data.layout ||
      actual_data.extents.size() != expected_data.extents.size()) {
    return false;
  }
  for (std::size_t index = 0; index < actual_data.extents.size(); ++index) {
    if (actual_data.extents[index] != expected_data.extents[index] &&
        actual_data.extents[index] != dynamic_extent &&
        expected_data.extents[index] != dynamic_extent) {
      return false;
    }
  }
  return true;
}

void verify_value_metadata(const ValueMetadata& metadata, const Program& program,
                           std::vector<Diagnostic>& diagnostics, const SourceLocation location,
                           const std::string_view stage) {
  if (!valid_index(metadata.type, program.types) || !valid_index(metadata.shape, program.shapes)) {
    add_error(diagnostics, location, stage,
              "value metadata references an invalid strong type or shape ID");
  }
  if (!metadata.sequence && (!metadata.elements.empty() || metadata.list_sequence)) {
    add_error(diagnostics, location, stage,
              "scalar value metadata unexpectedly carries sequence payload");
  }
  for (const auto& element : metadata.elements) {
    verify_value_metadata(element, program, diagnostics, location, stage);
  }
}

void verify_assignment_pattern(const AssignmentPattern& pattern, const Program& program,
                               std::vector<Diagnostic>& diagnostics, const std::string_view stage) {
  if (!pattern.valid()) return;
  if (!valid_index(pattern.type, program.types) || !valid_index(pattern.shape, program.shapes) ||
      !valid_index(pattern.previous_type, program.types)) {
    add_error(diagnostics, pattern.location, stage,
              "assignment pattern references an invalid strong type or shape ID");
  }
  if ((pattern.kind == AssignmentPatternKind::name ||
       pattern.kind == AssignmentPatternKind::starred_name) == pattern.name.empty()) {
    add_error(diagnostics, pattern.location, stage,
              "assignment pattern leaf/name representation is inconsistent");
  }
  for (const auto& child : pattern.children) {
    verify_assignment_pattern(child, program, diagnostics, stage);
  }
}

struct ExpressionVerificationIndex {
  std::vector<const CallSite*> calls_by_instruction;
  std::vector<const Instruction*> instructions_by_value;
  std::vector<const BlockArgument*> block_arguments_by_value;
};

ExpressionVerificationIndex build_expression_verification_index(const Program& program) {
  ExpressionVerificationIndex result;
  result.calls_by_instruction.resize(program.instructions.size());
  for (const auto& call : program.calls) {
    if (valid_index(call.instruction, result.calls_by_instruction)) {
      result.calls_by_instruction[call.instruction.value()] = &call;
    }
  }
  std::size_t maximum_value = 0;
  for (std::size_t index = 1; index < program.instructions.size(); ++index) {
    maximum_value = std::max(maximum_value,
                             static_cast<std::size_t>(program.instructions[index].result.value()));
  }
  for (std::size_t index = 1; index < program.blocks.size(); ++index) {
    for (const auto& argument : program.blocks[index].arguments) {
      maximum_value = std::max(maximum_value, static_cast<std::size_t>(argument.value.value()));
    }
  }
  result.instructions_by_value.resize(maximum_value + 1U);
  result.block_arguments_by_value.resize(maximum_value + 1U);
  for (std::size_t index = 1; index < program.instructions.size(); ++index) {
    const auto& instruction = program.instructions[index];
    if (instruction.result.valid()) {
      result.instructions_by_value[instruction.result.value()] = &instruction;
    }
  }
  for (std::size_t index = 1; index < program.blocks.size(); ++index) {
    for (const auto& argument : program.blocks[index].arguments) {
      if (argument.value.valid()) {
        result.block_arguments_by_value[argument.value.value()] = &argument;
      }
    }
  }
  return result;
}

void verify_expression(const Expression& expression, const Program& program,
                       const ExpressionVerificationIndex& index,
                       std::vector<Diagnostic>& diagnostics, const std::string_view stage) {
  if (!expression.id.valid() || expression.id.value() >= program.expressions.size() ||
      &program.expressions[expression.id.value()] != &expression) {
    add_error(diagnostics, expression.location, stage,
              "expression arena is not dense or has an invalid identity");
    return;
  }
  if (!expression.valid()) return;
  const auto* expression_attributes = attributes(program, expression.id);
  if (!expression.origin.valid() || !expression.value_id.valid() ||
      !valid_index(expression.type_id, program.types) ||
      !valid_index(expression.shape_id, program.shapes)) {
    add_error(diagnostics, expression.location, stage,
              "typed expression has an invalid origin, value, type, or shape ID");
  }
  if (!valid_index(expression.instruction, program.instructions)) {
    add_error(diagnostics, expression.location, stage,
              "expression has no resident instruction definition");
  } else {
    const auto& instruction = program.instructions[expression.instruction.value()];
    std::vector<ValueId> expected_operands;
    for (const auto child : expression.children) {
      const auto* child_node = mir::expression(program, child);
      if (child_node != nullptr && child_node->value_id.valid()) {
        expected_operands.push_back(child_node->value_id);
      }
    }
    bool operands_match = instruction.operands == expected_operands;
    if (expression_attributes != nullptr && expression_attributes->lazy_cfg) {
      const bool lazy_kind =
          expression.kind == ExpressionKind::conditional ||
          expression.kind == ExpressionKind::comparison_chain ||
          (expression.kind == ExpressionKind::binary &&
           (expression_attributes->spelling == "&&" || expression_attributes->spelling == "||"));
      const auto merged_value =
          instruction.operands.size() == 1U ? instruction.operands.front() : ValueId{};
      const auto* merge_argument =
          merged_value.valid() && merged_value.value() < index.block_arguments_by_value.size()
              ? index.block_arguments_by_value[merged_value.value()]
              : nullptr;
      operands_match = lazy_kind && merge_argument != nullptr &&
                       merge_argument->type == expression.type_id &&
                       merge_argument->shape == expression.shape_id;
    }
    if (!operands_match && expression.kind == ExpressionKind::call &&
        instruction.operands.size() == expected_operands.size()) {
      const auto* call = instruction.id.value() < index.calls_by_instruction.size()
                             ? index.calls_by_instruction[instruction.id.value()]
                             : nullptr;
      operands_match = call != nullptr &&
                       call->arguments.size() + 1U == instruction.operands.size() &&
                       instruction.operands.front() == expected_operands.front();
      for (std::size_t argument = 0; operands_match && argument < call->arguments.size();
           ++argument) {
        const auto actual = instruction.operands[argument + 1U];
        if (actual == expected_operands[argument + 1U]) continue;
        const auto* definition = actual.value() < index.instructions_by_value.size()
                                     ? index.instructions_by_value[actual.value()]
                                     : nullptr;
        operands_match = argument_transfer_copies(call->arguments[argument].transfer) &&
                         definition != nullptr && definition->opcode == Opcode::copy &&
                         definition->origin == expression.origin &&
                         definition->transfer == call->arguments[argument].transfer;
      }
    }
    if (instruction.origin != expression.origin || instruction.result != expression.value_id ||
        instruction.type != expression.type_id || instruction.shape != expression.shape_id ||
        instruction.storage != expression.storage_id ||
        instruction.opcode !=
            expression_opcode(expression.kind, expression_attributes == nullptr
                                                   ? BindingKind::unresolved
                                                   : expression_attributes->binding) ||
        !operands_match) {
      add_error(diagnostics, expression.location, stage,
                "expression arena disagrees with its MIR instruction definition");
    }
  }
  if (expression.storage_id.valid() && !valid_index(expression.storage_id, program.storages)) {
    add_error(diagnostics, expression.location, stage,
              "expression references an invalid storage ID");
  }
  if (expression_attributes == nullptr || expression_attributes->origin != expression.id) {
    add_error(diagnostics, expression.location, stage,
              "expression has no matching operation-attribute row");
    return;
  }
  if (expression_attributes->requested_results == 0U ||
      ((expression_attributes->binding == BindingKind::builtin) !=
       (expression_attributes->intrinsic != IntrinsicId::none))) {
    add_error(diagnostics, expression.location, stage,
              "expression operation attributes have an invalid result/binding contract");
  }
  const auto* expression_type = type_data(program, expression.type_id);
  if (!expression_attributes->tuple_shapes.empty() &&
      (expression_type == nullptr || expression_type->kind != TypeKind::tuple ||
       expression_attributes->tuple_shapes.size() != expression_type->elements.size())) {
    add_error(diagnostics, expression.location, stage,
              "tuple shape attributes disagree with the interned tuple type");
  }
  for (const auto tuple_shape : expression_attributes->tuple_shapes) {
    if (!valid_index(tuple_shape, program.shapes)) {
      add_error(diagnostics, expression.location, stage,
                "tuple attribute references an invalid shape ID");
    }
  }
  for (const auto& metadata : expression_attributes->sequence_elements) {
    verify_value_metadata(metadata, program, diagnostics, expression.location, stage);
  }
  if (expression.kind == ExpressionKind::binary &&
      ((expression_attributes->comparison != ComparisonOperator::none) ==
       !expression_attributes->spelling.empty())) {
    add_error(diagnostics, expression.location, stage,
              "binary expression has an ambiguous operator representation");
  }
  if (expression.kind == ExpressionKind::comparison_chain &&
      (expression.children.size() < 3U ||
       expression_attributes->comparisons.size() + 1U != expression.children.size() ||
       std::any_of(expression_attributes->comparisons.begin(),
                   expression_attributes->comparisons.end(),
                   [](const auto operation) { return operation == ComparisonOperator::none; }))) {
    add_error(diagnostics, expression.location, stage,
              "comparison chain operand/operator count is inconsistent");
  }
  for (const auto child : expression.children) {
    if (child.valid() && !valid_index(child, program.expressions)) {
      add_error(diagnostics, expression.location, stage,
                "expression references an invalid MIR expression child");
    }
  }
}

void verify_statements(const Program& program, std::vector<Diagnostic>& diagnostics,
                       const std::string_view stage) {
  for (std::size_t index = 1; index < program.statements.size(); ++index) {
    const auto& statement = program.statements[index];
    if (!statement.id.valid() || statement.id.value() != index) {
      add_error(diagnostics, {statement.line, 1}, stage,
                "statement arena is not dense or has an invalid identity");
      continue;
    }
    if (!statement.origin.valid()) {
      add_error(diagnostics, {statement.line, 1}, stage, "statement has no HIR origin");
    }
    const auto* statement_attributes = attributes(program, statement.id);
    if (statement_attributes == nullptr || statement_attributes->origin != statement.id) {
      add_error(diagnostics, {statement.line, 1}, stage,
                "statement has no matching operation-attribute row");
    } else {
      if (statement.has_target_pattern != statement_attributes->target_pattern.valid()) {
        add_error(diagnostics, {statement.line, 1}, stage,
                  "statement assignment-pattern flag disagrees with its attribute row");
      }
      if (statement.kind == StatementKind::multi_assignment &&
          statement_attributes->targets.size() != statement.target_names.size()) {
        add_error(diagnostics, {statement.line, 1}, stage,
                  "multi-assignment target attributes have inconsistent arity");
      }
      if (statement.kind == StatementKind::multi_assignment &&
          statement.target_symbols.size() != statement.target_names.size()) {
        add_error(diagnostics, {statement.line, 1}, stage,
                  "multi-assignment target symbols have inconsistent arity");
      }
      if (!valid_index(statement_attributes->previous_type, program.types)) {
        add_error(diagnostics, {statement.line, 1}, stage,
                  "statement attributes reference an invalid previous type");
      }
      verify_assignment_pattern(statement_attributes->target_pattern, program, diagnostics, stage);
      for (const auto& target : statement_attributes->targets) {
        if (!valid_index(target.type, program.types) ||
            !valid_index(target.shape, program.shapes) ||
            !valid_index(target.previous_type, program.types) ||
            (statement.kind == StatementKind::multi_assignment &&
             !valid_index(target.storage, program.storages))) {
          add_error(diagnostics, {statement.line, 1}, stage,
                    "target attributes reference an invalid strong type or shape ID");
        }
      }
      if (statement.kind == StatementKind::multi_assignment &&
          statement_attributes->targets.size() == statement.target_names.size()) {
        for (std::size_t target = 0; target < statement_attributes->targets.size(); ++target) {
          const auto instruction_index =
              static_cast<std::size_t>(statement.instruction.value()) + target;
          const auto* store = instruction_index < program.instructions.size()
                                  ? &program.instructions[instruction_index]
                                  : nullptr;
          if (store == nullptr || store->origin != statement.origin ||
              store->opcode != Opcode::store || store->result_index != target ||
              store->storage != statement_attributes->targets[target].storage) {
            add_error(diagnostics, {statement.line, 1}, stage,
                      "multi-assignment target has no explicit indexed store operation");
          }
        }
      }
    }
    if (!valid_index(statement.instruction, program.instructions)) {
      add_error(diagnostics, {statement.line, 1}, stage,
                "statement has no resident operation instruction");
    } else {
      const auto& instruction = program.instructions[statement.instruction.value()];
      if (instruction.origin != statement.origin ||
          instruction.opcode != statement_opcode(statement.kind, statement.has_expression)) {
        add_error(diagnostics, {statement.line, 1}, stage,
                  "statement arena disagrees with its MIR operation instruction");
      }
    }
    const auto verify_expression_id = [&](const MirExpressionId expression_id) {
      if (expression_id.valid() && !valid_index(expression_id, program.expressions)) {
        add_error(diagnostics, {statement.line, 1}, stage,
                  "statement references an invalid MIR expression");
      }
    };
    verify_expression_id(statement.expression);
    verify_expression_id(statement.secondary_expression);
    verify_expression_id(statement.tertiary_expression);
    verify_expression_id(statement.target_expression);
    for (const auto expression_id : statement.parameter_defaults) {
      verify_expression_id(expression_id);
    }
    for (const auto& selector : statement.case_selectors) {
      verify_expression_id(selector.lower);
      verify_expression_id(selector.upper);
    }
    for (const auto child : statement.body) {
      if (!valid_index(child, program.statements)) {
        add_error(diagnostics, {statement.line, 1}, stage,
                  "statement body references an invalid MIR statement");
      }
    }
    for (const auto child : statement.alternative) {
      if (!valid_index(child, program.statements)) {
        add_error(diagnostics, {statement.line, 1}, stage,
                  "statement alternative references an invalid MIR statement");
      }
    }
  }

  std::vector<bool> reachable(program.statements.size(), false);
  std::vector<MirStatementId> worklist(program.roots.begin(), program.roots.end());
  while (!worklist.empty()) {
    const auto id = worklist.back();
    worklist.pop_back();
    if (!valid_index(id, program.statements)) {
      add_error(diagnostics, {1, 1}, stage, "program root references an invalid MIR statement");
      continue;
    }
    if (reachable[id.value()]) {
      add_error(diagnostics, {program.statements[id.value()].line, 1}, stage,
                "MIR statement arena contains duplicate ownership or a cycle");
      continue;
    }
    reachable[id.value()] = true;
    const auto& node = program.statements[id.value()];
    worklist.insert(worklist.end(), node.body.begin(), node.body.end());
    worklist.insert(worklist.end(), node.alternative.begin(), node.alternative.end());
  }
  if (static_cast<std::size_t>(std::count(reachable.begin(), reachable.end(), true)) + 1U !=
      program.statements.size()) {
    add_error(diagnostics, {1, 1}, stage, "MIR statement arena contains unreachable operations");
  }
}

struct ValueDefinition {
  BlockId block{};
  std::size_t order{0};
  TypeId type{};
  ShapeId shape{};
  StorageId storage{};
};

void verify_terminator_shape(const BasicBlock& block, std::vector<Diagnostic>& diagnostics,
                             const std::string_view stage) {
  const auto& terminator = block.terminator;
  bool valid = false;
  switch (terminator.kind) {
    case TerminatorKind::branch:
      valid = terminator.operands.empty() && terminator.successors.size() == 1 &&
              terminator.successor_arguments.size() == 1;
      break;
    case TerminatorKind::conditional_branch:
      valid = terminator.operands.size() == 1 && terminator.successors.size() == 2 &&
              terminator.successor_arguments.size() == 2;
      break;
    case TerminatorKind::return_value:
      valid = terminator.operands.size() <= 1 && terminator.successors.empty() &&
              terminator.successor_arguments.empty();
      break;
    case TerminatorKind::unreachable:
      valid = terminator.operands.empty() && terminator.successors.empty() &&
              terminator.successor_arguments.empty();
      break;
    case TerminatorKind::none: break;
  }
  if (!valid) {
    add_error(diagnostics, {1, 1}, stage,
              "terminator operand/successor arity does not match its opcode");
  }
}

void verify_cfg(const Program& program, std::vector<Diagnostic>& diagnostics,
                const std::string_view stage) {
  std::vector<MirFunctionId> block_owners(program.blocks.size());
  std::vector<BlockId> instruction_owners(program.instructions.size());

  for (std::size_t index = 1; index < program.functions.size(); ++index) {
    const auto& function = program.functions[index];
    if (function.id.value() != index || !valid_index(function.entry, program.blocks) ||
        function.blocks.empty()) {
      add_error(diagnostics, {1, 1}, stage,
                "function table is not dense or function has no valid entry block");
      continue;
    }
    if (std::find(function.blocks.begin(), function.blocks.end(), function.entry) ==
        function.blocks.end()) {
      add_error(diagnostics, {1, 1}, stage, "function entry block is not owned by the function");
    }
    for (const auto block_id : function.blocks) {
      if (!valid_index(block_id, program.blocks)) {
        add_error(diagnostics, {1, 1}, stage, "function references an invalid block");
        continue;
      }
      auto& owner = block_owners[block_id.value()];
      if (owner.valid()) {
        add_error(diagnostics, {1, 1}, stage,
                  "basic block is owned more than once by the function table");
      } else {
        owner = function.id;
      }
    }
  }

  for (std::size_t index = 1; index < program.blocks.size(); ++index) {
    const auto& block = program.blocks[index];
    if (block.id.value() != index) {
      add_error(diagnostics, {1, 1}, stage, "basic block table is not dense");
    }
    if (!block_owners[index].valid()) {
      add_error(diagnostics, {1, 1}, stage, "basic block has no owning function");
    }
    verify_terminator_shape(block, diagnostics, stage);
    for (const auto instruction_id : block.instructions) {
      if (!valid_index(instruction_id, program.instructions)) {
        add_error(diagnostics, {1, 1}, stage, "block references an invalid instruction");
        continue;
      }
      auto& owner = instruction_owners[instruction_id.value()];
      if (owner.valid()) {
        add_error(diagnostics, program.instructions[instruction_id.value()].location, stage,
                  "instruction is owned by more than one basic block");
      } else {
        owner = block.id;
      }
    }
    for (const auto successor : block.terminator.successors) {
      if (!valid_index(successor, program.blocks)) {
        add_error(diagnostics, {1, 1}, stage, "terminator references an invalid successor");
      } else if (block_owners[index].valid() && block_owners[successor.value()].valid() &&
                 block_owners[index] != block_owners[successor.value()]) {
        add_error(diagnostics, {1, 1}, stage, "control-flow edge crosses a function boundary");
      }
    }
    if (block.terminator.successor_arguments.size() == block.terminator.successors.size()) {
      for (std::size_t edge = 0; edge < block.terminator.successors.size(); ++edge) {
        const auto successor = block.terminator.successors[edge];
        if (!valid_index(successor, program.blocks)) continue;
        if (block.terminator.successor_arguments[edge].size() !=
            program.blocks[successor.value()].arguments.size()) {
          add_error(diagnostics, {1, 1}, stage,
                    "control-flow edge argument arity does not match successor block arguments");
        }
      }
    }
  }

  std::unordered_map<ValueId, ValueDefinition> definitions;
  for (std::size_t block_index = 1; block_index < program.blocks.size(); ++block_index) {
    const auto& block = program.blocks[block_index];
    for (const auto& argument : block.arguments) {
      if (!argument.value.valid() || !valid_index(argument.type, program.types) ||
          !valid_index(argument.shape, program.shapes) ||
          (argument.storage.valid() && !valid_index(argument.storage, program.storages)) ||
          !definitions
               .emplace(argument.value, ValueDefinition{block.id, 0, argument.type, argument.shape,
                                                        argument.storage})
               .second) {
        add_error(diagnostics, {1, 1}, stage,
                  "block argument has invalid metadata or a duplicate value identity");
      }
    }
    for (std::size_t order = 0; order < block.instructions.size(); ++order) {
      const auto instruction_id = block.instructions[order];
      if (!valid_index(instruction_id, program.instructions)) continue;
      const auto& instruction = program.instructions[instruction_id.value()];
      if (instruction.result.valid() &&
          !definitions
               .emplace(instruction.result, ValueDefinition{block.id, order + 1U, instruction.type,
                                                            instruction.shape, instruction.storage})
               .second) {
        add_error(diagnostics, instruction.location, stage,
                  "value is defined by more than one instruction or block argument");
      }
    }
  }
  for (std::size_t index = 1; index < program.instructions.size(); ++index) {
    const auto& instruction = program.instructions[index];
    if (!instruction_owners[index].valid()) {
      add_error(diagnostics, instruction.location, stage, "instruction has no owning basic block");
    }
  }

  for (std::size_t block_index = 1; block_index < program.blocks.size(); ++block_index) {
    const auto& block = program.blocks[block_index];
    if (block.terminator.successor_arguments.size() != block.terminator.successors.size()) continue;
    for (std::size_t edge = 0; edge < block.terminator.successors.size(); ++edge) {
      const auto successor = block.terminator.successors[edge];
      if (!valid_index(successor, program.blocks)) continue;
      const auto& expected = program.blocks[successor.value()].arguments;
      const auto& actual = block.terminator.successor_arguments[edge];
      if (actual.size() != expected.size()) continue;
      for (std::size_t argument = 0; argument < actual.size(); ++argument) {
        const auto found = definitions.find(actual[argument]);
        if (found == definitions.end()) continue;
        if (!compatible_type(program, found->second.type, expected[argument].type) ||
            !compatible_shape(program, found->second.shape, expected[argument].shape,
                              expected[argument].type) ||
            (found->second.storage.valid() && expected[argument].storage.valid() &&
             found->second.storage != expected[argument].storage)) {
          add_error(diagnostics, {1, 1}, stage,
                    "control-flow edge argument type, shape, or storage is incompatible (actual " +
                        std::to_string(found->second.type.value()) + "/" +
                        std::to_string(found->second.shape.value()) + ", expected " +
                        std::to_string(expected[argument].type.value()) + "/" +
                        std::to_string(expected[argument].shape.value()) + ")");
        }
      }
    }
  }

  for (std::size_t function_index = 1; function_index < program.functions.size();
       ++function_index) {
    const auto& function = program.functions[function_index];
    if (function.blocks.empty() || !valid_index(function.entry, program.blocks)) continue;
    const auto& entry_block = program.blocks[function.entry.value()];
    if (entry_block.arguments.size() != function.parameter_types.size() ||
        function.parameter_shapes.size() != function.parameter_types.size() ||
        function.parameter_optional.size() != function.parameter_types.size()) {
      add_error(diagnostics, {1, 1}, stage,
                "function entry block arguments do not match its parameter signature");
    } else {
      for (std::size_t parameter = 0; parameter < function.parameter_types.size(); ++parameter) {
        if (!valid_index(function.parameter_types[parameter], program.types) ||
            !valid_index(function.parameter_shapes[parameter], program.shapes) ||
            entry_block.arguments[parameter].type != function.parameter_types[parameter] ||
            entry_block.arguments[parameter].shape != function.parameter_shapes[parameter]) {
          add_error(diagnostics, {1, 1}, stage,
                    "function parameter type/shape is invalid or differs from its entry argument");
        }
      }
    }
    if (function.result_shapes.size() != function.result_types.size()) {
      add_error(diagnostics, {1, 1}, stage,
                "function result type and shape signature arities differ");
    }
    for (const auto result : function.result_types) {
      if (!valid_index(result, program.types)) {
        add_error(diagnostics, {1, 1}, stage, "function result signature has an invalid type");
      }
    }
    for (const auto result_shape : function.result_shapes) {
      if (!valid_index(result_shape, program.shapes)) {
        add_error(diagnostics, {1, 1}, stage, "function result signature has an invalid shape");
      }
    }
    std::unordered_map<BlockId, std::size_t> positions;
    for (std::size_t index = 0; index < function.blocks.size(); ++index) {
      positions.emplace(function.blocks[index], index);
    }
    const auto entry = positions.find(function.entry);
    if (entry == positions.end()) continue;

    std::vector<std::vector<std::size_t>> predecessors(function.blocks.size());
    for (std::size_t index = 0; index < function.blocks.size(); ++index) {
      const auto block_id = function.blocks[index];
      if (!valid_index(block_id, program.blocks)) continue;
      for (const auto successor : program.blocks[block_id.value()].terminator.successors) {
        const auto found = positions.find(successor);
        if (found != positions.end()) predecessors[found->second].push_back(index);
      }
    }

    std::vector<std::vector<bool>> dominators(function.blocks.size(),
                                              std::vector<bool>(function.blocks.size(), true));
    std::fill(dominators[entry->second].begin(), dominators[entry->second].end(), false);
    dominators[entry->second][entry->second] = true;
    bool changed = true;
    while (changed) {
      changed = false;
      for (std::size_t block = 0; block < function.blocks.size(); ++block) {
        if (block == entry->second) continue;
        std::vector<bool> next(function.blocks.size(), false);
        if (!predecessors[block].empty()) {
          std::fill(next.begin(), next.end(), true);
          for (const auto predecessor : predecessors[block]) {
            for (std::size_t candidate = 0; candidate < next.size(); ++candidate) {
              next[candidate] = next[candidate] && dominators[predecessor][candidate];
            }
          }
        }
        next[block] = true;
        if (next != dominators[block]) {
          dominators[block] = std::move(next);
          changed = true;
        }
      }
    }

    const auto verify_use = [&](const ValueId operand, const BlockId use_block,
                                const std::size_t use_order, const SourceLocation location) {
      const auto definition = definitions.find(operand);
      if (definition == definitions.end()) {
        add_error(diagnostics, location, stage, "value operand has no definition");
        return;
      }
      if (definition->second.block == use_block) {
        if (definition->second.order >= use_order) {
          add_error(diagnostics, location, stage,
                    "value definition does not precede its use in the basic block");
        }
        return;
      }
      const auto definition_position = positions.find(definition->second.block);
      const auto use_position = positions.find(use_block);
      if (definition_position == positions.end() || use_position == positions.end() ||
          !dominators[use_position->second][definition_position->second]) {
        add_error(diagnostics, location, stage,
                  "value %v" + std::to_string(operand.value()) + " defined in block ^b" +
                      std::to_string(definition->second.block.value()) +
                      " does not dominate its use in block ^b" + std::to_string(use_block.value()));
      }
    };

    for (const auto block_id : function.blocks) {
      if (!valid_index(block_id, program.blocks)) continue;
      const auto& block = program.blocks[block_id.value()];
      for (std::size_t order = 0; order < block.instructions.size(); ++order) {
        const auto instruction_id = block.instructions[order];
        if (!valid_index(instruction_id, program.instructions)) continue;
        const auto& instruction = program.instructions[instruction_id.value()];
        for (const auto operand : instruction.operands) {
          verify_use(operand, block.id, order + 1U, instruction.location);
        }
      }
      for (const auto operand : block.terminator.operands) {
        verify_use(operand, block.id, block.instructions.size() + 1U, {1, 1});
      }
      for (const auto& edge_arguments : block.terminator.successor_arguments) {
        for (const auto operand : edge_arguments) {
          verify_use(operand, block.id, block.instructions.size() + 1U, {1, 1});
        }
      }
    }
  }
}

void verify_function_types_and_calls(const Program& program, std::vector<Diagnostic>& diagnostics,
                                     const std::string_view stage) {
  std::vector<MirFunctionId> instruction_callers(program.instructions.size());
  std::unordered_map<ValueId, TypeId> value_types;
  std::unordered_map<SymbolId, MirFunctionId> function_symbols;

  for (std::size_t function_index = 1; function_index < program.functions.size();
       ++function_index) {
    const auto& function = program.functions[function_index];
    if (function_index > 1U && (!function.symbol.valid() || !function.origin.valid() ||
                                !function_symbols.emplace(function.symbol, function.id).second)) {
      add_error(diagnostics, {1, 1}, stage, "function has an invalid or duplicate symbol identity");
    }
    const auto* signature = type_data(program, function.signature);
    if (signature == nullptr || signature->kind != TypeKind::function ||
        signature->parameters.size() != function.parameter_types.size() ||
        signature->results != function.result_types ||
        (!function.parameter_optional.empty() &&
         function.parameter_optional.size() != function.parameter_types.size())) {
      add_error(diagnostics, {1, 1}, stage,
                "function signature is missing or disagrees with its logical types");
    } else {
      for (std::size_t parameter = 0; parameter < function.parameter_types.size(); ++parameter) {
        if (!compatible_type(program, function.parameter_types[parameter],
                             signature->parameters[parameter])) {
          add_error(diagnostics, {1, 1}, stage,
                    "function signature parameter disagrees with its logical type");
        }
      }
    }
    for (const auto block_id : function.blocks) {
      if (!valid_index(block_id, program.blocks)) continue;
      const auto& block = program.blocks[block_id.value()];
      for (const auto& argument : block.arguments) {
        if (argument.value.valid() && valid_index(argument.type, program.types)) {
          value_types.emplace(argument.value, argument.type);
        }
      }
      for (const auto instruction_id : block.instructions) {
        if (!valid_index(instruction_id, program.instructions)) continue;
        instruction_callers[instruction_id.value()] = function.id;
        const auto& instruction = program.instructions[instruction_id.value()];
        if (instruction.result.valid() && valid_index(instruction.type, program.types)) {
          value_types.emplace(instruction.result, instruction.type);
        }
      }
    }
  }

  for (std::size_t function_index = 1; function_index < program.functions.size();
       ++function_index) {
    const auto& function = program.functions[function_index];
    for (const auto block_id : function.blocks) {
      if (!valid_index(block_id, program.blocks)) continue;
      const auto& terminator = program.blocks[block_id.value()].terminator;
      if (terminator.kind != TerminatorKind::return_value || terminator.operands.empty()) continue;
      if (function.result_types.empty()) {
        add_error(diagnostics, {1, 1}, stage,
                  "value-returning terminator belongs to a function with no result type");
        continue;
      }
      const auto actual = value_types.find(terminator.operands.front());
      if (actual == value_types.end()) continue;
      if (function.result_types.size() == 1U) {
        if (!compatible_type(program, actual->second, function.result_types.front())) {
          add_error(diagnostics, {1, 1}, stage,
                    "return value type disagrees with the function result signature");
        }
        continue;
      }
      const auto* tuple = type_data(program, actual->second);
      if (tuple == nullptr || tuple->kind != TypeKind::tuple ||
          tuple->elements.size() != function.result_types.size()) {
        add_error(diagnostics, {1, 1}, stage,
                  "multiple function results are not returned as a matching tuple type");
        continue;
      }
      for (std::size_t result = 0; result < function.result_types.size(); ++result) {
        if (!compatible_type(program, tuple->elements[result], function.result_types[result])) {
          add_error(diagnostics, {1, 1}, stage,
                    "tuple return element disagrees with the function result signature");
        }
      }
    }
  }

  std::vector<bool> seen_call_instruction(program.instructions.size(), false);
  std::vector<bool> seen_call_origin(program.hir_node_count + 1U, false);
  for (const auto& call : program.calls) {
    if (!valid_index(call.instruction, program.instructions) ||
        !valid_index(call.caller, program.functions) ||
        !valid_index(call.callee, program.functions) || !call.origin.valid() ||
        call.origin.value() >= seen_call_origin.size() || call.requested_results == 0) {
      add_error(diagnostics, {1, 1}, stage, "call-site table contains invalid identity or arity");
      continue;
    }
    const auto& instruction = program.instructions[call.instruction.value()];
    if (seen_call_instruction[call.instruction.value()] || seen_call_origin[call.origin.value()] ||
        instruction.opcode != Opcode::call || instruction.origin != call.origin ||
        instruction.callee != call.callee ||
        instruction_callers[call.instruction.value()] != call.caller) {
      add_error(diagnostics, instruction.location, stage,
                "call site does not match its call instruction or owning function");
      continue;
    }
    seen_call_instruction[call.instruction.value()] = true;
    seen_call_origin[call.origin.value()] = true;
    const auto& callee = program.functions[call.callee.value()];
    const auto* signature = type_data(program, callee.signature);
    if (signature == nullptr || signature->kind != TypeKind::function) {
      add_error(diagnostics, instruction.location, stage,
                "call site references a callee without a function type");
      continue;
    }
    if (call.arguments.size() > signature->parameters.size()) {
      add_error(diagnostics, instruction.location, stage,
                "call has more arguments than the callee signature");
      continue;
    }
    for (std::size_t parameter = call.arguments.size(); parameter < signature->parameters.size();
         ++parameter) {
      if (parameter >= callee.parameter_optional.size() || !callee.parameter_optional[parameter]) {
        add_error(diagnostics, instruction.location, stage,
                  "call omits a required function parameter");
      }
    }
    for (std::size_t argument = 0; argument < call.arguments.size(); ++argument) {
      const auto& actual = call.arguments[argument];
      const bool optional =
          argument < callee.parameter_optional.size() && callee.parameter_optional[argument];
      const auto* formal = type_data(program, signature->parameters[argument]);
      const auto formal_intent = formal != nullptr && formal->kind == TypeKind::reference
                                     ? formal->reference_intent
                                     : ParameterIntent::none;
      if (actual.intent != formal_intent) {
        add_error(diagnostics, instruction.location, stage,
                  "call argument intent disagrees with the callee signature");
      }
      if (!transfer_matches_intent(actual.transfer, actual.intent)) {
        add_error(diagnostics, instruction.location, stage,
                  "call argument transfer mode disagrees with its intent");
      }
      if (actual.transfer == ArgumentTransfer::omitted) {
        if (!optional || actual.storage.valid() || actual.root.valid() || actual.writable) {
          add_error(diagnostics, instruction.location, stage,
                    "omitted call argument has an invalid optional or storage contract");
        }
        continue;
      }
      if (!valid_index(actual.type, program.types) ||
          !compatible_type(program, actual.type, signature->parameters[argument])) {
        add_error(diagnostics, instruction.location, stage,
                  "call argument type disagrees with the callee signature");
        continue;
      }
      if (actual.storage.valid()) {
        if (!valid_index(actual.storage, program.storages)) {
          add_error(diagnostics, instruction.location, stage,
                    "call argument references invalid storage");
          continue;
        }
        const auto& storage = program.storages[actual.storage.value()];
        StorageId root = actual.storage;
        for (std::size_t depth = 0;
             depth < program.storages.size() && valid_index(root, program.storages) &&
             program.storages[root.value()].kind == StorageKind::view;
             ++depth) {
          root = program.storages[root.value()].base;
        }
        if (actual.root != root || actual.view != storage.view ||
            actual.lifetime != storage.lifetime || actual.writable != storage.writable) {
          add_error(diagnostics, instruction.location, stage,
                    "call argument storage region metadata is stale or inconsistent");
        }
      } else if (actual.root.valid() || actual.view != StorageViewKind::none || actual.writable) {
        add_error(diagnostics, instruction.location, stage,
                  "storage-free call argument has storage region metadata");
      }
      if (argument_transfer_writes(actual.transfer) &&
          (!valid_index(actual.storage, program.storages) || !actual.writable)) {
        add_error(diagnostics, instruction.location, stage,
                  "OUT/INOUT call argument is not backed by writable storage");
      }
      if (argument_transfer_copies(actual.transfer) &&
          (actual.view != StorageViewKind::section ||
           actual.lifetime != StorageLifetime::expression)) {
        add_error(diagnostics, instruction.location, stage,
                  "copy-in/copy-out call argument is not an expression-lifetime section view");
      }
      if ((actual.transfer == ArgumentTransfer::mutable_borrow_out ||
           actual.transfer == ArgumentTransfer::mutable_borrow_inout) &&
          actual.view == StorageViewKind::section) {
        add_error(diagnostics, instruction.location, stage,
                  "section writable actual requires an explicit copy transfer");
      }
      if (argument_transfer_forwards_optional(actual.transfer) &&
          (!valid_index(actual.storage, program.storages) ||
           program.storages[actual.storage.value()].kind != StorageKind::parameter ||
           !program.storages[actual.storage.value()].optional)) {
        add_error(diagnostics, instruction.location, stage,
                  "optional forwarding does not reference optional parameter storage");
      }
    }
    if (signature->results.empty()) continue;
    if (!valid_index(call.result_type, program.types) ||
        call.requested_results > signature->results.size()) {
      add_error(diagnostics, instruction.location, stage,
                "call result arity or type identity disagrees with the callee signature");
      continue;
    }
    if (call.requested_results == 1U) {
      if (!compatible_type(program, call.result_type, signature->results.front())) {
        add_error(diagnostics, instruction.location, stage,
                  "call result type disagrees with the callee result signature");
      }
      continue;
    }
    const auto* tuple = type_data(program, call.result_type);
    if (tuple == nullptr || tuple->kind != TypeKind::tuple ||
        tuple->elements.size() != call.requested_results) {
      add_error(diagnostics, instruction.location, stage,
                "multi-result call does not produce a matching tuple type");
      continue;
    }
    for (std::size_t result = 0; result < call.requested_results; ++result) {
      if (!compatible_type(program, tuple->elements[result], signature->results[result])) {
        add_error(diagnostics, instruction.location, stage,
                  "multi-result call tuple element disagrees with the callee signature");
      }
    }
  }
  for (std::size_t index = 1; index < program.instructions.size(); ++index) {
    const auto& instruction = program.instructions[index];
    if (instruction.callee.valid() &&
        (instruction.opcode != Opcode::call ||
         !valid_index(instruction.callee, program.functions) || !seen_call_instruction[index])) {
      add_error(diagnostics, instruction.location, stage,
                "linked user-function call instruction has no matching call-site entry");
    }
  }
}

}  // namespace

std::vector<Diagnostic> verify(const Program& program, const std::string_view stage) {
  std::vector<Diagnostic> diagnostics;
  if (program.source_language == SourceLanguage::automatic) {
    add_error(diagnostics, {1, 1}, stage, "source language is unresolved");
  }
  if (program.types.size() <= 1 || program.shapes.size() <= 1 || program.blocks.size() <= 1 ||
      program.functions.size() <= 1) {
    add_error(diagnostics, {1, 1}, stage, "required dense tables are empty");
    return diagnostics;
  }
  if (program.attributes.mir_revision != program.revision ||
      program.attributes.expression_count + 1U != program.expressions.size() ||
      program.attributes.statement_count + 1U != program.statements.size() ||
      program.attributes.expressions.size() != program.expressions.size() ||
      program.attributes.statements.size() != program.statements.size()) {
    add_error(diagnostics, {1, 1}, stage,
              "operation-attribute table is stale or its dense inventory is inconsistent");
  }
  for (std::size_t index = 1; index < program.types.size(); ++index) {
    const auto& type = program.types[index];
    for (const auto element : type.elements) {
      if (!valid_index(element, program.types)) {
        add_error(diagnostics, {1, 1}, stage, "composite type references an invalid element type");
      }
    }
    for (const auto parameter : type.parameters) {
      if (!valid_index(parameter, program.types)) {
        add_error(diagnostics, {1, 1}, stage, "function type references an invalid parameter type");
      }
    }
    for (const auto result : type.results) {
      if (!valid_index(result, program.types)) {
        add_error(diagnostics, {1, 1}, stage, "function type references an invalid result type");
      }
    }
    const bool has_function_payload = !type.parameters.empty() || !type.results.empty();
    switch (type.kind) {
      case TypeKind::scalar:
        if (type.value_type == ValueType::list || type.value_type == ValueType::tuple ||
            type.value_type == ValueType::function || !type.elements.empty() ||
            has_function_payload || type.referent.valid()) {
          add_error(diagnostics, {1, 1}, stage, "scalar type has composite payload");
        }
        break;
      case TypeKind::sequence:
        if (type.value_type != ValueType::list || !type.elements.empty() || has_function_payload ||
            type.referent.valid()) {
          add_error(diagnostics, {1, 1}, stage, "sequence type has an invalid payload");
        }
        break;
      case TypeKind::tuple:
        if (type.value_type != ValueType::tuple || has_function_payload || type.referent.valid()) {
          add_error(diagnostics, {1, 1}, stage, "tuple type has an invalid payload");
        }
        break;
      case TypeKind::function:
        if (type.value_type != ValueType::function || !type.elements.empty() ||
            type.referent.valid()) {
          add_error(diagnostics, {1, 1}, stage, "function type has an invalid payload");
        }
        break;
      case TypeKind::reference:
        if (!valid_index(type.referent, program.types) || type.referent.value() == index ||
            type.reference_intent == ParameterIntent::none || !type.elements.empty() ||
            has_function_payload) {
          add_error(diagnostics, {1, 1}, stage, "reference type has an invalid referent or mode");
        }
        break;
    }
  }
  for (std::size_t index = 1; index < program.shapes.size(); ++index) {
    const auto& shape = program.shapes[index];
    if (!shape.dynamic_rank && shape.extents.size() != shape.strides.size()) {
      add_error(diagnostics, {1, 1}, stage,
                "shape extent and canonical stride ranks are inconsistent");
    }
  }
  for (std::size_t index = 1; index < program.storages.size(); ++index) {
    const auto& storage = program.storages[index];
    if (storage.name.empty() ||
        (storage.kind != StorageKind::temporary && !storage.symbol.valid()) ||
        !storage.origin.valid() || !valid_index(storage.type, program.types) ||
        !valid_index(storage.shape, program.shapes)) {
      add_error(diagnostics, {1, 1}, stage,
                "storage has invalid name, symbol, origin, type, or shape metadata");
    }
    if (storage.kind == StorageKind::view) {
      if (!valid_index(storage.base, program.storages) || storage.base.value() == index ||
          storage.view == StorageViewKind::none) {
        add_error(diagnostics, {1, 1}, stage, "view storage has an invalid base storage");
      }
    } else if (storage.base.valid() || storage.view != StorageViewKind::none) {
      add_error(diagnostics, {1, 1}, stage, "non-view storage unexpectedly has view metadata");
    }
    if (storage.intent == ParameterIntent::in && storage.writable) {
      add_error(diagnostics, {1, 1}, stage, "intent-in storage must not be writable");
    }
    if (storage.optional && storage.kind != StorageKind::parameter) {
      add_error(diagnostics, {1, 1}, stage, "only parameter storage may be optional");
    }
    if ((storage.kind == StorageKind::parameter && storage.lifetime != StorageLifetime::borrowed) ||
        (storage.kind == StorageKind::global && storage.lifetime != StorageLifetime::module) ||
        (storage.kind == StorageKind::view && storage.lifetime != StorageLifetime::expression)) {
      add_error(diagnostics, {1, 1}, stage, "storage kind and lifetime contract are inconsistent");
    }
  }
  for (std::size_t index = 1; index < program.instructions.size(); ++index) {
    const auto& instruction = program.instructions[index];
    if (instruction.id.value() != index || instruction.opcode == Opcode::invalid ||
        !instruction.origin.valid()) {
      add_error(diagnostics, instruction.location, stage,
                "instruction table is not dense or has an invalid opcode/origin");
    }
    if (instruction.result.valid() && !valid_index(instruction.type, program.types)) {
      add_error(diagnostics, instruction.location, stage,
                "value-producing instruction has an invalid type");
    }
    if (instruction.result.valid() && !valid_index(instruction.shape, program.shapes)) {
      add_error(diagnostics, instruction.location, stage,
                "value-producing instruction has an invalid shape");
    }
    if (instruction.opcode >= Opcode::literal && instruction.opcode <= Opcode::aggregate &&
        !instruction.result.valid()) {
      add_error(diagnostics, instruction.location, stage,
                "expression instruction does not define a value");
    }
    if ((instruction.opcode == Opcode::copy || instruction.opcode == Opcode::writeback) &&
        (!instruction.result.valid() || (instruction.transfer != ArgumentTransfer::copy_out &&
                                         instruction.transfer != ArgumentTransfer::copy_in_out))) {
      add_error(diagnostics, instruction.location, stage,
                "copy/writeback operation has no result or an invalid transfer mode");
    }
    if (instruction.opcode == Opcode::copy &&
        ((instruction.transfer == ArgumentTransfer::copy_out && !instruction.operands.empty()) ||
         (instruction.transfer == ArgumentTransfer::copy_in_out &&
          instruction.operands.size() != 1U))) {
      add_error(diagnostics, instruction.location, stage,
                "copy operation operand arity disagrees with its transfer mode");
    }
    if (instruction.opcode == Opcode::writeback &&
        (instruction.operands.size() != 1U || !instruction.storage.valid())) {
      add_error(diagnostics, instruction.location, stage,
                "writeback operation has invalid source or target storage");
    }
    if (instruction.opcode == Opcode::allocate &&
        (!instruction.storage.valid() || !instruction.operands.empty() ||
         !valid_index(instruction.type, program.types) ||
         !valid_index(instruction.shape, program.shapes))) {
      add_error(diagnostics, instruction.location, stage,
                "allocate operation has invalid storage, operands, type, or shape");
    }
    if ((instruction.opcode == Opcode::store || instruction.opcode == Opcode::store_indexed) &&
        (!instruction.storage.valid() || instruction.operands.empty() ||
         !instruction.result.valid() || !valid_index(instruction.type, program.types) ||
         !valid_index(instruction.shape, program.shapes))) {
      add_error(diagnostics, instruction.location, stage,
                "store operation has invalid storage, operands, result, type, or shape");
    }
    if (instruction.opcode != Opcode::copy && instruction.opcode != Opcode::writeback &&
        instruction.transfer != ArgumentTransfer::value) {
      add_error(diagnostics, instruction.location, stage,
                "non-transfer operation unexpectedly carries a call transfer mode");
    }
    if (instruction.opcode == Opcode::truthiness &&
        (instruction.operands.size() != 1U ||
         value_type(program, instruction.type) != ValueType::boolean)) {
      add_error(diagnostics, instruction.location, stage,
                "truthiness operation has invalid operand or result type");
    }
    if (instruction.opcode == Opcode::compare &&
        (instruction.operands.size() != 2U || instruction.comparison == ComparisonOperator::none ||
         value_type(program, instruction.type) != ValueType::boolean)) {
      add_error(diagnostics, instruction.location, stage,
                "comparison operation has invalid operands, operator, or result type");
    }
    if (instruction.opcode != Opcode::compare &&
        instruction.comparison != ComparisonOperator::none) {
      add_error(diagnostics, instruction.location, stage,
                "non-comparison operation unexpectedly carries a comparison operator");
    }
    for (const auto operand : instruction.operands) {
      if (!operand.valid()) {
        add_error(diagnostics, instruction.location, stage,
                  "instruction has an invalid value operand");
      }
    }
  }
  verify_cfg(program, diagnostics, stage);
  verify_function_types_and_calls(program, diagnostics, stage);
  const auto expression_index = build_expression_verification_index(program);
  for (std::size_t index = 1; index < program.expressions.size(); ++index) {
    verify_expression(program.expressions[index], program, expression_index, diagnostics, stage);
  }
  verify_statements(program, diagnostics, stage);
  return diagnostics;
}

}  // namespace mpf::detail::mir
