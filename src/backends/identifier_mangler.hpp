#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <unordered_map>

#include "../compiler/expression_ast.hpp"
#include "../compiler/statement_kind.hpp"
#include "../ir/ids.hpp"

namespace mpf::detail {

struct IdentifierPlan {
  TargetLanguage target{TargetLanguage::javascript};
  bool unique_symbol_names{false};
  std::unordered_map<std::string, std::string> names;
  std::unordered_map<SymbolId, std::string> symbols;
  std::set<std::string> used;
};

struct IdentifierInventory {
  std::map<SymbolId, std::string> symbols;
  std::set<std::string> names;
  bool valid{true};
  bool require_unique_symbol_names{false};
};

struct IdentifierReference {
  SymbolId symbol{};
  std::string name;

  friend bool operator==(const IdentifierReference& left,
                         const IdentifierReference& right) noexcept {
    return left.symbol == right.symbol && left.name == right.name;
  }
};

[[nodiscard]] IdentifierPlan allocate_identifiers(TargetLanguage target,
                                                  const std::set<std::string>& originals);
[[nodiscard]] IdentifierPlan allocate_identifiers(TargetLanguage target,
                                                  const IdentifierInventory& originals);
[[nodiscard]] bool identifier_plan_complete(const IdentifierPlan& plan,
                                            const std::set<std::string>& originals) noexcept;
[[nodiscard]] bool identifier_plan_complete(const IdentifierPlan& plan,
                                            const IdentifierInventory& originals) noexcept;
[[nodiscard]] std::string reserve_internal_identifier(std::set<std::string>& used,
                                                      const std::string& stem, std::uint32_t node,
                                                      std::size_t ordinal = 0);

class IdentifierMangler final {
 public:
  IdentifierMangler(TargetLanguage target, const std::set<std::string>& originals);
  explicit IdentifierMangler(const IdentifierPlan& plan);

  [[nodiscard]] const std::string& name(const std::string& source_name) const;
  [[nodiscard]] const std::string& name(SymbolId symbol, const std::string& source_name) const;

 private:
  std::unordered_map<std::string, std::string> names_;
  std::unordered_map<SymbolId, std::string> symbols_;
};

inline void add_identifier(IdentifierInventory& identifiers, const SymbolId symbol,
                           const std::string& name) {
  if (name.empty()) return;
  if (symbol.valid()) {
    const auto inserted = identifiers.symbols.emplace(symbol, name);
    if (!inserted.second && inserted.first->second != name) identifiers.valid = false;
  } else {
    identifiers.names.insert(name);
  }
}

template <typename Expression>
void collect_identifier_expression(const Expression& expression, IdentifierInventory& identifiers) {
  if (expression.kind == ExpressionKind::identifier && expression.binding != BindingKind::builtin) {
    add_identifier(identifiers, expression.symbol_id, expression.value);
  }
  for (const auto& child : expression.children) {
    collect_identifier_expression(child, identifiers);
  }
}

template <typename Statement>
void collect_identifier_statements(const std::vector<Statement>& statements,
                                   IdentifierInventory& identifiers) {
  for (const auto& statement : statements) {
    if (statement.kind == StatementKind::declaration ||
        statement.kind == StatementKind::assignment ||
        statement.kind == StatementKind::range_loop || statement.kind == StatementKind::for_loop ||
        statement.kind == StatementKind::function) {
      add_identifier(identifiers, statement.symbol_id, statement.name);
    }
    for (std::size_t index = 0; index < statement.parameters.size(); ++index) {
      add_identifier(identifiers,
                     index < statement.parameter_symbols.size() ? statement.parameter_symbols[index]
                                                                : SymbolId{},
                     statement.parameters[index]);
    }
    for (std::size_t index = 0; index < statement.return_names.size(); ++index) {
      add_identifier(
          identifiers,
          index < statement.return_symbols.size() ? statement.return_symbols[index] : SymbolId{},
          statement.return_names[index]);
    }
    for (std::size_t index = 0; index < statement.target_names.size(); ++index) {
      add_identifier(
          identifiers,
          index < statement.target_symbols.size() ? statement.target_symbols[index] : SymbolId{},
          statement.target_names[index]);
    }
    if (statement.has_expression) collect_identifier_expression(statement.expression, identifiers);
    if (statement.has_target_expression) {
      collect_identifier_expression(statement.target_expression, identifiers);
    }
    if (statement.has_secondary_expression) {
      collect_identifier_expression(statement.secondary_expression, identifiers);
    }
    if (statement.has_tertiary_expression) {
      collect_identifier_expression(statement.tertiary_expression, identifiers);
    }
    collect_identifier_statements(statement.body, identifiers);
    collect_identifier_statements(statement.alternative, identifiers);
  }
}

template <typename Program>
IdentifierInventory collect_identifier_inventory(const Program& program) {
  IdentifierInventory identifiers;
  identifiers.require_unique_symbol_names = program.emission.lexical_block_scopes;
  collect_identifier_statements(program.statements, identifiers);
  return identifiers;
}

template <typename Program>
std::set<std::string> collect_identifier_names(const Program& program) {
  const auto inventory = collect_identifier_inventory(program);
  auto names = inventory.names;
  for (const auto& [symbol, spelling] : inventory.symbols) {
    (void)symbol;
    names.insert(spelling);
  }
  return names;
}

}  // namespace mpf::detail
