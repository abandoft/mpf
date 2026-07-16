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
  diagnostics.push_back(
      {DiagnosticSeverity::error, "MPF0005",
       "invalid semantic table at '" + std::string(stage) + "': " + std::move(message), location});
}

void add_expression(const Expression& expression, SemanticTable& result) {
  if (!expression.valid()) return;
  const auto offset = result.expressions.size();
  if (offset > std::numeric_limits<std::uint32_t>::max()) return;
  result.nodes[expression.id.value()] = {SemanticNodeKind::expression,
                                         static_cast<std::uint32_t>(offset)};
  ExpressionFacts facts;
  facts.origin = expression.id;
  facts.inferred_type = expression.inferred_type;
  facts.binding = expression.binding;
  facts.intrinsic = expression.intrinsic;
  facts.element_type = expression.element_type;
  facts.shape = expression.shape;
  facts.tuple_types = expression.tuple_types;
  facts.tuple_element_types = expression.tuple_element_types;
  facts.tuple_shapes = expression.tuple_shapes;
  facts.sequence_is_list = expression.sequence_is_list;
  facts.sequence_elements = expression.sequence_elements;
  facts.requested_outputs = expression.requested_outputs;
  facts.multi_output_call = expression.multi_output_call;
  facts.argument_intents = expression.argument_intents;
  facts.argument_names = expression.argument_names;
  facts.argument_optional_forward = expression.argument_optional_forward;
  facts.procedure_has_result = expression.procedure_has_result;
  facts.index_base = expression.index_base;
  facts.allow_negative_index = expression.allow_negative_index;
  facts.column_major = expression.column_major;
  facts.slice_stop_inclusive = expression.slice_stop_inclusive;
  result.expressions.push_back(std::move(facts));
  for (const auto& child : expression.children) add_expression(child, result);
}

void add_statement(const Statement& statement, SemanticTable& result) {
  const auto offset = result.statements.size();
  if (offset > std::numeric_limits<std::uint32_t>::max()) return;
  result.nodes[statement.id.value()] = {SemanticNodeKind::statement,
                                        static_cast<std::uint32_t>(offset)};
  StatementFacts facts;
  facts.origin = statement.id;
  facts.declared_type = statement.declared_type;
  facts.element_type = statement.element_type;
  facts.previous_type = statement.previous_type;
  facts.previous_element_type = statement.previous_element_type;
  facts.parameter_intent = statement.parameter_intent;
  facts.optional_parameter = statement.optional_parameter;
  facts.dummy_parameter = statement.dummy_parameter;
  facts.shape = statement.shape;
  facts.index_base = statement.index_base;
  facts.allow_negative_index = statement.allow_negative_index;
  facts.parameter_intents = statement.parameter_intents;
  facts.parameter_optional = statement.parameter_optional;
  facts.parameter_types = statement.parameter_types;
  facts.parameter_element_types = statement.parameter_element_types;
  facts.parameter_shapes = statement.parameter_shapes;
  facts.has_value_return = statement.has_value_return;
  facts.return_types = statement.return_types;
  facts.return_element_types = statement.return_element_types;
  facts.return_shapes = statement.return_shapes;
  facts.return_sequence_is_list = statement.return_sequence_is_list;
  facts.return_sequence_elements = statement.return_sequence_elements;
  facts.target_pattern = statement.target_pattern;
  facts.target_types = statement.target_types;
  facts.target_element_types = statement.target_element_types;
  facts.target_shapes = statement.target_shapes;
  facts.target_previous_types = statement.target_previous_types;
  facts.target_previous_element_types = statement.target_previous_element_types;
  result.statements.push_back(std::move(facts));

  add_expression(statement.expression, result);
  add_expression(statement.secondary_expression, result);
  add_expression(statement.tertiary_expression, result);
  add_expression(statement.target_expression, result);
  for (const auto& expression : statement.parameter_defaults) add_expression(expression, result);
  for (const auto& selector : statement.case_selectors) {
    add_expression(selector.lower, result);
    add_expression(selector.upper, result);
  }
  for (const auto& child : statement.body) add_statement(child, result);
  for (const auto& child : statement.alternative) add_statement(child, result);
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

SemanticTable initialize_semantics(const Program& program) {
  SemanticTable result;
  result.hir_revision = program.revision;
  result.hir_node_count = program.node_count;
  result.nodes.resize(program.node_count + 1U);
  result.expressions.reserve(program.node_count);
  result.statements.reserve(program.statements.size());
  for (const auto& statement : program.statements) add_statement(statement, result);
  return result;
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
