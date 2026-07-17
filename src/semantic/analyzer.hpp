#pragma once

#include <vector>

#include "flow_analysis.hpp"
#include "ir/hir.hpp"
#include "ir/semantic_table.hpp"
#include "name_analysis.hpp"

namespace mpf::detail {

struct AnalysisResult {
  hir::SemanticTable semantics;
  FlowTable flow;
  NameTable names;
  std::vector<Diagnostic> diagnostics;

  [[nodiscard]] bool empty() const noexcept { return diagnostics.empty(); }
};

// Analysis adopts and writes the frontend-produced, revision-bound dense semantic side table.
// Argument association may structurally normalize call operands, after which HIR IDs and
// side-table slots are compacted together; semantic consumers must use AnalysisResult::semantics.
[[nodiscard]] AnalysisResult analyze_program(hir::Program& program,
                                             hir::SemanticTable semantic_seed);
[[nodiscard]] const char* to_string(ValueType type) noexcept;

}  // namespace mpf::detail
