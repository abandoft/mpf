#include "name_analysis.hpp"

#include <algorithm>
#include <limits>
#include <string>
#include <unordered_map>
#include <utility>

namespace mpf::detail {
namespace {

struct ScopeBuilder {
  std::unordered_map<std::string, SymbolId> symbols;
};

void add_error(std::vector<Diagnostic>& diagnostics, const SourceLocation location,
               const std::string_view stage, std::string message) {
  diagnostics.push_back(
      {DiagnosticSeverity::error, "MPF0005",
       "invalid HIR name table at '" + std::string(stage) + "': " + std::move(message), location});
}

BindingKind binding_for(const NameSymbolKind kind) noexcept {
  return kind == NameSymbolKind::function ? BindingKind::function : BindingKind::variable;
}

class NameAnalyzer final {
 public:
  explicit NameAnalyzer(const hir::Program& program) : program_(program) {
    result_.names.hir_revision = program.revision;
    result_.names.hir_node_count = program.node_count;
    result_.names.nodes.resize(program.node_count + 1U);
    result_.names.owned_scopes.resize(program.node_count + 1U);
    result_.names.symbols.push_back({});
    result_.names.scopes.push_back({});
    builders_.push_back({});
    result_.names.global_scope = create_scope({}, {});
  }

  NameAnalysisResult run() {
    collect_statements(program_.statements, result_.names.global_scope);
    bind_statements(program_.statements, result_.names.global_scope);
    return std::move(result_);
  }

 private:
  ScopeId create_scope(const ScopeId parent, const HirNodeId owner) {
    const auto value = result_.names.scopes.size();
    const auto id = ScopeId{static_cast<ScopeId::value_type>(value)};
    result_.names.scopes.push_back({id, parent, owner, {}});
    builders_.push_back({});
    if (owner.valid() && owner.value() < result_.names.owned_scopes.size()) {
      result_.names.owned_scopes[owner.value()] = id;
    }
    return id;
  }

  SymbolId declare(const ScopeId scope, const std::string& name, const NameSymbolKind kind,
                   const HirNodeId origin) {
    auto& builder = builders_[scope.value()];
    const auto found = builder.symbols.find(name);
    if (found != builder.symbols.end()) {
      auto& symbol = result_.names.symbols[found->second.value()];
      if (symbol.kind == NameSymbolKind::variable && kind != NameSymbolKind::variable) {
        symbol.kind = kind;
        symbol.declaration = origin;
      }
      return found->second;
    }
    const auto value = result_.names.symbols.size();
    const auto id = SymbolId{static_cast<SymbolId::value_type>(value)};
    auto& scope_data = result_.names.scopes[scope.value()];
    const auto offset = scope_data.symbols.size();
    scope_data.symbols.push_back(id);
    result_.names.symbols.push_back(
        {id, scope, origin, kind, static_cast<std::uint32_t>(offset), name});
    builder.symbols.emplace(name, id);
    return id;
  }

  SymbolId find_local(const ScopeId scope, const std::string& name) const {
    const auto& symbols = builders_[scope.value()].symbols;
    const auto found = symbols.find(name);
    return found == symbols.end() ? SymbolId{} : found->second;
  }

  SymbolId resolve(ScopeId scope, const std::string& name) const {
    while (scope.valid()) {
      const auto found = find_local(scope, name);
      if (found.valid()) return found;
      scope = result_.names.scopes[scope.value()].parent;
    }
    return {};
  }

  void collect_statements(const std::vector<hir::Statement>& statements, const ScopeId scope) {
    for (const auto& statement : statements) {
      switch (statement.kind) {
        case StatementKind::function: {
          declare(scope, statement.name, NameSymbolKind::function, statement.id);
          auto child_scope = result_.names.owned_scope(statement.id);
          if (!child_scope.valid()) child_scope = create_scope(scope, statement.id);
          for (const auto& parameter : statement.parameters) {
            declare(child_scope, parameter, NameSymbolKind::parameter, statement.id);
          }
          for (const auto& result : statement.return_names) {
            declare(child_scope, result, NameSymbolKind::result, statement.id);
          }
          collect_statements(statement.body, child_scope);
          collect_statements(statement.alternative, child_scope);
          break;
        }
        case StatementKind::declaration:
        case StatementKind::assignment:
          declare(scope, statement.name, NameSymbolKind::variable, statement.id);
          break;
        case StatementKind::multi_assignment:
          for (const auto& name : statement.target_names) {
            declare(scope, name, NameSymbolKind::variable, statement.id);
          }
          break;
        case StatementKind::range_loop:
          declare(scope, statement.name, NameSymbolKind::loop_variable, statement.id);
          collect_statements(statement.body, scope);
          collect_statements(statement.alternative, scope);
          break;
        case StatementKind::if_statement:
        case StatementKind::select_case:
        case StatementKind::case_clause:
        case StatementKind::while_loop:
          collect_statements(statement.body, scope);
          collect_statements(statement.alternative, scope);
          break;
        case StatementKind::indexed_assignment:
        case StatementKind::print:
        case StatementKind::return_statement:
        case StatementKind::break_statement:
        case StatementKind::continue_statement:
        case StatementKind::expression: break;
      }
    }
  }

  void add_use(const HirNodeId origin, const ScopeId scope, const SymbolId symbol,
               const NameRole role, const std::size_t ordinal, const BindingKind binding,
               const IntrinsicId intrinsic = IntrinsicId::none) {
    if (!origin.valid() || origin.value() >= result_.names.nodes.size() ||
        result_.names.uses.size() > std::numeric_limits<std::uint32_t>::max()) {
      return;
    }
    auto& slot = result_.names.nodes[origin.value()];
    if (slot.count == 0) {
      slot.offset = static_cast<std::uint32_t>(result_.names.uses.size());
    } else if (static_cast<std::size_t>(slot.offset) + static_cast<std::size_t>(slot.count) !=
               result_.names.uses.size()) {
      return;
    }
    if (slot.count == std::numeric_limits<std::uint32_t>::max()) return;
    result_.names.uses.push_back(
        {origin, scope, symbol, role, static_cast<std::uint32_t>(ordinal), binding, intrinsic});
    ++slot.count;
  }

  void add_definition(const HirNodeId origin, const ScopeId scope, const std::string& name,
                      const NameRole role, const std::size_t ordinal) {
    const auto symbol = find_local(scope, name);
    const auto* data = symbol.valid() ? &result_.names.symbols[symbol.value()] : nullptr;
    add_use(origin, scope, symbol, role, ordinal,
            data == nullptr ? BindingKind::unresolved : binding_for(data->kind));
  }

  void bind_expression(const hir::Expression& expression, const ScopeId scope) {
    if (!expression.valid()) return;
    if (expression.kind == ExpressionKind::identifier) {
      const auto symbol = resolve(scope, expression.value);
      if (symbol.valid()) {
        add_use(expression.id, scope, symbol, NameRole::reference, 0,
                binding_for(result_.names.symbols[symbol.value()].kind));
      } else {
        const auto intrinsic = find_intrinsic(program_.language, expression.value);
        add_use(expression.id, scope, {}, NameRole::reference, 0,
                intrinsic == IntrinsicId::none ? BindingKind::unresolved : BindingKind::builtin,
                intrinsic);
      }
    }
    for (const auto& child : expression.children) bind_expression(child, scope);
  }

  void bind_statement_expressions(const hir::Statement& statement, const ScopeId scope) {
    bind_expression(statement.expression, scope);
    bind_expression(statement.secondary_expression, scope);
    bind_expression(statement.tertiary_expression, scope);
    bind_expression(statement.target_expression, scope);
    for (const auto& selector : statement.case_selectors) {
      bind_expression(selector.lower, scope);
      bind_expression(selector.upper, scope);
    }
  }

  void bind_statements(const std::vector<hir::Statement>& statements, const ScopeId scope) {
    for (const auto& statement : statements) {
      switch (statement.kind) {
        case StatementKind::function: {
          add_definition(statement.id, scope, statement.name, NameRole::declaration, 0);
          for (const auto& expression : statement.parameter_defaults) {
            bind_expression(expression, scope);
          }
          const auto child_scope = result_.names.owned_scope(statement.id);
          for (std::size_t index = 0; index < statement.parameters.size(); ++index) {
            add_definition(statement.id, child_scope, statement.parameters[index],
                           NameRole::parameter, index);
          }
          for (std::size_t index = 0; index < statement.return_names.size(); ++index) {
            add_definition(statement.id, child_scope, statement.return_names[index],
                           NameRole::result, index);
          }
          bind_statements(statement.body, child_scope);
          bind_statements(statement.alternative, child_scope);
          continue;
        }
        case StatementKind::declaration:
          add_definition(statement.id, scope, statement.name, NameRole::declaration, 0);
          break;
        case StatementKind::assignment:
          add_definition(statement.id, scope, statement.name, NameRole::assignment, 0);
          break;
        case StatementKind::multi_assignment:
          for (std::size_t index = 0; index < statement.target_names.size(); ++index) {
            add_definition(statement.id, scope, statement.target_names[index], NameRole::assignment,
                           index);
          }
          break;
        case StatementKind::range_loop:
          add_definition(statement.id, scope, statement.name, NameRole::loop_variable, 0);
          break;
        case StatementKind::indexed_assignment:
        case StatementKind::print:
        case StatementKind::return_statement:
        case StatementKind::break_statement:
        case StatementKind::continue_statement:
        case StatementKind::expression:
        case StatementKind::if_statement:
        case StatementKind::select_case:
        case StatementKind::case_clause:
        case StatementKind::while_loop: break;
      }
      bind_statement_expressions(statement, scope);
      bind_statements(statement.body, scope);
      bind_statements(statement.alternative, scope);
    }
  }

  const hir::Program& program_;
  NameAnalysisResult result_;
  std::vector<ScopeBuilder> builders_;
};

bool scope_contains(const NameTable& names, ScopeId scope, const ScopeId expected) {
  while (scope.valid()) {
    if (scope == expected) return true;
    const auto* data = names.scope(scope);
    if (data == nullptr) return false;
    scope = data->parent;
  }
  return false;
}

void verify_expression(const hir::Expression& expression, const ScopeId scope,
                       const NameTable& names, std::vector<bool>& resident,
                       const std::string_view stage, std::vector<Diagnostic>& diagnostics) {
  if (!expression.valid()) return;
  resident[expression.id.value()] = true;
  if (expression.kind == ExpressionKind::identifier) {
    const auto* use = names.reference(expression.id);
    if (use == nullptr || use->scope != scope) {
      add_error(diagnostics, expression.location, stage,
                "identifier expression has no name-resolution fact in its lexical scope");
    }
  }
  for (const auto& child : expression.children) {
    verify_expression(child, scope, names, resident, stage, diagnostics);
  }
}

void require_definition(const hir::Statement& statement, const ScopeId scope,
                        const NameTable& names, const NameRole role, const std::size_t ordinal,
                        const std::string_view stage, std::vector<Diagnostic>& diagnostics) {
  const auto* use = names.use(statement.id, role, ordinal);
  if (use == nullptr || !use->symbol.valid() || use->scope != scope) {
    add_error(diagnostics, {statement.line, 1}, stage,
              "statement definition has no resolved symbol in its lexical scope");
  }
}

void verify_statement_expressions(const hir::Statement& statement, const ScopeId scope,
                                  const NameTable& names, std::vector<bool>& resident,
                                  const std::string_view stage,
                                  std::vector<Diagnostic>& diagnostics) {
  verify_expression(statement.expression, scope, names, resident, stage, diagnostics);
  verify_expression(statement.secondary_expression, scope, names, resident, stage, diagnostics);
  verify_expression(statement.tertiary_expression, scope, names, resident, stage, diagnostics);
  verify_expression(statement.target_expression, scope, names, resident, stage, diagnostics);
  for (const auto& selector : statement.case_selectors) {
    verify_expression(selector.lower, scope, names, resident, stage, diagnostics);
    verify_expression(selector.upper, scope, names, resident, stage, diagnostics);
  }
}

void verify_statements(const std::vector<hir::Statement>& statements, const ScopeId scope,
                       const NameTable& names, std::vector<bool>& resident,
                       const std::string_view stage, std::vector<Diagnostic>& diagnostics) {
  for (const auto& statement : statements) {
    resident[statement.id.value()] = true;
    if (statement.kind == StatementKind::function) {
      require_definition(statement, scope, names, NameRole::declaration, 0, stage, diagnostics);
      const auto child_scope = names.owned_scope(statement.id);
      const auto* child = names.scope(child_scope);
      if (child == nullptr || child->parent != scope || child->owner != statement.id) {
        add_error(diagnostics, {statement.line, 1}, stage,
                  "function does not own a valid child scope");
        continue;
      }
      for (std::size_t index = 0; index < statement.parameters.size(); ++index) {
        require_definition(statement, child_scope, names, NameRole::parameter, index, stage,
                           diagnostics);
      }
      for (std::size_t index = 0; index < statement.return_names.size(); ++index) {
        require_definition(statement, child_scope, names, NameRole::result, index, stage,
                           diagnostics);
      }
      for (const auto& expression : statement.parameter_defaults) {
        verify_expression(expression, scope, names, resident, stage, diagnostics);
      }
      verify_statements(statement.body, child_scope, names, resident, stage, diagnostics);
      verify_statements(statement.alternative, child_scope, names, resident, stage, diagnostics);
      continue;
    }
    switch (statement.kind) {
      case StatementKind::declaration:
        require_definition(statement, scope, names, NameRole::declaration, 0, stage, diagnostics);
        break;
      case StatementKind::assignment:
        require_definition(statement, scope, names, NameRole::assignment, 0, stage, diagnostics);
        break;
      case StatementKind::multi_assignment:
        for (std::size_t index = 0; index < statement.target_names.size(); ++index) {
          require_definition(statement, scope, names, NameRole::assignment, index, stage,
                             diagnostics);
        }
        break;
      case StatementKind::range_loop:
        require_definition(statement, scope, names, NameRole::loop_variable, 0, stage, diagnostics);
        break;
      case StatementKind::function:
      case StatementKind::indexed_assignment:
      case StatementKind::print:
      case StatementKind::return_statement:
      case StatementKind::break_statement:
      case StatementKind::continue_statement:
      case StatementKind::expression:
      case StatementKind::if_statement:
      case StatementKind::select_case:
      case StatementKind::case_clause:
      case StatementKind::while_loop: break;
    }
    verify_statement_expressions(statement, scope, names, resident, stage, diagnostics);
    verify_statements(statement.body, scope, names, resident, stage, diagnostics);
    verify_statements(statement.alternative, scope, names, resident, stage, diagnostics);
  }
}

}  // namespace

const NameUse* NameTable::use(const HirNodeId origin, const NameRole role,
                              const std::size_t ordinal) const noexcept {
  if (!origin.valid() || origin.value() >= nodes.size()) return nullptr;
  const auto slot = nodes[origin.value()];
  const auto begin = static_cast<std::size_t>(slot.offset);
  const auto count = static_cast<std::size_t>(slot.count);
  if (begin > uses.size() || count > uses.size() - begin) return nullptr;
  for (std::size_t index = begin; index < begin + count; ++index) {
    if (uses[index].role == role && static_cast<std::size_t>(uses[index].ordinal) == ordinal) {
      return &uses[index];
    }
  }
  return nullptr;
}

const NameUse* NameTable::reference(const HirNodeId origin) const noexcept {
  return use(origin, NameRole::reference, 0);
}

const NameSymbol* NameTable::symbol(const SymbolId id) const noexcept {
  return id.valid() && id.value() < symbols.size() ? &symbols[id.value()] : nullptr;
}

const NameScope* NameTable::scope(const ScopeId id) const noexcept {
  return id.valid() && id.value() < scopes.size() ? &scopes[id.value()] : nullptr;
}

ScopeId NameTable::owned_scope(const HirNodeId owner) const noexcept {
  return owner.valid() && owner.value() < owned_scopes.size() ? owned_scopes[owner.value()]
                                                              : ScopeId{};
}

NameAnalysisResult analyze_names(const hir::Program& program) {
  auto result = NameAnalyzer(program).run();
  result.diagnostics = verify_names(program, result.names, "name-analysis");
  return result;
}

std::vector<Diagnostic> verify_names(const hir::Program& program, const NameTable& names,
                                     const std::string_view stage) {
  std::vector<Diagnostic> diagnostics;
  if (names.hir_revision != program.revision) {
    add_error(diagnostics, {1, 1}, stage, "HIR revision is stale");
  }
  if (names.hir_node_count != program.node_count || names.nodes.size() != program.node_count + 1U ||
      names.owned_scopes.size() != names.nodes.size()) {
    add_error(diagnostics, {1, 1}, stage, "dense node index does not cover the HIR ID space");
    return diagnostics;
  }
  if (!names.global_scope.valid() || names.scope(names.global_scope) == nullptr ||
      names.scopes.empty() || names.symbols.empty()) {
    add_error(diagnostics, {1, 1}, stage, "scope or symbol inventory has no sentinel/root");
    return diagnostics;
  }
  for (std::size_t index = 1; index < names.scopes.size(); ++index) {
    const auto& scope = names.scopes[index];
    if (scope.id.value() != index ||
        (scope.parent.valid() && names.scope(scope.parent) == nullptr)) {
      add_error(diagnostics, {1, 1}, stage, "scope identity or parent is invalid");
    }
    for (std::size_t offset = 0; offset < scope.symbols.size(); ++offset) {
      const auto* symbol = names.symbol(scope.symbols[offset]);
      if (symbol == nullptr || symbol->scope != scope.id || symbol->scope_offset != offset) {
        add_error(diagnostics, {1, 1}, stage, "scope symbol inventory is inconsistent");
      }
    }
  }
  for (std::size_t index = 1; index < names.symbols.size(); ++index) {
    const auto& symbol = names.symbols[index];
    if (symbol.id.value() != index || names.scope(symbol.scope) == nullptr || symbol.name.empty()) {
      add_error(diagnostics, {1, 1}, stage, "symbol identity, scope, or name is invalid");
    }
  }
  std::vector<bool> resident(names.nodes.size(), false);
  verify_statements(program.statements, names.global_scope, names, resident, stage, diagnostics);
  std::vector<bool> seen_use(names.uses.size(), false);
  for (std::size_t id = 1; id < names.nodes.size(); ++id) {
    const auto slot = names.nodes[id];
    const auto begin = static_cast<std::size_t>(slot.offset);
    const auto count = static_cast<std::size_t>(slot.count);
    if (begin > names.uses.size() || count > names.uses.size() - begin) {
      add_error(diagnostics, {1, 1}, stage, "node name-use range is out of bounds");
      continue;
    }
    for (std::size_t index = begin; index < begin + count; ++index) {
      const auto& use = names.uses[index];
      if (seen_use[index] || use.origin.value() != id || !resident[id] ||
          names.scope(use.scope) == nullptr) {
        add_error(diagnostics, {1, 1}, stage, "name use has invalid ownership or scope");
      }
      seen_use[index] = true;
      if (use.symbol.valid()) {
        const auto* symbol = names.symbol(use.symbol);
        if (symbol == nullptr || !scope_contains(names, use.scope, symbol->scope) ||
            use.binding != binding_for(symbol->kind) || use.intrinsic != IntrinsicId::none) {
          add_error(diagnostics, {1, 1}, stage, "resolved name use has an invalid symbol contract");
        }
        if (use.role != NameRole::reference && symbol->scope != use.scope) {
          add_error(diagnostics, {1, 1}, stage, "definition does not belong to its scope");
        }
      } else if ((use.binding == BindingKind::builtin) != (use.intrinsic != IntrinsicId::none) ||
                 use.role != NameRole::reference) {
        add_error(diagnostics, {1, 1}, stage, "unresolved/builtin name use is malformed");
      }
    }
  }
  if (std::find(seen_use.begin(), seen_use.end(), false) != seen_use.end()) {
    add_error(diagnostics, {1, 1}, stage, "name-use inventory contains an unowned entry");
  }
  return diagnostics;
}

}  // namespace mpf::detail
