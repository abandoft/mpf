#include "semantic_table.hpp"

#include <limits>
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
  diagnostics.push_back({DiagnosticSeverity::error, "MPF0005",
                         "invalid semantic table at '" + std::string(stage) + "': " +
                             std::move(message),
                         location});
}

void add_expression(Program& program, Expression& expression, SemanticTable& result) {
  if (!expression.valid()) return;
  const auto offset = result.expressions.size();
  if (offset > std::numeric_limits<std::uint32_t>::max()) return;
  result.nodes[expression.id.value()] =
      {SemanticNodeKind::expression, static_cast<std::uint32_t>(offset)};
  ExpressionFacts facts;
  facts.origin = expression.id;
  facts.inferred_type = std::exchange(expression.inferred_type, ValueType::unknown);
  facts.binding = std::exchange(expression.binding, BindingKind::unresolved);
  facts.intrinsic = std::exchange(expression.intrinsic, IntrinsicId::none);
  facts.element_type = std::exchange(expression.element_type, ValueType::unknown);
  facts.shape = std::move(expression.shape);
  facts.tuple_types = std::move(expression.tuple_types);
  facts.tuple_element_types = std::move(expression.tuple_element_types);
  facts.tuple_shapes = std::move(expression.tuple_shapes);
  facts.sequence_is_list = std::exchange(expression.sequence_is_list, false);
  facts.sequence_elements = std::move(expression.sequence_elements);
  facts.requested_outputs = std::exchange(expression.requested_outputs, 1U);
  facts.multi_output_call = std::exchange(expression.multi_output_call, false);
  facts.argument_intents = std::move(expression.argument_intents);
  facts.argument_optional_forward = std::move(expression.argument_optional_forward);
  facts.procedure_has_result = std::exchange(expression.procedure_has_result, false);
  facts.index_base = std::exchange(expression.index_base, 0U);
  facts.allow_negative_index = std::exchange(expression.allow_negative_index, false);
  facts.column_major = std::exchange(expression.column_major, false);
  facts.slice_stop_inclusive = std::exchange(expression.slice_stop_inclusive, false);
  result.expressions.push_back(std::move(facts));
  for (auto& child : expression.children) add_expression(program, child, result);
}

void add_statement(Program& program, Statement& statement, SemanticTable& result) {
  const auto offset = result.statements.size();
  if (offset > std::numeric_limits<std::uint32_t>::max()) return;
  result.nodes[statement.id.value()] =
      {SemanticNodeKind::statement, static_cast<std::uint32_t>(offset)};
  StatementFacts facts;
  facts.origin = statement.id;
  facts.declared_type = std::exchange(statement.declared_type, ValueType::unknown);
  facts.element_type = std::exchange(statement.element_type, ValueType::unknown);
  facts.previous_type = std::exchange(statement.previous_type, ValueType::unknown);
  facts.previous_element_type =
      std::exchange(statement.previous_element_type, ValueType::unknown);
  facts.parameter_intent = std::exchange(statement.parameter_intent, ParameterIntent::none);
  facts.optional_parameter = std::exchange(statement.optional_parameter, false);
  facts.dummy_parameter = std::exchange(statement.dummy_parameter, false);
  facts.shape = std::move(statement.shape);
  facts.index_base = std::exchange(statement.index_base, 0U);
  facts.allow_negative_index = std::exchange(statement.allow_negative_index, false);
  facts.parameter_intents = std::move(statement.parameter_intents);
  facts.parameter_optional = std::move(statement.parameter_optional);
  facts.parameter_types = std::move(statement.parameter_types);
  facts.parameter_element_types = std::move(statement.parameter_element_types);
  facts.parameter_shapes = std::move(statement.parameter_shapes);
  facts.has_value_return = std::exchange(statement.has_value_return, false);
  facts.return_types = std::move(statement.return_types);
  facts.return_element_types = std::move(statement.return_element_types);
  facts.return_shapes = std::move(statement.return_shapes);
  facts.return_sequence_is_list = std::exchange(statement.return_sequence_is_list, false);
  facts.return_sequence_elements = std::move(statement.return_sequence_elements);
  facts.target_types = std::move(statement.target_types);
  facts.target_element_types = std::move(statement.target_element_types);
  facts.target_shapes = std::move(statement.target_shapes);
  facts.target_previous_types = std::move(statement.target_previous_types);
  facts.target_previous_element_types = std::move(statement.target_previous_element_types);
  result.statements.push_back(std::move(facts));

  add_expression(program, statement.expression, result);
  add_expression(program, statement.secondary_expression, result);
  add_expression(program, statement.tertiary_expression, result);
  add_expression(program, statement.target_expression, result);
  for (auto& expression : statement.parameter_defaults) add_expression(program, expression, result);
  for (auto& selector : statement.case_selectors) {
    add_expression(program, selector.lower, result);
    add_expression(program, selector.upper, result);
  }
  for (auto& child : statement.body) add_statement(program, child, result);
  for (auto& child : statement.alternative) add_statement(program, child, result);
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
  if (facts->requested_outputs == 0) {
    add_error(diagnostics, expression.location, stage, "expression requests zero outputs");
  }
  if (facts->tuple_types.size() != facts->tuple_element_types.size() ||
      facts->tuple_types.size() != facts->tuple_shapes.size()) {
    add_error(diagnostics, expression.location, stage,
              "tuple type, element-type, and shape arities disagree");
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
    if (!compatible_arity(facts->parameter_intents.size(), parameters) ||
        !compatible_arity(facts->parameter_optional.size(), parameters) ||
        !compatible_arity(facts->parameter_types.size(), parameters) ||
        !compatible_arity(facts->parameter_element_types.size(), parameters) ||
        !compatible_arity(facts->parameter_shapes.size(), parameters)) {
      add_error(diagnostics, {statement.line, 1}, stage,
                "function parameter semantic arity disagrees with HIR");
    }
    const auto returns = statement.return_names.size();
    if (returns != 0 &&
        (!compatible_arity(facts->return_types.size(), returns) ||
         !compatible_arity(facts->return_element_types.size(), returns) ||
         !compatible_arity(facts->return_shapes.size(), returns))) {
      add_error(diagnostics, {statement.line, 1}, stage,
                "function result semantic arity disagrees with HIR");
    }
    const auto targets = statement.target_names.size();
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

SemanticTable extract_semantics(Program& program) {
  SemanticTable result;
  result.hir_revision = program.revision;
  result.hir_node_count = program.node_count;
  result.nodes.resize(program.node_count + 1U);
  result.expressions.reserve(program.node_count);
  result.statements.reserve(program.statements.size());
  for (auto& statement : program.statements) add_statement(program, statement, result);
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
