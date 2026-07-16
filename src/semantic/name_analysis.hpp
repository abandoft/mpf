#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "../ir/hir.hpp"

namespace mpf::detail {

enum class NameRole : std::uint8_t {
  declaration,
  assignment,
  parameter,
  result,
  loop_variable,
  reference
};

enum class NameSymbolKind : std::uint8_t { variable, function, parameter, result, loop_variable };

enum class NameScopeKind : std::uint8_t { global, function, statement, body, alternative };

struct NameUse {
  HirNodeId origin{};
  ScopeId scope{};
  SymbolId symbol{};
  NameRole role{NameRole::reference};
  std::uint32_t ordinal{0};
  BindingKind binding{BindingKind::unresolved};
  IntrinsicId intrinsic{IntrinsicId::none};
};

struct NameNodeSlot {
  std::uint32_t offset{0};
  std::uint32_t count{0};
};

struct NameSymbol {
  SymbolId id{};
  ScopeId scope{};
  HirNodeId declaration{};
  NameSymbolKind kind{NameSymbolKind::variable};
  std::uint32_t scope_offset{0};
  std::string name;
};

struct NameScope {
  ScopeId id{};
  ScopeId parent{};
  HirNodeId owner{};
  NameScopeKind kind{NameScopeKind::global};
  std::vector<SymbolId> symbols;
};

struct NameScopeEdges {
  ScopeId function{};
  ScopeId statement{};
  ScopeId body{};
  ScopeId alternative{};
};

// Immutable name/scope inventory. HIR nodes index compact name-use ranges in O(1), while symbols
// and scopes use dense strong IDs. Mutable type and definite-assignment states live elsewhere.
struct NameTable {
  std::uint64_t hir_revision{0};
  std::size_t hir_node_count{0};
  ScopeId global_scope{};
  std::vector<NameNodeSlot> nodes;
  std::vector<NameUse> uses;
  std::vector<NameSymbol> symbols;
  std::vector<NameScope> scopes;
  std::vector<NameScopeEdges> scope_edges;

  [[nodiscard]] const NameUse* use(HirNodeId origin, NameRole role,
                                   std::size_t ordinal = 0) const noexcept;
  [[nodiscard]] const NameUse* reference(HirNodeId origin) const noexcept;
  [[nodiscard]] const NameSymbol* symbol(SymbolId id) const noexcept;
  [[nodiscard]] const NameScope* scope(ScopeId id) const noexcept;
  [[nodiscard]] ScopeId function_scope(HirNodeId owner) const noexcept;
  [[nodiscard]] ScopeId statement_scope(HirNodeId owner) const noexcept;
  [[nodiscard]] ScopeId body_scope(HirNodeId owner) const noexcept;
  [[nodiscard]] ScopeId alternative_scope(HirNodeId owner) const noexcept;
};

struct NameAnalysisResult {
  NameTable names;
  std::vector<Diagnostic> diagnostics;
};

// Pure, deterministic HIR analysis suitable for AnalysisManager caching by Program::revision.
[[nodiscard]] NameAnalysisResult analyze_names(const hir::Program& program);
[[nodiscard]] std::vector<Diagnostic> verify_names(const hir::Program& program,
                                                   const NameTable& names, std::string_view stage);

}  // namespace mpf::detail
