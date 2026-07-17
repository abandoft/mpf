#include "semantic_table.hpp"

#include <algorithm>
#include <optional>
#include <string>
#include <utility>

namespace mpf::detail::hir {
namespace {

template <typename Facts>
bool valid_offset(const std::uint32_t offset, const std::vector<Facts>& facts) noexcept {
  return static_cast<std::size_t>(offset) < facts.size();
}

void add_error(std::vector<Diagnostic>& diagnostics, const SourceLocation location,
               const std::string_view stage, std::string message) {
  diagnostics.push_back(
      {DiagnosticSeverity::error, "MPF0005",
       "invalid semantic table at '" + std::string(stage) + "': " + std::move(message), location});
}

semantic::MatrixOperation matrix_operation_for_operator(const BinaryOperator operation) noexcept {
  switch (operation) {
    case BinaryOperator::multiply: return semantic::MatrixOperation::multiply;
    case BinaryOperator::left_divide: return semantic::MatrixOperation::left_divide;
    case BinaryOperator::divide: return semantic::MatrixOperation::right_divide;
    case BinaryOperator::power: return semantic::MatrixOperation::integer_power;
    default: return semantic::MatrixOperation::none;
  }
}

semantic::MatrixOperation expected_matrix_operation(const Expression& expression,
                                                    const ExpressionFacts& facts,
                                                    const SemanticTable& table) noexcept {
  if (facts.array_operation != semantic::ArrayOperation::matlab) {
    return semantic::MatrixOperation::none;
  }
  if (expression.children.size() != 2U) return semantic::MatrixOperation::none;
  const auto* left = table.expression(expression.children[0].id);
  const auto* right = table.expression(expression.children[1].id);
  if (left == nullptr || right == nullptr) return semantic::MatrixOperation::none;
  const bool left_array = left->inferred_type == ValueType::list;
  const bool right_array = right->inferred_type == ValueType::list;
  if (expression.operation == BinaryOperator::multiply && left_array && right_array) {
    return semantic::MatrixOperation::multiply;
  }
  if (expression.operation == BinaryOperator::left_divide && left_array && right_array) {
    return semantic::MatrixOperation::left_divide;
  }
  if (expression.operation == BinaryOperator::divide && left_array && right_array) {
    return semantic::MatrixOperation::right_divide;
  }
  if (expression.operation == BinaryOperator::power && left_array && !right_array) {
    return semantic::MatrixOperation::integer_power;
  }
  return semantic::MatrixOperation::none;
}

bool static_rank_two(const std::vector<std::size_t>& shape) noexcept {
  return shape.size() == 2U && std::find(shape.begin(), shape.end(), dynamic_extent) == shape.end();
}

std::optional<semantic::IndexSelectorKind> expected_index_selector(
    const Expression& selector, const SemanticTable& table) noexcept {
  if (selector.kind == ExpressionKind::slice) return semantic::IndexSelectorKind::slice;
  const auto* facts = table.expression(selector.id);
  if (facts == nullptr || facts->inferred_type == ValueType::unknown) return std::nullopt;
  if (facts->inferred_type != ValueType::list) return semantic::IndexSelectorKind::scalar;
  if (std::find(facts->shape.begin(), facts->shape.end(), 0U) != facts->shape.end()) {
    return semantic::IndexSelectorKind::empty;
  }
  if (facts->element_type == ValueType::boolean) return semantic::IndexSelectorKind::logical;
  if (facts->element_type == ValueType::integer || facts->element_type == ValueType::real) {
    return semantic::IndexSelectorKind::numeric;
  }
  return std::nullopt;
}

void collect_index_extent(const Expression& expression, const SemanticTable& table,
                          std::optional<semantic::IndexExtentSource>& source, bool& valid) {
  const auto* facts = table.expression(expression.id);
  if (expression.kind == ExpressionKind::end_index) {
    if (facts == nullptr || !semantic::requires_runtime_extent(facts->index_extent) ||
        (source.has_value() && *source != facts->index_extent)) {
      valid = false;
      return;
    }
    source = facts->index_extent;
    return;
  }
  for (const auto& child : expression.children) {
    collect_index_extent(child, table, source, valid);
  }
}

bool valid_matrix_shapes(const MatrixOperationPlan& plan) noexcept {
  if (!static_rank_two(plan.left_shape) || !static_rank_two(plan.result_shape)) return false;
  switch (plan.operation) {
    case semantic::MatrixOperation::none: return false;
    case semantic::MatrixOperation::multiply:
      return plan.solve == semantic::MatrixSolveKind::none && static_rank_two(plan.right_shape) &&
             plan.left_shape[1] == plan.right_shape[0] &&
             plan.result_shape[0] == plan.left_shape[0] &&
             plan.result_shape[1] == plan.right_shape[1];
    case semantic::MatrixOperation::left_divide:
      return static_rank_two(plan.right_shape) && plan.left_shape[0] == plan.right_shape[0] &&
             plan.solve == semantic::matrix_solve_kind(plan.left_shape[0], plan.left_shape[1]) &&
             plan.result_shape[0] == plan.left_shape[1] &&
             plan.result_shape[1] == plan.right_shape[1];
    case semantic::MatrixOperation::right_divide:
      return static_rank_two(plan.right_shape) && plan.left_shape[1] == plan.right_shape[1] &&
             plan.solve == semantic::matrix_solve_kind(plan.right_shape[1], plan.right_shape[0]) &&
             plan.result_shape[0] == plan.left_shape[0] &&
             plan.result_shape[1] == plan.right_shape[0];
    case semantic::MatrixOperation::integer_power:
      return plan.solve == semantic::MatrixSolveKind::none && plan.right_shape.empty() &&
             plan.left_shape[0] == plan.left_shape[1] && plan.result_shape == plan.left_shape;
  }
  return false;
}

void verify_expression(const Expression& expression, const SemanticTable& table,
                       std::vector<bool>& seen, const std::string_view stage,
                       std::vector<Diagnostic>& diagnostics) {
  if (!expression.valid()) return;
  const auto* facts = table.expression(expression.id);
  if (facts == nullptr || facts->origin != expression.id) {
    add_error(diagnostics, expression.location, stage,
              "expression fact is missing or has the wrong origin");
    return;
  }
  seen[expression.id.value()] = true;
  const bool analyzed = stage != "ast-to-hir" && stage != "frontend-seed" && stage != "conformance";
  if (facts->requested_outputs == 0) {
    add_error(diagnostics, expression.location, stage, "expression requests zero outputs");
  }
  if (analyzed && (expression.kind == ExpressionKind::end_index) !=
                      semantic::requires_runtime_extent(facts->index_extent)) {
    add_error(diagnostics, expression.location, stage,
              "runtime index-extent fact has invalid expression ownership");
  }
  if (facts->array_operation == semantic::ArrayOperation::matlab &&
      expression.kind != ExpressionKind::binary) {
    add_error(diagnostics, expression.location, stage,
              "Matlab array-operation facts require a binary expression");
  }
  if (facts->broadcast.valid) {
    const auto& broadcast = facts->broadcast;
    const auto rank = broadcast.axes.size();
    const bool runtime = broadcast.shape_source == semantic::BroadcastShapeSource::runtime_operands;
    const bool valid_unknown_rank = runtime && rank == 0U && broadcast.left_shape.empty() &&
                                    broadcast.right_shape.empty() && broadcast.result_shape.empty();
    if (facts->array_operation != semantic::ArrayOperation::matlab ||
        expression.kind != ExpressionKind::binary || (!valid_unknown_rank && rank == 0U) ||
        broadcast.left_shape.size() != rank || broadcast.right_shape.size() != rank ||
        broadcast.result_shape.size() != rank || facts->shape != broadcast.result_shape) {
      add_error(diagnostics, expression.location, stage,
                "expression broadcast plan has an invalid kind, arity, or result shape");
    } else {
      bool has_runtime_axis = false;
      bool valid_axes = true;
      for (std::size_t axis = 0; axis < rank; ++axis) {
        const auto left = broadcast.left_shape[axis];
        const auto right = broadcast.right_shape[axis];
        const auto result = broadcast.result_shape[axis];
        const auto mode = broadcast.axes[axis];
        const auto known = left == dynamic_extent ? right : left;
        const auto runtime_result = known == dynamic_extent || known == 1U ? dynamic_extent : known;
        valid_axes =
            valid_axes &&
            ((mode == semantic::BroadcastAxis::match && left == right && result == left) ||
             (mode == semantic::BroadcastAxis::expand_left && left == 1U && result == right) ||
             (mode == semantic::BroadcastAxis::expand_right && right == 1U && result == left) ||
             (runtime && mode == semantic::BroadcastAxis::runtime &&
              (left == dynamic_extent || right == dynamic_extent) && result == runtime_result));
        has_runtime_axis = has_runtime_axis || mode == semantic::BroadcastAxis::runtime;
      }
      if (!valid_axes || (rank != 0U && runtime != has_runtime_axis)) {
        add_error(diagnostics, expression.location, stage,
                  "expression broadcast axes disagree with their shape-source contract");
      }
    }
  } else if (facts->broadcast.shape_source != semantic::BroadcastShapeSource::static_extents) {
    add_error(diagnostics, expression.location, stage,
              "inactive expression broadcast plan retains a runtime shape source");
  }
  const auto& matrix = facts->matrix_operation;
  const auto expected_matrix = expected_matrix_operation(expression, *facts, table);
  if (matrix.operation != expected_matrix) {
    add_error(diagnostics, expression.location, stage,
              "matrix-operation identity disagrees with the typed operands");
  } else if (matrix.valid()) {
    if (facts->array_operation != semantic::ArrayOperation::matlab ||
        expression.kind != ExpressionKind::binary || facts->broadcast.valid ||
        matrix.operation != matrix_operation_for_operator(expression.operation) ||
        facts->shape != matrix.result_shape || !valid_matrix_shapes(matrix)) {
      add_error(diagnostics, expression.location, stage,
                "matrix-operation plan has an invalid operator, shape, or result contract");
    }
  } else if (matrix.solve != semantic::MatrixSolveKind::none || !matrix.left_shape.empty() ||
             !matrix.right_shape.empty() || !matrix.result_shape.empty()) {
    add_error(diagnostics, expression.location, stage,
              "inactive matrix-operation plan retains shape facts");
  }
  if (expression.kind == ExpressionKind::index) {
    if (facts->index_selectors.size() + 1U != expression.children.size() ||
        facts->index_extents.size() != facts->index_selectors.size()) {
      add_error(diagnostics, expression.location, stage,
                "index selector or extent facts disagree with the expression arity");
    } else {
      for (std::size_t index = 0; index < facts->index_selectors.size(); ++index) {
        const auto expected = expected_index_selector(expression.children[index + 1U], table);
        if (expected.has_value() && *expected != facts->index_selectors[index]) {
          add_error(diagnostics, expression.location, stage,
                    "index selector kind disagrees with the source expression");
          break;
        }
        if (analyzed) {
          std::optional<semantic::IndexExtentSource> extent;
          bool valid_extent = true;
          collect_index_extent(expression.children[index + 1U], table, extent, valid_extent);
          const auto expected_extent = extent.value_or(semantic::IndexExtentSource::none);
          if (!valid_extent || facts->index_extents[index] != expected_extent) {
            add_error(diagnostics, expression.location, stage,
                      "index runtime-extent plan disagrees with the selector expression");
            break;
          }
        }
      }
    }
  } else if (!facts->index_selectors.empty() || !facts->index_extents.empty()) {
    add_error(diagnostics, expression.location, stage,
              "non-index expression retains selector or extent facts");
  }
  if (!valid_storage_region(facts->storage_region) ||
      (facts->storage_region.kind != StorageRegionKind::unknown &&
       expression.kind != ExpressionKind::identifier && expression.kind != ExpressionKind::index &&
       expression.kind != ExpressionKind::slice)) {
    add_error(diagnostics, expression.location, stage,
              "expression storage-region fact is invalid for its expression kind");
  }
  if (facts->tuple_types.size() != facts->tuple_element_types.size() ||
      facts->tuple_types.size() != facts->tuple_shapes.size()) {
    add_error(diagnostics, expression.location, stage,
              "tuple type, element-type, and shape arities disagree");
  }
  if (expression.kind == ExpressionKind::call && !facts->argument_names.empty() &&
      facts->argument_names.size() + 1U != expression.children.size()) {
    add_error(diagnostics, expression.location, stage,
              "normalized call argument-name arity disagrees with HIR");
  }
  for (const auto& child : expression.children) {
    verify_expression(child, table, seen, stage, diagnostics);
  }
}

bool compatible_arity(const std::size_t size, const std::size_t expected) noexcept {
  return size == 0 || size == expected;
}

void verify_statements(const std::vector<Statement>& statements, const SemanticTable& table,
                       std::vector<bool>& seen, const std::string_view stage,
                       std::vector<Diagnostic>& diagnostics) {
  for (const auto& statement : statements) {
    const auto* facts = table.statement(statement.id);
    if (facts == nullptr || facts->origin != statement.id) {
      add_error(diagnostics, {statement.line, 1}, stage,
                "statement fact is missing or has the wrong origin");
      continue;
    }
    seen[statement.id.value()] = true;
    const auto parameters = statement.parameters.size();
    if (facts->exported && statement.kind != StatementKind::function) {
      add_error(diagnostics, {statement.line, 1}, stage,
                "only a function statement may carry the exported semantic fact");
    }
    if (!compatible_arity(facts->parameter_intents.size(), parameters) ||
        !compatible_arity(facts->parameter_optional.size(), parameters) ||
        !compatible_arity(facts->parameter_types.size(), parameters) ||
        !compatible_arity(facts->parameter_element_types.size(), parameters) ||
        !compatible_arity(facts->parameter_shapes.size(), parameters)) {
      add_error(diagnostics, {statement.line, 1}, stage,
                "function parameter semantic arity disagrees with HIR");
    }
    const auto returns = statement.return_names.size();
    if (returns != 0 && (!compatible_arity(facts->return_types.size(), returns) ||
                         !compatible_arity(facts->return_element_types.size(), returns) ||
                         !compatible_arity(facts->return_shapes.size(), returns))) {
      add_error(diagnostics, {statement.line, 1}, stage,
                "function result semantic arity disagrees with HIR");
    }
    const auto targets = statement.target_names.size();
    if (statement.has_target_pattern && !facts->target_pattern.valid()) {
      add_error(diagnostics, {statement.line, 1}, stage,
                "analyzed assignment pattern is missing from the semantic table");
    }
    if (!compatible_arity(facts->target_types.size(), targets) ||
        !compatible_arity(facts->target_element_types.size(), targets) ||
        !compatible_arity(facts->target_shapes.size(), targets) ||
        !compatible_arity(facts->target_previous_types.size(), targets) ||
        !compatible_arity(facts->target_previous_element_types.size(), targets)) {
      add_error(diagnostics, {statement.line, 1}, stage,
                "assignment target semantic arity disagrees with HIR");
    }
    verify_expression(statement.expression, table, seen, stage, diagnostics);
    verify_expression(statement.secondary_expression, table, seen, stage, diagnostics);
    verify_expression(statement.tertiary_expression, table, seen, stage, diagnostics);
    verify_expression(statement.target_expression, table, seen, stage, diagnostics);
    for (const auto& expression : statement.parameter_defaults) {
      verify_expression(expression, table, seen, stage, diagnostics);
    }
    for (const auto& selector : statement.case_selectors) {
      verify_expression(selector.lower, table, seen, stage, diagnostics);
      verify_expression(selector.upper, table, seen, stage, diagnostics);
    }
    verify_statements(statement.body, table, seen, stage, diagnostics);
    verify_statements(statement.alternative, table, seen, stage, diagnostics);
  }
}

void reindex_expression(Expression& expression, SemanticTable& source, SemanticTable& result,
                        IrIdAllocator<HirNodeId>& ids) {
  if (!expression.valid()) {
    expression.id = {};
    return;
  }
  const auto old_id = expression.id;
  auto facts = source.expression(old_id) == nullptr ? ExpressionFacts{}
                                                    : std::move(*source.expression(old_id));
  expression.id = ids.next();
  facts.origin = expression.id;
  const auto offset = result.expressions.size();
  result.nodes.push_back({SemanticNodeKind::expression, static_cast<std::uint32_t>(offset)});
  result.expressions.push_back(std::move(facts));
  for (auto& child : expression.children) reindex_expression(child, source, result, ids);
}

void reindex_statements(std::vector<Statement>& statements, SemanticTable& source,
                        SemanticTable& result, IrIdAllocator<HirNodeId>& ids) {
  for (auto& statement : statements) {
    const auto old_id = statement.id;
    auto facts = source.statement(old_id) == nullptr ? StatementFacts{}
                                                     : std::move(*source.statement(old_id));
    statement.id = ids.next();
    facts.origin = statement.id;
    const auto offset = result.statements.size();
    result.nodes.push_back({SemanticNodeKind::statement, static_cast<std::uint32_t>(offset)});
    result.statements.push_back(std::move(facts));
    reindex_expression(statement.expression, source, result, ids);
    reindex_expression(statement.secondary_expression, source, result, ids);
    reindex_expression(statement.tertiary_expression, source, result, ids);
    reindex_expression(statement.target_expression, source, result, ids);
    for (auto& expression : statement.parameter_defaults) {
      reindex_expression(expression, source, result, ids);
    }
    for (auto& selector : statement.case_selectors) {
      reindex_expression(selector.lower, source, result, ids);
      reindex_expression(selector.upper, source, result, ids);
    }
    reindex_statements(statement.body, source, result, ids);
    reindex_statements(statement.alternative, source, result, ids);
  }
}

}  // namespace

const ExpressionFacts* SemanticTable::expression(const HirNodeId id) const noexcept {
  if (!id.valid() || id.value() >= nodes.size()) return nullptr;
  const auto slot = nodes[id.value()];
  if (slot.kind != SemanticNodeKind::expression || !valid_offset(slot.offset, expressions)) {
    return nullptr;
  }
  return &expressions[slot.offset];
}

ExpressionFacts* SemanticTable::expression(const HirNodeId id) noexcept {
  return const_cast<ExpressionFacts*>(std::as_const(*this).expression(id));
}

const StatementFacts* SemanticTable::statement(const HirNodeId id) const noexcept {
  if (!id.valid() || id.value() >= nodes.size()) return nullptr;
  const auto slot = nodes[id.value()];
  if (slot.kind != SemanticNodeKind::statement || !valid_offset(slot.offset, statements)) {
    return nullptr;
  }
  return &statements[slot.offset];
}

StatementFacts* SemanticTable::statement(const HirNodeId id) noexcept {
  return const_cast<StatementFacts*>(std::as_const(*this).statement(id));
}

SemanticTable reindex_semantics(Program& program, SemanticTable&& table) {
  SemanticTable result;
  result.hir_revision = program.revision;
  result.nodes.push_back({});
  result.expressions.reserve(table.expressions.size());
  result.statements.reserve(table.statements.size());
  IrIdAllocator<HirNodeId> ids;
  reindex_statements(program.statements, table, result, ids);
  program.node_count = ids.count();
  result.hir_node_count = program.node_count;
  return result;
}

std::vector<Diagnostic> verify_semantics(const Program& program, const SemanticTable& table,
                                         const std::string_view stage) {
  std::vector<Diagnostic> diagnostics;
  if (table.hir_revision != program.revision) {
    add_error(diagnostics, {1, 1}, stage, "HIR revision is stale");
  }
  if (table.hir_node_count != program.node_count || table.nodes.size() != program.node_count + 1U) {
    add_error(diagnostics, {1, 1}, stage, "dense node index does not cover the HIR ID space");
    return diagnostics;
  }
  if (!table.nodes.empty() && table.nodes.front().kind != SemanticNodeKind::absent) {
    add_error(diagnostics, {1, 1}, stage, "node ID zero is not the required sentinel");
  }
  if (table.expressions.size() + table.statements.size() != program.node_count) {
    add_error(diagnostics, {1, 1}, stage,
              "expression and statement fact inventories do not match HIR node count");
  }
  std::vector<bool> expression_offsets(table.expressions.size(), false);
  std::vector<bool> statement_offsets(table.statements.size(), false);
  for (std::size_t index = 1; index < table.nodes.size(); ++index) {
    const auto slot = table.nodes[index];
    const auto origin = HirNodeId{static_cast<HirNodeId::value_type>(index)};
    if (slot.kind == SemanticNodeKind::expression) {
      if (!valid_offset(slot.offset, table.expressions) || expression_offsets[slot.offset] ||
          table.expressions[slot.offset].origin != origin) {
        add_error(diagnostics, {1, 1}, stage,
                  "expression fact offset is invalid, duplicated, or has the wrong origin");
      } else {
        expression_offsets[slot.offset] = true;
      }
    } else if (slot.kind == SemanticNodeKind::statement) {
      if (!valid_offset(slot.offset, table.statements) || statement_offsets[slot.offset] ||
          table.statements[slot.offset].origin != origin) {
        add_error(diagnostics, {1, 1}, stage,
                  "statement fact offset is invalid, duplicated, or has the wrong origin");
      } else {
        statement_offsets[slot.offset] = true;
      }
    } else {
      add_error(diagnostics, {1, 1}, stage, "nonzero HIR node has no semantic fact kind");
    }
  }
  std::vector<bool> seen(program.node_count + 1U, false);
  verify_statements(program.statements, table, seen, stage, diagnostics);
  for (std::size_t index = 1; index < seen.size(); ++index) {
    if (!seen[index] || table.nodes[index].kind == SemanticNodeKind::absent) {
      add_error(diagnostics, {1, 1}, stage,
                "semantic node ID space has missing or unreachable entries");
      break;
    }
  }
  return diagnostics;
}

}  // namespace mpf::detail::hir
