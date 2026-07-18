#include "frontends/common/ast.hpp"

#include <algorithm>
#include <iterator>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>

#include "ir/semantic_table.hpp"

namespace mpf::detail {
namespace {

semantic::Profile semantic_profile(const SourceLanguage language) noexcept {
  semantic::Profile profile;
  if (language == SourceLanguage::python) {
    profile.truthiness = semantic::Truthiness::dynamic;
    profile.logical_result = semantic::LogicalResult::operand;
    profile.equality = semantic::Equality::structural;
    profile.division = semantic::Division::real_quotient;
    profile.resizable_sections = true;
    profile.emit_parameter_defaults = true;
  } else if (language == SourceLanguage::matlab) {
    profile.truthiness = semantic::Truthiness::matlab_all_nonzero;
    profile.division = semantic::Division::real_quotient;
    profile.layout = semantic::IndexLayout::column_major;
    profile.top_level_storage = semantic::TopLevelStorage::entry_function;
  } else if (language == SourceLanguage::fortran) {
    profile.layout = semantic::IndexLayout::column_major;
  } else if (language == SourceLanguage::typescript) {
    profile.division = semantic::Division::real_quotient;
    profile.export_policy = semantic::ExportPolicy::explicit_only;
    profile.scope_model = semantic::ScopeModel::lexical_blocks;
    profile.emit_parameter_defaults = true;
  }
  return profile;
}

template <typename Tag>
class HirLowerer final {
 public:
  explicit HirLowerer(ArenaProgram<Tag>& program) : source_(program) {
    semantics_.nodes.push_back({});
  }

  hir::LoweringResult lower() {
    hir::LoweringResult result;
    result.program.language = source_.language;
    result.program.semantics = semantic_profile(source_.language);
    result.program.statements.reserve(source_.roots.size());
    for (const auto root : source_.roots) result.program.statements.push_back(statement(root));
    result.program.node_count = ids_.count();
    semantics_.hir_revision = result.program.revision;
    semantics_.hir_node_count = result.program.node_count;
    result.semantics = std::move(semantics_);
    result.diagnostics = hir::verify(result.program, "ast-to-hir");
    auto semantic_diagnostics =
        hir::verify_semantics(result.program, result.semantics, "ast-to-hir");
    result.diagnostics.insert(result.diagnostics.end(),
                              std::make_move_iterator(semantic_diagnostics.begin()),
                              std::make_move_iterator(semantic_diagnostics.end()));
    return result;
  }

 private:
  const AstNodeRecord* record(const AstNodeId id, const AstNodeKind kind) const noexcept {
    if (!id.valid() || static_cast<std::size_t>(id.value()) >= source_.records.size())
      return nullptr;
    const auto& found = source_.records[id.value()];
    return found.kind == kind ? &found : nullptr;
  }

  hir::Expression expression(const AstNodeId id) {
    hir::Expression result;
    const auto* entry = record(id, AstNodeKind::expression);
    if (entry == nullptr || entry->index >= source_.expressions.size()) return result;
    auto& node = source_.expressions[entry->index];
    result.id = ids_.next();
    result.location = node.location;
    result.kind = node.kind;
    result.value = std::move(node.value);
    result.unary_operation = node.unary_operation;
    result.operation = node.operation;
    result.comparison = node.comparison;
    result.comparisons = std::move(node.comparisons);
    result.children.reserve(node.children.size());
    for (const auto child : node.children) result.children.push_back(expression(child));
    hir::ExpressionFacts facts;
    facts.origin = result.id;
    facts.inferred_type = node.inferred_type;
    facts.binding = node.binding;
    facts.intrinsic = node.intrinsic;
    facts.element_type = node.element_type;
    facts.shape = std::move(node.shape);
    facts.tuple_types = std::move(node.tuple_types);
    facts.tuple_element_types = std::move(node.tuple_element_types);
    facts.tuple_shapes = std::move(node.tuple_shapes);
    facts.sequence_is_list = node.sequence_is_list;
    facts.sequence_elements = std::move(node.sequence_elements);
    facts.requested_outputs = node.requested_outputs;
    facts.multi_output_call = node.multi_output_call;
    facts.argument_intents = std::move(node.argument_intents);
    facts.argument_names = std::move(node.argument_names);
    facts.argument_optional_forward = std::move(node.argument_optional_forward);
    facts.procedure_has_result = node.procedure_has_result;
    facts.index_base = node.index_base;
    facts.allow_negative_index = node.allow_negative_index;
    facts.column_major = node.column_major;
    facts.slice_stop_inclusive = node.slice_stop_inclusive;
    if (result.kind == ExpressionKind::index) {
      facts.index_selectors.reserve(result.children.empty() ? 0U : result.children.size() - 1U);
      facts.index_extents.reserve(result.children.empty() ? 0U : result.children.size() - 1U);
      for (std::size_t index = 1; index < result.children.size(); ++index) {
        facts.index_selectors.push_back(result.children[index].kind == ExpressionKind::slice
                                            ? semantic::IndexSelectorKind::slice
                                            : semantic::IndexSelectorKind::scalar);
        facts.index_extents.push_back(semantic::IndexExtentSource::none);
      }
    }
    append_expression_facts(std::move(facts));
    return result;
  }

  hir::CaseSelector selector(ArenaCaseSelector<Tag>& node) {
    hir::CaseSelector result;
    result.lower = expression(node.lower);
    result.has_lower = node.has_lower;
    result.upper = expression(node.upper);
    result.has_upper = node.has_upper;
    result.range = node.range;
    return result;
  }

  hir::Statement statement(const AstNodeId id) {
    hir::Statement result;
    const auto* entry = record(id, AstNodeKind::statement);
    if (entry == nullptr || entry->index >= source_.statements.size()) return result;
    auto& node = source_.statements[entry->index];
    result.id = ids_.next();
    result.kind = node.kind;
    result.line = node.line;
    result.name = std::move(node.name);
    result.expression = expression(node.expression);
    result.has_expression = node.has_expression;
    result.procedure_call = node.procedure_call;
    result.secondary_expression = expression(node.secondary_expression);
    result.has_secondary_expression = node.has_secondary_expression;
    result.tertiary_expression = expression(node.tertiary_expression);
    result.has_tertiary_expression = node.has_tertiary_expression;
    result.inclusive_stop = node.inclusive_stop;
    result.retain_last_loop_value = node.retain_last_loop_value;
    result.target_expression = expression(node.target_expression);
    result.has_target_expression = node.has_target_expression;
    result.parameters = std::move(node.parameters);
    result.parameter_kinds = std::move(node.parameter_kinds);
    result.parameter_defaults.reserve(node.parameter_defaults.size());
    for (const auto value : node.parameter_defaults) {
      result.parameter_defaults.push_back(expression(value));
    }
    result.return_names = std::move(node.return_names);
    result.target_names = std::move(node.target_names);
    result.has_target_pattern = node.has_target_pattern;
    result.case_selectors.reserve(node.case_selectors.size());
    for (auto& value : node.case_selectors) result.case_selectors.push_back(selector(value));
    result.default_case = node.default_case;
    result.body.reserve(node.body.size());
    for (const auto value : node.body) result.body.push_back(statement(value));
    result.alternative.reserve(node.alternative.size());
    for (const auto value : node.alternative) result.alternative.push_back(statement(value));
    hir::StatementFacts facts;
    facts.origin = result.id;
    facts.declared_type = node.declared_type;
    facts.element_type = node.element_type;
    facts.previous_type = node.previous_type;
    facts.previous_element_type = node.previous_element_type;
    facts.parameter_intent = node.parameter_intent;
    facts.optional_parameter = node.optional_parameter;
    facts.dummy_parameter = node.dummy_parameter;
    facts.exported = node.exported;
    facts.shape = std::move(node.shape);
    facts.index_base = node.index_base;
    facts.allow_negative_index = node.allow_negative_index;
    facts.parameter_intents = std::move(node.parameter_intents);
    facts.parameter_optional = std::move(node.parameter_optional);
    facts.parameter_types = std::move(node.parameter_types);
    facts.parameter_element_types = std::move(node.parameter_element_types);
    facts.parameter_shapes = std::move(node.parameter_shapes);
    facts.has_value_return = node.has_value_return;
    facts.return_types = std::move(node.return_types);
    facts.return_element_types = std::move(node.return_element_types);
    facts.return_shapes = std::move(node.return_shapes);
    facts.return_sequence_is_list = node.return_sequence_is_list;
    facts.return_sequence_elements = std::move(node.return_sequence_elements);
    facts.target_pattern = std::move(node.target_pattern);
    facts.target_types = std::move(node.target_types);
    facts.target_element_types = std::move(node.target_element_types);
    facts.target_shapes = std::move(node.target_shapes);
    facts.target_previous_types = std::move(node.target_previous_types);
    facts.target_previous_element_types = std::move(node.target_previous_element_types);
    append_statement_facts(std::move(facts));
    return result;
  }

  void append_expression_facts(hir::ExpressionFacts facts) {
    const auto id = facts.origin;
    if (semantics_.nodes.size() <= id.value()) semantics_.nodes.resize(id.value() + 1U);
    semantics_.nodes[id.value()] = {hir::SemanticNodeKind::expression,
                                    static_cast<std::uint32_t>(semantics_.expressions.size())};
    semantics_.expressions.push_back(std::move(facts));
  }

  void append_statement_facts(hir::StatementFacts facts) {
    const auto id = facts.origin;
    if (semantics_.nodes.size() <= id.value()) semantics_.nodes.resize(id.value() + 1U);
    semantics_.nodes[id.value()] = {hir::SemanticNodeKind::statement,
                                    static_cast<std::uint32_t>(semantics_.statements.size())};
    semantics_.statements.push_back(std::move(facts));
  }

  ArenaProgram<Tag>& source_;
  IrIdAllocator<HirNodeId> ids_;
  hir::SemanticTable semantics_;
};

template <typename Tag>
std::vector<Diagnostic> verify_typed_ast(const ArenaProgram<Tag>& ast,
                                         const SourceLanguage expected) {
  std::vector<Diagnostic> diagnostics;
  const auto add_error = [&](const SourceLocation location, std::string message) {
    diagnostics.push_back({DiagnosticSeverity::error, "MPF0005", std::move(message), location});
  };
  if (ast.language != expected) {
    add_error({1, 1}, "frontend AST language identity does not match its descriptor");
  }
  if (ast.records.empty()) {
    add_error({1, 1}, "frontend AST has no sentinel record");
    return diagnostics;
  }
  std::vector<bool> seen(ast.records.size(), false);
  const auto visit = [&](const auto& self, const AstNodeId id,
                         const AstNodeKind expected_kind) -> void {
    if (!id.valid() || static_cast<std::size_t>(id.value()) >= ast.records.size()) {
      add_error({1, 1}, "frontend AST edge references an invalid node ID");
      return;
    }
    const auto index = static_cast<std::size_t>(id.value());
    const auto& record = ast.records[index];
    if (seen[index] || record.kind != expected_kind) {
      add_error(record.origin, "frontend AST node is shared, duplicated, or has the wrong kind");
      return;
    }
    seen[index] = true;
    if (record.kind == AstNodeKind::expression) {
      if (record.index >= ast.expressions.size() || ast.expressions[record.index].id != id) {
        add_error(record.origin, "frontend AST expression record is inconsistent");
        return;
      }
      const auto& node = ast.expressions[record.index];
      if (node.kind == ExpressionKind::invalid || node.requested_outputs == 0) {
        add_error(node.location, "frontend AST expression payload is invalid");
      }
      if (node.kind == ExpressionKind::binary) {
        const bool has_comparison = node.comparison != ComparisonOperator::none;
        const bool has_operation = node.operation != BinaryOperator::none;
        if (has_comparison == has_operation || (has_comparison && !node.value.empty()) ||
            (has_operation && node.value.empty())) {
          add_error(node.location, "frontend AST binary operator representation is ambiguous");
        }
      }
      if ((node.kind == ExpressionKind::unary) != (node.unary_operation != UnaryOperator::none) ||
          (node.kind == ExpressionKind::unary && node.children.size() != 1U)) {
        add_error(node.location, "frontend AST unary operator representation is inconsistent");
      }
      if (node.kind == ExpressionKind::comparison_chain &&
          (node.children.size() < 3U || node.comparisons.size() + 1U != node.children.size() ||
           std::any_of(node.comparisons.begin(), node.comparisons.end(), [](const auto operation) {
             return operation == ComparisonOperator::none;
           }))) {
        add_error(node.location, "frontend AST comparison chain arity is inconsistent");
      }
      for (const auto child : node.children) {
        if (child.valid()) self(self, child, AstNodeKind::expression);
      }
      return;
    }
    if (record.index >= ast.statements.size() || ast.statements[record.index].id != id) {
      add_error(record.origin, "frontend AST statement record is inconsistent");
      return;
    }
    const auto& node = ast.statements[record.index];
    if (node.exported && node.kind != StatementKind::function) {
      add_error({node.line, 1}, "only a function AST node may be explicitly exported");
    }
    const auto optional = [&](const AstNodeId value, const bool present, const char* name) {
      if (present != value.valid()) {
        add_error({node.line, 1}, std::string(name) + " presence flag is inconsistent");
      }
      if (value.valid()) self(self, value, AstNodeKind::expression);
    };
    optional(node.expression, node.has_expression, "primary expression");
    optional(node.secondary_expression, node.has_secondary_expression, "secondary expression");
    optional(node.tertiary_expression, node.has_tertiary_expression, "tertiary expression");
    optional(node.target_expression, node.has_target_expression, "target expression");
    for (const auto value : node.parameter_defaults) {
      if (value.valid()) self(self, value, AstNodeKind::expression);
    }
    for (const auto& selector : node.case_selectors) {
      if (selector.has_lower != selector.lower.valid() ||
          selector.has_upper != selector.upper.valid()) {
        add_error({node.line, 1}, "case selector presence flag is inconsistent");
      }
      if (selector.lower.valid()) self(self, selector.lower, AstNodeKind::expression);
      if (selector.upper.valid()) self(self, selector.upper, AstNodeKind::expression);
    }
    for (const auto value : node.body) self(self, value, AstNodeKind::statement);
    for (const auto value : node.alternative) self(self, value, AstNodeKind::statement);
  };
  for (const auto root : ast.roots) visit(visit, root, AstNodeKind::statement);
  if (static_cast<std::size_t>(std::count(seen.begin(), seen.end(), true)) != ast.node_count()) {
    add_error({1, 1}, "frontend AST arena contains unreachable nodes");
  }
  return diagnostics;
}

template <typename Tag>
std::string dump_typed_ast(const ArenaProgram<Tag>& ast, const std::string_view language) {
  std::ostringstream output;
  output << "ast " << language << " nodes=" << ast.node_count() << '\n';
  for (std::size_t index = 1; index < ast.records.size(); ++index) {
    const auto& record = ast.records[index];
    output << '%' << index << ' ' << (record.kind == AstNodeKind::expression ? "expr" : "stmt")
           << " slot=" << record.index << " @" << record.origin.line << ':' << record.origin.column
           << '\n';
  }
  return output.str();
}

template <typename Tag>
std::size_t arena_bytes(const ArenaProgram<Tag>& ast) noexcept {
  return ast.records.capacity() * sizeof(AstNodeRecord) +
         ast.expressions.capacity() * sizeof(ArenaExpression<Tag>) +
         ast.statements.capacity() * sizeof(ArenaStatement<Tag>) +
         ast.roots.capacity() * sizeof(AstNodeId);
}

}  // namespace

hir::LoweringResult lower_python_ast(python::ast::Program&& program) {
  return HirLowerer<python::ast::LanguageTag>(program).lower();
}

hir::LoweringResult lower_matlab_ast(matlab::ast::Program&& program) {
  return HirLowerer<matlab::ast::LanguageTag>(program).lower();
}

hir::LoweringResult lower_fortran_ast(fortran::ast::Program&& program) {
  return HirLowerer<fortran::ast::LanguageTag>(program).lower();
}

hir::LoweringResult lower_typescript_ast(typescript::ast::Program&& program) {
  return HirLowerer<typescript::ast::LanguageTag>(program).lower();
}

std::size_t frontend_ast_node_count(const FrontendAst& ast) noexcept {
  return std::visit(
      [](const auto& artifact) -> std::size_t {
        using Artifact = std::decay_t<decltype(artifact)>;
        if constexpr (std::is_same_v<Artifact, std::monostate>) {
          return 0;
        } else {
          return artifact.node_count();
        }
      },
      ast);
}

std::size_t frontend_ast_arena_bytes(const FrontendAst& ast) noexcept {
  return std::visit(
      [](const auto& artifact) -> std::size_t {
        using Artifact = std::decay_t<decltype(artifact)>;
        if constexpr (std::is_same_v<Artifact, std::monostate>) {
          return 0;
        } else {
          return arena_bytes(artifact);
        }
      },
      ast);
}

std::string dump_frontend_ast(const FrontendAst& ast) {
  if (const auto* program = std::get_if<python::ast::Program>(&ast)) {
    return dump_typed_ast(*program, "python");
  }
  if (const auto* program = std::get_if<matlab::ast::Program>(&ast)) {
    return dump_typed_ast(*program, "matlab");
  }
  if (const auto* program = std::get_if<fortran::ast::Program>(&ast)) {
    return dump_typed_ast(*program, "fortran");
  }
  if (const auto* program = std::get_if<typescript::ast::Program>(&ast)) {
    return dump_typed_ast(*program, "typescript");
  }
  return "ast <empty> nodes=0\n";
}

std::vector<Diagnostic> verify_frontend_ast(const FrontendAst& ast,
                                            const SourceLanguage expected_language) {
  if (const auto* program = std::get_if<python::ast::Program>(&ast)) {
    return verify_typed_ast(*program, expected_language);
  }
  if (const auto* program = std::get_if<matlab::ast::Program>(&ast)) {
    return verify_typed_ast(*program, expected_language);
  }
  if (const auto* program = std::get_if<fortran::ast::Program>(&ast)) {
    return verify_typed_ast(*program, expected_language);
  }
  if (const auto* program = std::get_if<typescript::ast::Program>(&ast)) {
    return verify_typed_ast(*program, expected_language);
  }
  return {{DiagnosticSeverity::error, "MPF0005", "frontend produced no AST artifact", {1, 1}}};
}

}  // namespace mpf::detail
