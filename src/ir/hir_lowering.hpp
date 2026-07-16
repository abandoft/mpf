#pragma once

#include <vector>

#include "hir.hpp"
#include "semantic_facts.hpp"

namespace mpf::detail::hir {

// Atomic frontend ownership transfer: structural HIR and its revision-bound semantic seed.
struct LoweringResult {
  Program program;
  SemanticTable semantics;
  std::vector<Diagnostic> diagnostics;
};

}  // namespace mpf::detail::hir
