#include "frontend_ast.hpp"

#include <algorithm>
#include <iterator>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>

#include "../ir/semantic_table.hpp"

namespace mpf::detail {
namespace {

template <typename Tag>
class AstBuilder final {
 public:
  explicit AstBuilder(std::pmr::memory_resource* resource) : result_(resource) {}

  ArenaProgram<Tag> build(mpf::detail::Program&& source) {
    result_.language = source.language;
    result_.roots.reserve(source.statements.size());
    for (auto& statement : source.statements) {
      result_.roots.push_back(add_statement(std::move(statement)));
    }
    return std::move(result_);
  }

 private:
  AstNodeId add_expression(mpf::detail::Expression&& source) {
    if (!source.valid()) return {};
    ArenaExpression<Tag> node;
    node.location = source.location;
    node.kind = source.kind;
    node.value = std::move(source.value);
    node.operators = std::move(source.operators);
    node.children.reserve(source.children.size());
    for (auto& child : source.children) node.children.push_back(add_expression(std::move(child)));
    node.inferred_type = source.inferred_type;
    node.binding = source.binding;
    node.intrinsic = source.intrinsic;
    node.element_type = source.element_type;
    node.shape = std::move(source.shape);
    node.tuple_types = std::move(source.tuple_types);
    node.tuple_element_types = std::move(source.tuple_element_types);
    node.tuple_shapes = std::move(source.tuple_shapes);
    node.sequence_is_list = source.sequence_is_list;
    node.sequence_elements = std::move(source.sequence_elements);
    node.requested_outputs = source.requested_outputs;
    node.multi_output_call = source.matlab_multi_output_call;
    node.argument_intents = std::move(source.argument_intents);
    node.argument_names = std::move(source.argument_names);
    node.argument_optional_forward = std::move(source.argument_optional_forward);
    node.procedure_has_result = source.procedure_has_result;
    node.index_base = source.index_base;
    node.allow_negative_index = source.allow_negative_index;
    node.column_major = source.column_major;
    node.slice_stop_inclusive = source.slice_stop_inclusive;
    node.id = next_id();
    result_.records.push_back({AstNodeKind::expression, result_.expressions.size(), node.location});
    result_.expressions.push_back(std::move(node));
    return result_.expressions.back().id;
  }

  ArenaCaseSelector<Tag> add_selector(mpf::detail::CaseSelector&& source) {
    ArenaCaseSelector<Tag> result;
    result.lower = add_expression(std::move(source.lower));
    result.has_lower = source.has_lower;
    result.upper = add_expression(std::move(source.upper));
    result.has_upper = source.has_upper;
    result.range = source.range;
    return result;
  }

  AstNodeId add_statement(mpf::detail::Statement&& source) {
    ArenaStatement<Tag> node;
    node.kind = source.kind;
    node.line = source.line;
    node.name = std::move(source.name);
    node.expression = add_expression(std::move(source.expression));
    node.has_expression = source.has_expression;
    node.procedure_call = source.procedure_call;
    node.secondary_expression = add_expression(std::move(source.secondary_expression));
    node.has_secondary_expression = source.has_secondary_expression;
    node.tertiary_expression = add_expression(std::move(source.tertiary_expression));
    node.has_tertiary_expression = source.has_tertiary_expression;
    node.inclusive_stop = source.inclusive_stop;
    node.retain_last_loop_value = source.retain_last_loop_value;
    node.declared_type = source.declared_type;
    node.element_type = source.element_type;
    node.previous_type = source.previous_type;
    node.previous_element_type = source.previous_element_type;
    node.parameter_intent = source.parameter_intent;
    node.optional_parameter = source.optional_parameter;
    node.dummy_parameter = source.dummy_parameter;
    node.shape = std::move(source.shape);
    node.index_base = source.index_base;
    node.allow_negative_index = source.allow_negative_index;
    node.target_expression = add_expression(std::move(source.target_expression));
    node.has_target_expression = source.has_target_expression;
    node.parameters = std::move(source.parameters);
    node.parameter_kinds = std::move(source.parameter_kinds);
    node.parameter_defaults.reserve(source.parameter_defaults.size());
    for (auto& expression : source.parameter_defaults) {
      node.parameter_defaults.push_back(add_expression(std::move(expression)));
    }
    node.parameter_intents = std::move(source.parameter_intents);
    node.parameter_optional = std::move(source.parameter_optional);
    node.parameter_types = std::move(source.parameter_types);
    node.parameter_element_types = std::move(source.parameter_element_types);
    node.parameter_shapes = std::move(source.parameter_shapes);
    node.return_names = std::move(source.return_names);
    node.has_value_return = source.has_value_return;
    node.return_types = std::move(source.return_types);
    node.return_element_types = std::move(source.return_element_types);
    node.return_shapes = std::move(source.return_shapes);
    node.return_sequence_is_list = source.return_sequence_is_list;
    node.return_sequence_elements = std::move(source.return_sequence_elements);
    node.target_names = std::move(source.target_names);
    node.target_pattern = std::move(source.target_pattern);
    node.has_target_pattern = source.has_target_pattern;
    node.target_types = std::move(source.target_types);
    node.target_element_types = std::move(source.target_element_types);
    node.target_shapes = std::move(source.target_shapes);
    node.target_previous_types = std::move(source.target_previous_types);
    node.target_previous_element_types = std::move(source.target_previous_element_types);
    node.case_selectors.reserve(source.case_selectors.size());
    for (auto& selector : source.case_selectors) {
      node.case_selectors.push_back(add_selector(std::move(selector)));
    }
    node.default_case = source.default_case;
    node.body.reserve(source.body.size());
    for (auto& statement : source.body) node.body.push_back(add_statement(std::move(statement)));
    node.alternative.reserve(source.alternative.size());
    for (auto& statement : source.alternative) {
      node.alternative.push_back(add_statement(std::move(statement)));
    }
    node.id = next_id();
    result_.records.push_back({AstNodeKind::statement, result_.statements.size(), {node.line, 1}});
    result_.statements.push_back(std::move(node));
    return result_.statements.back().id;
  }

  AstNodeId next_id() const noexcept {
    return AstNodeId{static_cast<AstNodeId::value_type>(result_.records.size())};
  }

  ArenaProgram<Tag> result_;
};

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
    profile.division = semantic::Division::real_quotient;
    profile.layout = semantic::IndexLayout::column_major;
    profile.top_level_storage = semantic::TopLevelStorage::entry_function;
  } else if (language == SourceLanguage::fortran) {
    profile.layout = semantic::IndexLayout::column_major;
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
    result.operators = std::move(node.operators);
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

python::ast::Program make_python_ast(mpf::detail::Program&& syntax,
                                     std::pmr::memory_resource* resource) {
  return AstBuilder<python::ast::LanguageTag>(resource).build(std::move(syntax));
}

matlab::ast::Program make_matlab_ast(mpf::detail::Program&& syntax,
                                     std::pmr::memory_resource* resource) {
  return AstBuilder<matlab::ast::LanguageTag>(resource).build(std::move(syntax));
}

fortran::ast::Program make_fortran_ast(mpf::detail::Program&& syntax,
                                       std::pmr::memory_resource* resource) {
  return AstBuilder<fortran::ast::LanguageTag>(resource).build(std::move(syntax));
}

hir::LoweringResult lower_python_ast(python::ast::Program&& program) {
  return HirLowerer<python::ast::LanguageTag>(program).lower();
}

hir::LoweringResult lower_matlab_ast(matlab::ast::Program&& program) {
  return HirLowerer<matlab::ast::LanguageTag>(program).lower();
}

hir::LoweringResult lower_fortran_ast(fortran::ast::Program&& program) {
  return HirLowerer<fortran::ast::LanguageTag>(program).lower();
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
  return {{DiagnosticSeverity::error, "MPF0005", "frontend produced no AST artifact", {1, 1}}};
}

}  // namespace mpf::detail
