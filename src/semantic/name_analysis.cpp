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
    result_.names.scope_edges.resize(program.node_count + 1U);
    result_.names.symbols.push_back({});
    result_.names.scopes.push_back({});
    builders_.push_back({});
    result_.names.global_scope = create_scope({}, {}, NameScopeKind::global);
  }

  NameAnalysisResult run() {
    collect_statements(program_.statements, result_.names.global_scope);
    bind_statements(program_.statements, result_.names.global_scope);
    return std::move(result_);
  }

 private:
  ScopeId create_scope(const ScopeId parent, const HirNodeId owner, const NameScopeKind kind) {
    const auto value = result_.names.scopes.size();
    const auto id = ScopeId{static_cast<ScopeId::value_type>(value)};
    result_.names.scopes.push_back({id, parent, owner, kind, {}});
    builders_.push_back({});
    if (owner.valid() && owner.value() < result_.names.scope_edges.size()) {
      auto& edges = result_.names.scope_edges[owner.value()];
      switch (kind) {
        case NameScopeKind::function: edges.function = id; break;
        case NameScopeKind::statement: edges.statement = id; break;
        case NameScopeKind::body: edges.body = id; break;
        case NameScopeKind::alternative: edges.alternative = id; break;
        case NameScopeKind::global: break;
      }
    }
    return id;
  }

  [[nodiscard]] bool lexical_blocks() const noexcept {
    return program_.semantics.scope_model == semantic::ScopeModel::lexical_blocks;
  }

  ScopeId branch_scope(const ScopeId parent, const hir::Statement& statement,
                       const NameScopeKind kind) {
    if (!lexical_blocks()) return parent;
    return create_scope(parent, statement.id, kind);
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
      if (statement.implicit_result != semantic::ImplicitResultPolicy::none && !lexical_blocks()) {
        declare(scope, statement.name, NameSymbolKind::variable, statement.id);
      }
      switch (statement.kind) {
        case StatementKind::function: {
          declare(scope, statement.name, NameSymbolKind::function, statement.id);
          auto child_scope = result_.names.function_scope(statement.id);
          if (!child_scope.valid()) {
            child_scope = create_scope(scope, statement.id, NameScopeKind::function);
          }
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
          declare(scope, statement.name, NameSymbolKind::variable, statement.id);
          break;
        case StatementKind::assignment:
          if (!lexical_blocks()) {
            declare(scope, statement.name, NameSymbolKind::variable, statement.id);
          }
          break;
        case StatementKind::multi_assignment:
          if (!lexical_blocks()) {
            for (const auto& name : statement.target_names) {
              declare(scope, name, NameSymbolKind::variable, statement.id);
            }
          }
          break;
        case StatementKind::return_statement: break;
        case StatementKind::range_loop: {
          auto loop_scope = scope;
          auto body_scope = scope;
          if (lexical_blocks()) {
            loop_scope = create_scope(scope, statement.id, NameScopeKind::statement);
            body_scope = create_scope(loop_scope, statement.id, NameScopeKind::body);
          }
          declare(loop_scope, statement.name, NameSymbolKind::loop_variable, statement.id);
          collect_statements(statement.body, body_scope);
          const auto alternative_scope =
              statement.alternative.empty()
                  ? scope
                  : branch_scope(scope, statement, NameScopeKind::alternative);
          collect_statements(statement.alternative, alternative_scope);
          break;
        }
        case StatementKind::for_loop: {
          const auto loop_scope = create_scope(scope, statement.id, NameScopeKind::statement);
          const auto body_scope = create_scope(loop_scope, statement.id, NameScopeKind::body);
          declare(loop_scope, statement.name, NameSymbolKind::loop_variable, statement.id);
          collect_statements(statement.body, body_scope);
          collect_statements(statement.alternative, scope);
          break;
        }
        case StatementKind::if_statement:
        case StatementKind::select_case:
        case StatementKind::case_clause:
        case StatementKind::while_loop: {
          const auto body_scope = branch_scope(scope, statement, NameScopeKind::body);
          const auto alternative_scope =
              statement.alternative.empty()
                  ? scope
                  : branch_scope(scope, statement, NameScopeKind::alternative);
          collect_statements(statement.body, body_scope);
          collect_statements(statement.alternative, alternative_scope);
          break;
        }
        case StatementKind::indexed_assignment:
        case StatementKind::print:
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

  void add_assignment(const HirNodeId origin, const ScopeId scope, const std::string& name,
                      const std::size_t ordinal) {
    const auto symbol = resolve(scope, name);
    const auto* data = symbol.valid() ? &result_.names.symbols[symbol.value()] : nullptr;
    add_use(origin, scope, symbol, NameRole::assignment, ordinal,
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
      if (statement.implicit_result != semantic::ImplicitResultPolicy::none) {
        if (lexical_blocks())
          add_assignment(statement.id, scope, statement.name, 0);
        else
          add_definition(statement.id, scope, statement.name, NameRole::assignment, 0);
      }
      switch (statement.kind) {
        case StatementKind::function: {
          add_definition(statement.id, scope, statement.name, NameRole::declaration, 0);
          for (const auto& expression : statement.parameter_defaults) {
            bind_expression(expression, scope);
          }
          const auto child_scope = result_.names.function_scope(statement.id);
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
          if (lexical_blocks())
            add_assignment(statement.id, scope, statement.name, 0);
          else
            add_definition(statement.id, scope, statement.name, NameRole::assignment, 0);
          break;
        case StatementKind::multi_assignment:
          for (std::size_t index = 0; index < statement.target_names.size(); ++index) {
            if (lexical_blocks())
              add_assignment(statement.id, scope, statement.target_names[index], index);
            else
              add_definition(statement.id, scope, statement.target_names[index],
                             NameRole::assignment, index);
          }
          break;
        case StatementKind::return_statement:
          for (std::size_t index = 0; index < statement.return_names.size(); ++index) {
            add_definition(statement.id, scope, statement.return_names[index], NameRole::result,
                           index);
          }
          break;
        case StatementKind::range_loop: {
          bind_statement_expressions(statement, scope);
          const auto loop_scope =
              lexical_blocks() ? result_.names.statement_scope(statement.id) : scope;
          const auto body_scope = lexical_blocks() ? result_.names.body_scope(statement.id) : scope;
          add_definition(statement.id, loop_scope, statement.name, NameRole::loop_variable, 0);
          bind_statements(statement.body, body_scope);
          const auto alternative_scope = lexical_blocks() && !statement.alternative.empty()
                                             ? result_.names.alternative_scope(statement.id)
                                             : scope;
          bind_statements(statement.alternative, alternative_scope);
          continue;
        }
        case StatementKind::for_loop: {
          const auto loop_scope = result_.names.statement_scope(statement.id);
          add_definition(statement.id, loop_scope, statement.name, NameRole::loop_variable, 0);
          bind_expression(statement.expression, loop_scope);
          bind_expression(statement.secondary_expression, loop_scope);
          bind_expression(statement.tertiary_expression, loop_scope);
          bind_expression(statement.target_expression, loop_scope);
          bind_statements(statement.body, result_.names.body_scope(statement.id));
          bind_statements(statement.alternative, scope);
          continue;
        }
        case StatementKind::indexed_assignment:
        case StatementKind::print:
        case StatementKind::break_statement:
        case StatementKind::continue_statement:
        case StatementKind::expression:
        case StatementKind::if_statement:
        case StatementKind::select_case:
        case StatementKind::case_clause:
        case StatementKind::while_loop: break;
      }
      bind_statement_expressions(statement, scope);
      const bool nested_scope =
          lexical_blocks() && (statement.kind == StatementKind::if_statement ||
                               statement.kind == StatementKind::select_case ||
                               statement.kind == StatementKind::case_clause ||
                               statement.kind == StatementKind::while_loop);
      const auto body_scope = nested_scope ? result_.names.body_scope(statement.id) : scope;
      const auto alternative_scope = nested_scope && !statement.alternative.empty()
                                         ? result_.names.alternative_scope(statement.id)
                                         : scope;
      bind_statements(statement.body, body_scope);
      bind_statements(statement.alternative, alternative_scope);
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
                        const bool local, const std::string_view stage,
                        std::vector<Diagnostic>& diagnostics) {
  const auto* use = names.use(statement.id, role, ordinal);
  const auto* symbol = use == nullptr ? nullptr : names.symbol(use->symbol);
  if (use == nullptr || symbol == nullptr || use->scope != scope ||
      (local && symbol->scope != scope) ||
      (!local && !scope_contains(names, scope, symbol->scope))) {
    add_error(diagnostics, {statement.line, 1}, stage,
              "statement definition has no resolved symbol in its lexical scope");
  }
}

bool lexical_blocks(const hir::Program& program) noexcept {
  return program.semantics.scope_model == semantic::ScopeModel::lexical_blocks;
}

ScopeId child_scope(const hir::Program& program, const NameTable& names,
                    const hir::Statement& statement, const NameScopeKind kind,
                    const ScopeId fallback) {
  if (!lexical_blocks(program)) return fallback;
  switch (kind) {
    case NameScopeKind::statement: return names.statement_scope(statement.id);
    case NameScopeKind::body: return names.body_scope(statement.id);
    case NameScopeKind::alternative: return names.alternative_scope(statement.id);
    case NameScopeKind::function: return names.function_scope(statement.id);
    case NameScopeKind::global: break;
  }
  return {};
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

void verify_statements(const hir::Program& program, const std::vector<hir::Statement>& statements,
                       const ScopeId scope, const NameTable& names, std::vector<bool>& resident,
                       const std::string_view stage, std::vector<Diagnostic>& diagnostics) {
  for (const auto& statement : statements) {
    resident[statement.id.value()] = true;
    if (statement.implicit_result != semantic::ImplicitResultPolicy::none) {
      require_definition(statement, scope, names, NameRole::assignment, 0, !lexical_blocks(program),
                         stage, diagnostics);
    }
    if (statement.kind == StatementKind::function) {
      require_definition(statement, scope, names, NameRole::declaration, 0, true, stage,
                         diagnostics);
      const auto function_scope = names.function_scope(statement.id);
      const auto* child = names.scope(function_scope);
      if (child == nullptr || child->parent != scope || child->owner != statement.id ||
          child->kind != NameScopeKind::function) {
        add_error(diagnostics, {statement.line, 1}, stage,
                  "function does not own a valid child scope");
        continue;
      }
      for (std::size_t index = 0; index < statement.parameters.size(); ++index) {
        require_definition(statement, function_scope, names, NameRole::parameter, index, true,
                           stage, diagnostics);
      }
      for (std::size_t index = 0; index < statement.return_names.size(); ++index) {
        require_definition(statement, function_scope, names, NameRole::result, index, true, stage,
                           diagnostics);
      }
      for (const auto& expression : statement.parameter_defaults) {
        verify_expression(expression, scope, names, resident, stage, diagnostics);
      }
      verify_statements(program, statement.body, function_scope, names, resident, stage,
                        diagnostics);
      verify_statements(program, statement.alternative, function_scope, names, resident, stage,
                        diagnostics);
      continue;
    }
    switch (statement.kind) {
      case StatementKind::declaration:
        require_definition(statement, scope, names, NameRole::declaration, 0, true, stage,
                           diagnostics);
        break;
      case StatementKind::assignment:
        require_definition(statement, scope, names, NameRole::assignment, 0,
                           !lexical_blocks(program), stage, diagnostics);
        break;
      case StatementKind::multi_assignment:
        for (std::size_t index = 0; index < statement.target_names.size(); ++index) {
          require_definition(statement, scope, names, NameRole::assignment, index,
                             !lexical_blocks(program), stage, diagnostics);
        }
        break;
      case StatementKind::range_loop: {
        verify_statement_expressions(statement, scope, names, resident, stage, diagnostics);
        const auto loop_scope =
            child_scope(program, names, statement, NameScopeKind::statement, scope);
        const auto body_scope = child_scope(program, names, statement, NameScopeKind::body, scope);
        const auto* loop_data = names.scope(loop_scope);
        const auto* body_data = names.scope(body_scope);
        if (lexical_blocks(program) &&
            (loop_data == nullptr || body_data == nullptr || loop_data->parent != scope ||
             body_data->parent != loop_scope || loop_data->owner != statement.id ||
             body_data->owner != statement.id)) {
          add_error(diagnostics, {statement.line, 1}, stage,
                    "range loop does not own valid statement/body scopes");
        }
        require_definition(statement, loop_scope, names, NameRole::loop_variable, 0, true, stage,
                           diagnostics);
        verify_statements(program, statement.body, body_scope, names, resident, stage, diagnostics);
        const auto alternative_scope = lexical_blocks(program) && !statement.alternative.empty()
                                           ? names.alternative_scope(statement.id)
                                           : scope;
        verify_statements(program, statement.alternative, alternative_scope, names, resident, stage,
                          diagnostics);
        continue;
      }
      case StatementKind::for_loop: {
        const auto loop_scope = names.statement_scope(statement.id);
        const auto body_scope = names.body_scope(statement.id);
        const auto* loop_data = names.scope(loop_scope);
        const auto* body_data = names.scope(body_scope);
        if (loop_data == nullptr || body_data == nullptr || loop_data->parent != scope ||
            body_data->parent != loop_scope || loop_data->owner != statement.id ||
            body_data->owner != statement.id || loop_data->kind != NameScopeKind::statement ||
            body_data->kind != NameScopeKind::body) {
          add_error(diagnostics, {statement.line, 1}, stage,
                    "for loop does not own valid statement/body scopes");
        }
        require_definition(statement, loop_scope, names, NameRole::loop_variable, 0, true, stage,
                           diagnostics);
        verify_expression(statement.expression, loop_scope, names, resident, stage, diagnostics);
        verify_expression(statement.secondary_expression, loop_scope, names, resident, stage,
                          diagnostics);
        verify_expression(statement.tertiary_expression, loop_scope, names, resident, stage,
                          diagnostics);
        verify_expression(statement.target_expression, loop_scope, names, resident, stage,
                          diagnostics);
        verify_statements(program, statement.body, body_scope, names, resident, stage, diagnostics);
        verify_statements(program, statement.alternative, scope, names, resident, stage,
                          diagnostics);
        continue;
      } break;
      case StatementKind::function:
      case StatementKind::indexed_assignment:
      case StatementKind::print:
      case StatementKind::break_statement:
      case StatementKind::continue_statement:
      case StatementKind::expression:
      case StatementKind::if_statement:
      case StatementKind::select_case:
      case StatementKind::case_clause:
      case StatementKind::while_loop: break;
      case StatementKind::return_statement:
        for (std::size_t index = 0; index < statement.return_names.size(); ++index) {
          require_definition(statement, scope, names, NameRole::result, index, true, stage,
                             diagnostics);
        }
        break;
    }
    verify_statement_expressions(statement, scope, names, resident, stage, diagnostics);
    const bool scoped_control =
        lexical_blocks(program) && (statement.kind == StatementKind::if_statement ||
                                    statement.kind == StatementKind::select_case ||
                                    statement.kind == StatementKind::case_clause ||
                                    statement.kind == StatementKind::while_loop);
    const auto body_scope = scoped_control ? names.body_scope(statement.id) : scope;
    const auto alternative_scope = scoped_control && !statement.alternative.empty()
                                       ? names.alternative_scope(statement.id)
                                       : scope;
    if (scoped_control) {
      const auto* child = names.scope(body_scope);
      if (child == nullptr || child->parent != scope || child->owner != statement.id ||
          child->kind != NameScopeKind::body) {
        add_error(diagnostics, {statement.line, 1}, stage,
                  "control-flow body does not own a valid lexical scope");
      }
    }
    if (scoped_control && !statement.alternative.empty()) {
      const auto* child = names.scope(alternative_scope);
      if (child == nullptr || child->parent != scope || child->owner != statement.id ||
          child->kind != NameScopeKind::alternative) {
        add_error(diagnostics, {statement.line, 1}, stage,
                  "control-flow alternative does not own a valid lexical scope");
      }
    }
    verify_statements(program, statement.body, body_scope, names, resident, stage, diagnostics);
    verify_statements(program, statement.alternative, alternative_scope, names, resident, stage,
                      diagnostics);
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

ScopeId NameTable::function_scope(const HirNodeId owner) const noexcept {
  return owner.valid() && owner.value() < scope_edges.size() ? scope_edges[owner.value()].function
                                                             : ScopeId{};
}

ScopeId NameTable::statement_scope(const HirNodeId owner) const noexcept {
  return owner.valid() && owner.value() < scope_edges.size() ? scope_edges[owner.value()].statement
                                                             : ScopeId{};
}

ScopeId NameTable::body_scope(const HirNodeId owner) const noexcept {
  return owner.valid() && owner.value() < scope_edges.size() ? scope_edges[owner.value()].body
                                                             : ScopeId{};
}

ScopeId NameTable::alternative_scope(const HirNodeId owner) const noexcept {
  return owner.valid() && owner.value() < scope_edges.size()
             ? scope_edges[owner.value()].alternative
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
      names.scope_edges.size() != names.nodes.size()) {
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
        (scope.parent.valid() && names.scope(scope.parent) == nullptr) ||
        (scope.kind == NameScopeKind::global) != (scope.id == names.global_scope)) {
      add_error(diagnostics, {1, 1}, stage, "scope identity or parent is invalid");
    }
    if (scope.kind != NameScopeKind::global) {
      if (!scope.owner.valid() || scope.owner.value() >= names.scope_edges.size()) {
        add_error(diagnostics, {1, 1}, stage, "non-global scope has no valid HIR owner");
      } else {
        const auto& edges = names.scope_edges[scope.owner.value()];
        const auto linked =
            (scope.kind == NameScopeKind::function && edges.function == scope.id) ||
            (scope.kind == NameScopeKind::statement && edges.statement == scope.id) ||
            (scope.kind == NameScopeKind::body && edges.body == scope.id) ||
            (scope.kind == NameScopeKind::alternative && edges.alternative == scope.id);
        if (!linked) {
          add_error(diagnostics, {1, 1}, stage, "scope is not linked from its owner edge");
        }
      }
    }
    for (std::size_t offset = 0; offset < scope.symbols.size(); ++offset) {
      const auto* symbol = names.symbol(scope.symbols[offset]);
      if (symbol == nullptr || symbol->scope != scope.id || symbol->scope_offset != offset) {
        add_error(diagnostics, {1, 1}, stage, "scope symbol inventory is inconsistent");
      }
    }
  }
  for (std::size_t owner = 0; owner < names.scope_edges.size(); ++owner) {
    const auto verify_edge = [&](const ScopeId id, const NameScopeKind kind) {
      if (!id.valid()) return;
      const auto* scope = names.scope(id);
      if (scope == nullptr || scope->owner.value() != owner || scope->kind != kind) {
        add_error(diagnostics, {1, 1}, stage,
                  "scope edge does not match its HIR owner and scope kind");
      }
    };
    const auto& edges = names.scope_edges[owner];
    verify_edge(edges.function, NameScopeKind::function);
    verify_edge(edges.statement, NameScopeKind::statement);
    verify_edge(edges.body, NameScopeKind::body);
    verify_edge(edges.alternative, NameScopeKind::alternative);
  }
  for (std::size_t index = 1; index < names.symbols.size(); ++index) {
    const auto& symbol = names.symbols[index];
    if (symbol.id.value() != index || names.scope(symbol.scope) == nullptr || symbol.name.empty()) {
      add_error(diagnostics, {1, 1}, stage, "symbol identity, scope, or name is invalid");
    }
  }
  std::vector<bool> resident(names.nodes.size(), false);
  verify_statements(program, program.statements, names.global_scope, names, resident, stage,
                    diagnostics);
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
        if (use.role != NameRole::reference && use.role != NameRole::assignment &&
            symbol->scope != use.scope) {
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
