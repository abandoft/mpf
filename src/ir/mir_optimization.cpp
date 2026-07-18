#include "mir_optimization.hpp"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "mir_opcode.hpp"

namespace mpf::detail::mir {
namespace {

void add_error(std::vector<Diagnostic>& diagnostics, std::string message) {
  diagnostics.push_back({DiagnosticSeverity::error,
                         "MPF0006",
                         "MIR optimization failed: " + std::move(message),
                         {1, 1}});
}

void synchronize_revision(Program& program, const std::uint64_t revision) {
  program.attributes.mir_revision = revision;
  program.attributes.instruction_count =
      program.instructions.size() - (program.instructions.empty() ? 0U : 1U);
}

std::vector<std::size_t> canonical_strides(const ShapeData& shape) {
  if (shape.dynamic_rank) return shape.strides;
  std::vector<std::size_t> result(shape.extents.size(), 1U);
  std::size_t stride = 1U;
  for (std::size_t offset = 0; offset < shape.extents.size(); ++offset) {
    const auto index = shape.layout == semantic::IndexLayout::column_major
                           ? offset
                           : shape.extents.size() - offset - 1U;
    result[index] = stride;
    const auto extent = shape.extents[index];
    if (extent == dynamic_extent || stride == dynamic_extent ||
        (extent != 0U && stride > std::numeric_limits<std::size_t>::max() / extent)) {
      stride = dynamic_extent;
    } else {
      stride *= extent;
    }
  }
  return result;
}

void remap_shape(ShapeId& id, const std::vector<ShapeId>& remap) {
  if (id.valid() && id.value() < remap.size()) id = remap[id.value()];
}

void remap_value_metadata(ValueMetadata& metadata, const std::vector<ShapeId>& remap) {
  remap_shape(metadata.shape, remap);
  for (auto& element : metadata.elements) remap_value_metadata(element, remap);
}

void remap_assignment_pattern(AssignmentPattern& pattern, const std::vector<ShapeId>& remap) {
  if (!pattern.valid()) return;
  remap_shape(pattern.shape, remap);
  for (auto& child : pattern.children) remap_assignment_pattern(child, remap);
}

std::vector<Diagnostic> canonicalize_shapes(Program& program, OptimizationStatistics& statistics) {
  using ShapeKey =
      std::tuple<bool, semantic::IndexLayout, std::vector<std::size_t>, std::vector<std::size_t>>;
  std::map<ShapeKey, ShapeId> canonical;
  std::vector<ShapeId> remap(program.shapes.size());
  std::vector<ShapeData> shapes;
  shapes.reserve(program.shapes.size());
  shapes.push_back({});
  for (std::size_t index = 1; index < program.shapes.size(); ++index) {
    auto shape = program.shapes[index];
    shape.strides = canonical_strides(shape);
    const ShapeKey key{shape.dynamic_rank, shape.layout, shape.extents, shape.strides};
    const auto found = canonical.find(key);
    if (found != canonical.end()) {
      remap[index] = found->second;
      ++statistics.canonicalized_shapes;
      continue;
    }
    const auto id = ShapeId{static_cast<ShapeId::value_type>(shapes.size())};
    remap[index] = id;
    canonical.emplace(key, id);
    shapes.push_back(std::move(shape));
  }
  for (std::size_t index = 1; index < program.expressions.size(); ++index) {
    remap_shape(program.expressions[index].shape_id, remap);
    auto& attributes = program.attributes.expressions[index];
    if (attributes.broadcast.valid) {
      remap_shape(attributes.broadcast.left_shape, remap);
      remap_shape(attributes.broadcast.right_shape, remap);
      remap_shape(attributes.broadcast.result_shape, remap);
    }
    if (attributes.matrix_operation.valid()) {
      remap_shape(attributes.matrix_operation.left_shape, remap);
      if (attributes.matrix_operation.right_shape.valid()) {
        remap_shape(attributes.matrix_operation.right_shape, remap);
      }
      remap_shape(attributes.matrix_operation.result_shape, remap);
    }
    for (auto& shape : attributes.tuple_shapes) remap_shape(shape, remap);
    for (auto& metadata : attributes.sequence_elements) remap_value_metadata(metadata, remap);
  }
  for (std::size_t index = 1; index < program.attributes.statements.size(); ++index) {
    auto& attributes = program.attributes.statements[index];
    remap_assignment_pattern(attributes.target_pattern, remap);
    for (auto& target : attributes.targets) remap_shape(target.shape, remap);
  }
  for (std::size_t index = 1; index < program.storages.size(); ++index) {
    remap_shape(program.storages[index].shape, remap);
  }
  for (std::size_t index = 1; index < program.instructions.size(); ++index) {
    remap_shape(program.instructions[index].shape, remap);
  }
  for (std::size_t index = 1; index < program.blocks.size(); ++index) {
    for (auto& argument : program.blocks[index].arguments) remap_shape(argument.shape, remap);
  }
  for (std::size_t index = 1; index < program.functions.size(); ++index) {
    for (auto& shape : program.functions[index].parameter_shapes) remap_shape(shape, remap);
    for (auto& shape : program.functions[index].result_shapes) remap_shape(shape, remap);
  }
  program.shapes = std::move(shapes);
  return {};
}

void replace_value(Program& program, const ValueId from, const ValueId to) {
  const auto replace = [&](ValueId& value) {
    if (value == from) value = to;
  };
  for (std::size_t index = 1; index < program.instructions.size(); ++index) {
    for (auto& operand : program.instructions[index].operands) replace(operand);
  }
  for (std::size_t index = 1; index < program.blocks.size(); ++index) {
    auto& terminator = program.blocks[index].terminator;
    for (auto& operand : terminator.operands) replace(operand);
    for (auto& edge : terminator.successor_arguments) {
      for (auto& operand : edge) replace(operand);
    }
  }
}

std::vector<Diagnostic> propagate_block_arguments(Program& program,
                                                  OptimizationStatistics& statistics) {
  bool changed = true;
  while (changed) {
    changed = false;
    for (std::size_t target_index = 1; target_index < program.blocks.size() && !changed;
         ++target_index) {
      auto& target = program.blocks[target_index];
      for (std::size_t offset = 0; offset < target.arguments.size(); ++offset) {
        const auto argument_index = target.arguments.size() - offset - 1U;
        const auto argument = target.arguments[argument_index];
        if (!argument.storage.valid()) continue;
        struct Edge {
          std::size_t block{0};
          std::size_t successor{0};
        };
        std::vector<Edge> incoming;
        std::optional<ValueId> common;
        bool compatible = true;
        for (std::size_t block_index = 1; block_index < program.blocks.size(); ++block_index) {
          const auto& terminator = program.blocks[block_index].terminator;
          for (std::size_t edge = 0; edge < terminator.successors.size(); ++edge) {
            if (terminator.successors[edge] != target.id) continue;
            if (edge >= terminator.successor_arguments.size() ||
                argument_index >= terminator.successor_arguments[edge].size()) {
              compatible = false;
              continue;
            }
            const auto actual = terminator.successor_arguments[edge][argument_index];
            if (!common.has_value()) common = actual;
            if (*common != actual) compatible = false;
            incoming.push_back({block_index, edge});
          }
        }
        if (!compatible || incoming.empty() || !common.has_value() || !common->valid() ||
            *common == argument.value) {
          continue;
        }
        replace_value(program, argument.value, *common);
        for (const auto& edge : incoming) {
          auto& actuals = program.blocks[edge.block].terminator.successor_arguments[edge.successor];
          actuals.erase(actuals.begin() + static_cast<std::ptrdiff_t>(argument_index));
        }
        target.arguments.erase(target.arguments.begin() +
                               static_cast<std::ptrdiff_t>(argument_index));
        ++statistics.propagated_block_arguments;
        changed = true;
        break;
      }
    }
  }
  return {};
}

enum class ConstantKind { integer, boolean_value };

constexpr std::int64_t maximum_exact_common_integer = 9007199254740991LL;

struct Constant {
  ConstantKind kind{ConstantKind::integer};
  std::int64_t integer{0};
  bool boolean{false};
};

std::optional<std::int64_t> parse_integer(const std::string& value) {
  std::int64_t result = 0;
  const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
  if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size()) return std::nullopt;
  return result;
}

bool exactly_representable_by_all_targets(const std::int64_t value) noexcept {
  return value >= -maximum_exact_common_integer && value <= maximum_exact_common_integer;
}

std::optional<Constant> constant(const Program& program, const MirExpressionId id) {
  const auto* expression = mir::expression(program, id);
  const auto* facts = mir::attributes(program, id);
  if (expression == nullptr || facts == nullptr || !expression->valid()) return std::nullopt;
  if (expression->kind == ExpressionKind::number_literal &&
      value_type(program, expression->type_id) == ValueType::integer) {
    const auto value = parse_integer(facts->spelling);
    if (value.has_value() && exactly_representable_by_all_targets(*value)) {
      return Constant{ConstantKind::integer, *value, false};
    }
  }
  if (expression->kind == ExpressionKind::boolean_literal) {
    if (facts->spelling == "true") return Constant{ConstantKind::boolean_value, 0, true};
    if (facts->spelling == "false") return Constant{ConstantKind::boolean_value, 0, false};
  }
  return std::nullopt;
}

std::optional<std::int64_t> checked_add(const std::int64_t left, const std::int64_t right) {
  if ((right > 0 && left > std::numeric_limits<std::int64_t>::max() - right) ||
      (right < 0 && left < std::numeric_limits<std::int64_t>::min() - right)) {
    return std::nullopt;
  }
  return left + right;
}

std::optional<std::int64_t> checked_subtract(const std::int64_t left, const std::int64_t right) {
  if ((right < 0 && left > std::numeric_limits<std::int64_t>::max() + right) ||
      (right > 0 && left < std::numeric_limits<std::int64_t>::min() + right)) {
    return std::nullopt;
  }
  return left - right;
}

std::optional<std::int64_t> checked_multiply(const std::int64_t left, const std::int64_t right) {
  if (left == 0 || right == 0) return 0;
  if ((left == -1 && right == std::numeric_limits<std::int64_t>::min()) ||
      (right == -1 && left == std::numeric_limits<std::int64_t>::min())) {
    return std::nullopt;
  }
  if (left > 0) {
    if ((right > 0 && left > std::numeric_limits<std::int64_t>::max() / right) ||
        (right < 0 && right < std::numeric_limits<std::int64_t>::min() / left)) {
      return std::nullopt;
    }
  } else if ((right > 0 && left < std::numeric_limits<std::int64_t>::min() / right) ||
             (right < 0 && left < std::numeric_limits<std::int64_t>::max() / right)) {
    return std::nullopt;
  }
  return left * right;
}

bool compare(const ComparisonOperator operation, const std::int64_t left,
             const std::int64_t right) {
  switch (operation) {
    case ComparisonOperator::equal: return left == right;
    case ComparisonOperator::not_equal: return left != right;
    case ComparisonOperator::less: return left < right;
    case ComparisonOperator::less_equal: return left <= right;
    case ComparisonOperator::greater: return left > right;
    case ComparisonOperator::greater_equal: return left >= right;
    case ComparisonOperator::none:
    case ComparisonOperator::identity:
    case ComparisonOperator::not_identity:
    case ComparisonOperator::contains:
    case ComparisonOperator::not_contains: return false;
  }
  return false;
}

bool fold_expression(Program& program, const MirExpressionId id, OptimizationStatistics& statistics,
                     std::vector<std::uint8_t>& state) {
  if (!id.valid() || id.value() >= program.expressions.size()) return false;
  if (state[id.value()] == 2U) return false;
  if (state[id.value()] == 1U) return false;
  state[id.value()] = 1U;
  auto* expression = mir::expression(program, id);
  auto* facts = mir::attributes(program, id);
  if (expression == nullptr || facts == nullptr || !expression->valid()) {
    state[id.value()] = 2U;
    return false;
  }
  const auto children = expression->children;
  for (const auto child : children) (void)fold_expression(program, child, statistics, state);
  std::optional<Constant> result;
  if (expression->kind == ExpressionKind::unary && children.size() == 1U && !facts->lazy_cfg) {
    const auto operand = constant(program, children.front());
    if (operand.has_value() && operand->kind == ConstantKind::integer && facts->spelling == "+") {
      result = operand;
    } else if (operand.has_value() && operand->kind == ConstantKind::integer &&
               facts->spelling == "-" &&
               operand->integer != std::numeric_limits<std::int64_t>::min()) {
      result = Constant{ConstantKind::integer, -operand->integer, false};
    } else if (operand.has_value() && operand->kind == ConstantKind::boolean_value &&
               facts->spelling == "!") {
      result = Constant{ConstantKind::boolean_value, 0, !operand->boolean};
    }
  } else if (expression->kind == ExpressionKind::binary && children.size() == 2U &&
             !facts->lazy_cfg) {
    const auto left = constant(program, children[0]);
    const auto right = constant(program, children[1]);
    if (left.has_value() && right.has_value() && left->kind == ConstantKind::integer &&
        right->kind == ConstantKind::integer) {
      if (facts->comparison != ComparisonOperator::none &&
          !comparison_is_identity(facts->comparison) &&
          !comparison_is_membership(facts->comparison)) {
        result = Constant{ConstantKind::boolean_value, 0,
                          compare(facts->comparison, left->integer, right->integer)};
      } else if (facts->operation == BinaryOperator::add) {
        const auto value = checked_add(left->integer, right->integer);
        if (value.has_value() && exactly_representable_by_all_targets(*value)) {
          result = Constant{ConstantKind::integer, *value, false};
        }
      } else if (facts->operation == BinaryOperator::subtract) {
        const auto value = checked_subtract(left->integer, right->integer);
        if (value.has_value() && exactly_representable_by_all_targets(*value)) {
          result = Constant{ConstantKind::integer, *value, false};
        }
      } else if (facts->operation == BinaryOperator::multiply) {
        const auto value = checked_multiply(left->integer, right->integer);
        if (value.has_value() && exactly_representable_by_all_targets(*value)) {
          result = Constant{ConstantKind::integer, *value, false};
        }
      }
    } else if (left.has_value() && right.has_value() && left->kind == ConstantKind::boolean_value &&
               right->kind == ConstantKind::boolean_value &&
               (facts->comparison == ComparisonOperator::equal ||
                facts->comparison == ComparisonOperator::not_equal)) {
      const auto equal = left->boolean == right->boolean;
      result = Constant{ConstantKind::boolean_value, 0,
                        facts->comparison == ComparisonOperator::equal ? equal : !equal};
    }
  }
  if (!result.has_value()) {
    state[id.value()] = 2U;
    return false;
  }
  expression = mir::expression(program, id);
  facts = mir::attributes(program, id);
  auto& instruction = program.instructions[expression->instruction.value()];
  expression->kind = result->kind == ConstantKind::integer ? ExpressionKind::number_literal
                                                           : ExpressionKind::boolean_literal;
  expression->children.clear();
  expression->storage_id = {};
  expression->symbol_id = {};
  facts->spelling = result->kind == ConstantKind::integer ? std::to_string(result->integer)
                    : result->boolean                     ? "true"
                                                          : "false";
  facts->unary_operation = UnaryOperator::none;
  facts->operation = BinaryOperator::none;
  facts->comparison = ComparisonOperator::none;
  facts->comparisons.clear();
  facts->logical_evaluation = semantic::LogicalEvaluation::none;
  facts->array_operation = semantic::ArrayOperation::native;
  facts->broadcast = {};
  facts->matrix_operation = {};
  facts->binding = BindingKind::unresolved;
  facts->intrinsic = IntrinsicId::none;
  facts->tuple_shapes.clear();
  facts->sequence_elements.clear();
  facts->requested_results = 1U;
  facts->multi_result_call = false;
  facts->procedure_has_result = false;
  facts->index_base = 0U;
  facts->allow_negative_index = false;
  facts->slice_stop_inclusive = false;
  facts->index_extent = semantic::IndexExtentSource::none;
  facts->index_selectors.clear();
  facts->index_extents.clear();
  facts->lazy_cfg = false;
  facts->storage_region = {};
  instruction.opcode = Opcode::literal;
  instruction.callee = {};
  instruction.intrinsic = IntrinsicId::none;
  instruction.transfer = ArgumentTransfer::value;
  instruction.comparison = ComparisonOperator::none;
  instruction.truthiness = semantic::Truthiness::native;
  instruction.storage = {};
  instruction.result_index = dynamic_extent;
  instruction.operands.clear();
  ++statistics.folded_expressions;
  state[id.value()] = 2U;
  return true;
}

void visit_expression(const Program& program, const MirExpressionId id,
                      std::vector<bool>& reachable) {
  if (!id.valid() || id.value() >= program.expressions.size() || reachable[id.value()]) return;
  const auto& expression = program.expressions[id.value()];
  if (!expression.valid()) return;
  reachable[id.value()] = true;
  for (const auto child : expression.children) visit_expression(program, child, reachable);
}

std::vector<bool> reachable_expressions(const Program& program) {
  std::vector<bool> reachable(program.expressions.size(), false);
  std::unordered_map<ValueId, MirExpressionId> expression_by_value;
  std::vector<bool> expression_instructions(program.instructions.size(), false);
  for (std::size_t index = 1; index < program.expressions.size(); ++index) {
    const auto& expression = program.expressions[index];
    if (!expression.valid()) continue;
    expression_by_value.emplace(expression.value_id,
                                MirExpressionId{static_cast<MirExpressionId::value_type>(index)});
    if (expression.instruction.valid() &&
        expression.instruction.value() < expression_instructions.size()) {
      expression_instructions[expression.instruction.value()] = true;
    }
  }
  for (std::size_t index = 1; index < program.statements.size(); ++index) {
    const auto& statement = program.statements[index];
    visit_expression(program, statement.expression, reachable);
    visit_expression(program, statement.secondary_expression, reachable);
    visit_expression(program, statement.tertiary_expression, reachable);
    visit_expression(program, statement.target_expression, reachable);
    for (const auto expression : statement.parameter_defaults) {
      visit_expression(program, expression, reachable);
    }
    for (const auto& selector : statement.case_selectors) {
      visit_expression(program, selector.lower, reachable);
      visit_expression(program, selector.upper, reachable);
    }
  }
  const auto visit_value = [&](const ValueId value) {
    const auto found = expression_by_value.find(value);
    if (found != expression_by_value.end()) visit_expression(program, found->second, reachable);
  };
  for (std::size_t index = 1; index < program.instructions.size(); ++index) {
    if (expression_instructions[index]) continue;
    for (const auto operand : program.instructions[index].operands) visit_value(operand);
  }
  for (std::size_t index = 1; index < program.blocks.size(); ++index) {
    const auto& terminator = program.blocks[index].terminator;
    for (const auto operand : terminator.operands) visit_value(operand);
    for (const auto& edge : terminator.successor_arguments) {
      for (const auto operand : edge) visit_value(operand);
    }
  }
  return reachable;
}

bool pure_folded_instruction(const Opcode opcode) noexcept {
  return opcode == Opcode::literal || opcode == Opcode::unary || opcode == Opcode::binary;
}

void compact_instructions(Program& program, const std::vector<bool>& removed,
                          std::vector<Diagnostic>& diagnostics) {
  std::vector<InstructionId> remap(program.instructions.size());
  std::vector<Instruction> instructions;
  instructions.reserve(program.instructions.size());
  instructions.push_back({});
  std::vector<InstructionAttributes> instruction_attributes;
  instruction_attributes.reserve(program.attributes.instructions.size());
  instruction_attributes.push_back({});
  for (std::size_t index = 1; index < program.instructions.size(); ++index) {
    if (removed[index]) continue;
    auto instruction = std::move(program.instructions[index]);
    instruction.id = InstructionId{static_cast<InstructionId::value_type>(instructions.size())};
    remap[index] = instruction.id;
    auto attributes = std::move(program.attributes.instructions[index]);
    attributes.origin = instruction.id;
    instruction_attributes.push_back(std::move(attributes));
    instructions.push_back(std::move(instruction));
  }
  const auto remap_id = [&](InstructionId& id) {
    if (!id.valid()) return;
    if (id.value() >= remap.size() || !remap[id.value()].valid()) {
      add_error(diagnostics, "live node references a removed instruction");
      id = {};
      return;
    }
    id = remap[id.value()];
  };
  for (std::size_t index = 1; index < program.expressions.size(); ++index) {
    if (!program.expressions[index].retired) remap_id(program.expressions[index].instruction);
  }
  for (std::size_t index = 1; index < program.statements.size(); ++index) {
    remap_id(program.statements[index].instruction);
  }
  for (auto& call : program.calls) remap_id(call.instruction);
  for (std::size_t index = 1; index < program.blocks.size(); ++index) {
    auto& owned = program.blocks[index].instructions;
    owned.erase(std::remove_if(owned.begin(), owned.end(),
                               [&](const InstructionId id) {
                                 return id.valid() && id.value() < removed.size() &&
                                        removed[id.value()];
                               }),
                owned.end());
    for (auto& id : owned) remap_id(id);
  }
  program.instructions = std::move(instructions);
  program.attributes.instructions = std::move(instruction_attributes);
}

std::vector<Diagnostic> fold_constants_and_eliminate_dead_pure(Program& program,
                                                               OptimizationStatistics& statistics) {
  std::vector<std::uint8_t> fold_state(program.expressions.size(), 0U);
  for (std::size_t index = 1; index < program.expressions.size(); ++index) {
    if (program.expressions[index].valid()) {
      (void)fold_expression(program,
                            MirExpressionId{static_cast<MirExpressionId::value_type>(index)},
                            statistics, fold_state);
    }
  }
  const auto reachable = reachable_expressions(program);
  std::vector<bool> candidates(program.instructions.size(), false);
  std::unordered_set<ValueId> dead_values;
  for (std::size_t index = 1; index < program.expressions.size(); ++index) {
    const auto& expression = program.expressions[index];
    if (!expression.valid() || reachable[index]) continue;
    if (!expression.instruction.valid() ||
        expression.instruction.value() >= program.instructions.size() ||
        !pure_folded_instruction(program.instructions[expression.instruction.value()].opcode)) {
      return {{DiagnosticSeverity::error,
               "MPF0006",
               "MIR optimization failed: unreachable expression is not proven pure",
               {1, 1}}};
    }
    candidates[expression.instruction.value()] = true;
    dead_values.insert(expression.value_id);
  }
  const auto external_dead_use = [&](const ValueId value) {
    return dead_values.count(value) != 0U;
  };
  for (std::size_t index = 1; index < program.instructions.size(); ++index) {
    if (candidates[index]) continue;
    if (std::any_of(program.instructions[index].operands.begin(),
                    program.instructions[index].operands.end(), external_dead_use)) {
      return {{DiagnosticSeverity::error,
               "MPF0006",
               "MIR optimization failed: live instruction uses a retired expression value",
               {1, 1}}};
    }
  }
  for (std::size_t index = 1; index < program.blocks.size(); ++index) {
    const auto& terminator = program.blocks[index].terminator;
    if (std::any_of(terminator.operands.begin(), terminator.operands.end(), external_dead_use) ||
        std::any_of(terminator.successor_arguments.begin(), terminator.successor_arguments.end(),
                    [&](const auto& edge) {
                      return std::any_of(edge.begin(), edge.end(), external_dead_use);
                    })) {
      return {{DiagnosticSeverity::error,
               "MPF0006",
               "MIR optimization failed: control-flow edge uses a retired expression value",
               {1, 1}}};
    }
  }
  std::vector<bool> removed(program.instructions.size(), false);
  for (std::size_t index = 1; index < program.expressions.size(); ++index) {
    auto& expression = program.expressions[index];
    if (!expression.valid() || reachable[index]) continue;
    removed[expression.instruction.value()] = true;
    const auto id = expression.id;
    expression.kind = ExpressionKind::invalid;
    expression.instruction = {};
    expression.children.clear();
    expression.value_id = {};
    expression.type_id = {};
    expression.shape_id = {};
    expression.storage_id = {};
    expression.symbol_id = {};
    expression.retired = true;
    program.attributes.expressions[index] = {};
    program.attributes.expressions[index].origin = id;
    ++statistics.retired_expressions;
  }
  std::vector<Diagnostic> diagnostics;
  compact_instructions(program, removed, diagnostics);
  statistics.removed_instructions +=
      static_cast<std::size_t>(std::count(removed.begin(), removed.end(), true));
  return diagnostics;
}

void compact_blocks(Program& program, const std::vector<bool>& kept,
                    std::vector<Diagnostic>& diagnostics) {
  std::vector<BlockId> remap(program.blocks.size());
  std::vector<BasicBlock> blocks;
  blocks.reserve(program.blocks.size());
  blocks.push_back({});
  for (std::size_t index = 1; index < program.blocks.size(); ++index) {
    if (!kept[index]) continue;
    auto block = std::move(program.blocks[index]);
    block.id = BlockId{static_cast<BlockId::value_type>(blocks.size())};
    remap[index] = block.id;
    blocks.push_back(std::move(block));
  }
  const auto remap_id = [&](BlockId& id) {
    if (!id.valid()) return;
    if (id.value() >= remap.size() || !remap[id.value()].valid()) {
      add_error(diagnostics, "live control-flow node references a removed block");
      id = {};
      return;
    }
    id = remap[id.value()];
  };
  for (std::size_t index = 1; index < blocks.size(); ++index) {
    for (auto& successor : blocks[index].terminator.successors) remap_id(successor);
  }
  for (std::size_t index = 1; index < program.functions.size(); ++index) {
    remap_id(program.functions[index].entry);
    for (auto& block : program.functions[index].blocks) remap_id(block);
  }
  program.blocks = std::move(blocks);
}

std::vector<Diagnostic> cleanup_cfg(Program& program, OptimizationStatistics& statistics) {
  std::vector<bool> kept(program.blocks.size(), true);
  if (!kept.empty()) kept[0] = true;
  bool changed = true;
  while (changed) {
    changed = false;
    std::vector<std::size_t> predecessors(program.blocks.size(), 0U);
    std::vector<bool> entry(program.blocks.size(), false);
    for (std::size_t index = 1; index < program.functions.size(); ++index) {
      if (program.functions[index].entry.valid())
        entry[program.functions[index].entry.value()] = true;
    }
    for (std::size_t index = 1; index < program.blocks.size(); ++index) {
      if (!kept[index]) continue;
      for (const auto successor : program.blocks[index].terminator.successors) {
        if (successor.valid() && successor.value() < predecessors.size() &&
            kept[successor.value()]) {
          ++predecessors[successor.value()];
        }
      }
    }
    for (std::size_t index = 1; index < program.blocks.size(); ++index) {
      if (!kept[index] || entry[index]) continue;
      const auto& block = program.blocks[index];
      const bool unreachable_empty = predecessors[index] == 0U && block.arguments.empty() &&
                                     block.instructions.empty() &&
                                     block.terminator.kind == TerminatorKind::unreachable;
      const bool forwarding =
          block.arguments.empty() && block.instructions.empty() &&
          block.terminator.kind == TerminatorKind::branch &&
          block.terminator.successors.size() == 1U &&
          block.terminator.successor_arguments.size() == 1U &&
          block.terminator.successor_arguments.front().empty() &&
          block.terminator.successors.front().value() != index &&
          kept[block.terminator.successors.front().value()] &&
          program.blocks[block.terminator.successors.front().value()].arguments.empty();
      if (!unreachable_empty && !forwarding) continue;
      if (forwarding) {
        const auto target = block.terminator.successors.front();
        for (std::size_t predecessor = 1; predecessor < program.blocks.size(); ++predecessor) {
          if (!kept[predecessor] || predecessor == index) continue;
          for (auto& successor : program.blocks[predecessor].terminator.successors) {
            if (successor.value() == index) successor = target;
          }
        }
      }
      kept[index] = false;
      for (std::size_t function = 1; function < program.functions.size(); ++function) {
        auto& blocks = program.functions[function].blocks;
        blocks.erase(std::remove(blocks.begin(), blocks.end(), block.id), blocks.end());
      }
      ++statistics.removed_blocks;
      changed = true;
      break;
    }
  }
  std::vector<Diagnostic> diagnostics;
  compact_blocks(program, kept, diagnostics);
  return diagnostics;
}

}  // namespace

OptimizationResult run_default_optimization_pipeline(Program& program) {
  OptimizationResult result;
  result.statistics.instructions_before =
      program.instructions.size() - (program.instructions.empty() ? 0U : 1U);
  result.statistics.blocks_before = program.blocks.size() - (program.blocks.empty() ? 0U : 1U);
  PassManager<Program> passes(&verify);
  passes.add({"mir-shape-canonicalization",
              [&](Program& ir) { return canonicalize_shapes(ir, result.statistics); },
              true,
              {},
              true,
              true,
              &synchronize_revision});
  passes.add({"mir-copy-propagation",
              [&](Program& ir) { return propagate_block_arguments(ir, result.statistics); },
              true,
              {},
              true,
              true,
              &synchronize_revision});
  passes.add(
      {"mir-constant-folding-dce",
       [&](Program& ir) { return fold_constants_and_eliminate_dead_pure(ir, result.statistics); },
       true,
       {},
       true,
       true,
       &synchronize_revision});
  passes.add({"mir-cfg-cleanup",
              [&](Program& ir) { return cleanup_cfg(ir, result.statistics); },
              true,
              {},
              true,
              true,
              &synchronize_revision});
  result.diagnostics = passes.run(program);
  result.instrumentation = passes.instrumentation();
  result.statistics.instructions_after =
      program.instructions.size() - (program.instructions.empty() ? 0U : 1U);
  result.statistics.blocks_after = program.blocks.size() - (program.blocks.empty() ? 0U : 1U);
  return result;
}

}  // namespace mpf::detail::mir
