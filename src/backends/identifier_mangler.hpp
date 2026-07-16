#pragma once

#include <cstddef>
#include <cstdint>
#include <set>
#include <string>
#include <unordered_map>

#include "../compiler/expression_ast.hpp"
#include "../compiler/ir.hpp"

namespace mpf::detail {

struct IdentifierPlan {
  std::unordered_map<std::string, std::string> names;
  std::set<std::string> used;
};

[[nodiscard]] IdentifierPlan allocate_identifiers(TargetLanguage target,
                                                  const std::set<std::string>& originals);
[[nodiscard]] bool identifier_plan_complete(const IdentifierPlan& plan,
                                            const std::set<std::string>& originals) noexcept;
[[nodiscard]] std::string reserve_internal_identifier(std::set<std::string>& used,
                                                      const std::string& stem, std::uint32_t node,
                                                      std::size_t ordinal = 0);

class IdentifierMangler final {
 public:
  IdentifierMangler(TargetLanguage target, const std::set<std::string>& originals);
  explicit IdentifierMangler(const IdentifierPlan& plan);

  [[nodiscard]] const std::string& name(const std::string& source_name) const;

 private:
  std::unordered_map<std::string, std::string> names_;
};

template <typename Expression>
void collect_identifier_expression(const Expression& expression, std::set<std::string>& names) {
  if (expression.kind == ExpressionKind::identifier && expression.binding != BindingKind::builtin) {
    names.insert(expression.value);
  }
  for (const auto& child : expression.children) {
    collect_identifier_expression(child, names);
  }
}

template <typename Statement>
void collect_identifier_statements(const std::vector<Statement>& statements,
                                   std::set<std::string>& names) {
  for (const auto& statement : statements) {
    if (statement.kind == StatementKind::declaration ||
        statement.kind == StatementKind::assignment ||
        statement.kind == StatementKind::range_loop || statement.kind == StatementKind::function) {
      names.insert(statement.name);
    }
    names.insert(statement.parameters.begin(), statement.parameters.end());
    names.insert(statement.return_names.begin(), statement.return_names.end());
    names.insert(statement.target_names.begin(), statement.target_names.end());
    if (statement.has_expression) collect_identifier_expression(statement.expression, names);
    if (statement.has_target_expression) {
      collect_identifier_expression(statement.target_expression, names);
    }
    if (statement.has_secondary_expression) {
      collect_identifier_expression(statement.secondary_expression, names);
    }
    if (statement.has_tertiary_expression) {
      collect_identifier_expression(statement.tertiary_expression, names);
    }
    collect_identifier_statements(statement.body, names);
    collect_identifier_statements(statement.alternative, names);
  }
}

template <typename Program>
std::set<std::string> collect_identifier_names(const Program& program) {
  std::set<std::string> names;
  collect_identifier_statements(program.statements, names);
  return names;
}

}  // namespace mpf::detail
