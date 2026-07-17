#pragma once

#include <iterator>
#include <limits>
#include <memory_resource>
#include <string_view>
#include <utility>
#include <vector>

#include "compiler/expression.hpp"
#include "frontends/common/ast.hpp"

namespace mpf::detail {

// Builds a language-owned arena directly from parser events. Statement parsers
// retain only one language-specific statement draft containing AstNodeId edges;
// no recursive shared Program/Statement tree exists between parsing and the AST.
template <typename LanguageTag>
class FrontendAstBuilder final {
 public:
  using Program = ArenaProgram<LanguageTag>;
  using Statement = ArenaStatement<LanguageTag>;

  FrontendAstBuilder(const SourceLanguage language, const ExpressionLexer expression_lexer,
                     std::pmr::memory_resource* resource)
      : expression_lexer_(expression_lexer), program_(resource) {
    program_.language = language;
  }

  void reserve(const std::size_t statement_hint, const std::size_t expression_hint) {
    const auto maximum =
        static_cast<std::size_t>(std::numeric_limits<AstNodeId::value_type>::max());
    const auto nodes =
        statement_hint > maximum - expression_hint ? maximum : statement_hint + expression_hint;
    program_.records.reserve(nodes == maximum ? maximum : nodes + 1U);
    program_.expressions.reserve(expression_hint);
    program_.statements.reserve(statement_hint);
    program_.roots.reserve(statement_hint);
  }

  [[nodiscard]] AstNodeId add_expression(Expression&& source) {
    if (!source.valid()) return {};
    ArenaExpression<LanguageTag> node;
    node.location = source.location;
    node.kind = source.kind;
    node.value = std::move(source.value);
    node.unary_operation = source.unary_operation;
    node.operation = source.operation;
    node.comparison = source.comparison;
    node.comparisons = std::move(source.comparisons);
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
    program_.records.push_back(
        {AstNodeKind::expression, program_.expressions.size(), node.location});
    program_.expressions.push_back(std::move(node));
    return program_.expressions.back().id;
  }

  [[nodiscard]] AstNodeId parse_expression(const std::string_view source,
                                           const SourceLanguage language, const std::size_t line,
                                           std::vector<Diagnostic>& diagnostics) {
    if (frontend_trimmed_empty(source)) return {};
    auto parsed = mpf::detail::parse_expression(expression_lexer_(source, line, 1U), language);
    diagnostics.insert(diagnostics.end(), std::make_move_iterator(parsed.diagnostics.begin()),
                       std::make_move_iterator(parsed.diagnostics.end()));
    return add_expression(std::move(parsed.expression));
  }

  [[nodiscard]] AstNodeId add_statement(Statement&& node) {
    node.id = next_id();
    program_.records.push_back(
        {AstNodeKind::statement, program_.statements.size(), {node.line, 1}});
    program_.statements.push_back(std::move(node));
    return program_.statements.back().id;
  }

  void set_roots(std::vector<AstNodeId> roots) {
    program_.roots.assign(std::make_move_iterator(roots.begin()),
                          std::make_move_iterator(roots.end()));
  }

  [[nodiscard]] const ArenaExpression<LanguageTag>* expression(const AstNodeId id) const noexcept {
    if (!id.valid() || static_cast<std::size_t>(id.value()) >= program_.records.size()) {
      return nullptr;
    }
    const auto& record = program_.records[id.value()];
    if (record.kind != AstNodeKind::expression || record.index >= program_.expressions.size()) {
      return nullptr;
    }
    return &program_.expressions[record.index];
  }

  [[nodiscard]] Program finish() && { return std::move(program_); }

 private:
  static bool frontend_trimmed_empty(const std::string_view source) noexcept {
    for (const char character : source) {
      if (character != ' ' && character != '\t' && character != '\r' && character != '\n') {
        return false;
      }
    }
    return true;
  }

  [[nodiscard]] AstNodeId next_id() const noexcept {
    return AstNodeId{static_cast<AstNodeId::value_type>(program_.records.size())};
  }

  ExpressionLexer expression_lexer_;
  Program program_;
};

}  // namespace mpf::detail
