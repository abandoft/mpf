#pragma once

#include <string_view>
#include <vector>

#include "hir.hpp"
#include "semantic_facts.hpp"

namespace mpf::detail::hir {

// Rebuilds dense HIR identities and remaps an already analyzed side table after structural
// normalization (for example, argument association). No semantic fact is read back from HIR.
[[nodiscard]] SemanticTable reindex_semantics(Program& program, SemanticTable&& table);
[[nodiscard]] std::vector<Diagnostic> verify_semantics(const Program& program,
                                                       const SemanticTable& table,
                                                       std::string_view stage);

}  // namespace mpf::detail::hir
