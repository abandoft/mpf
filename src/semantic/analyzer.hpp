#pragma once

#include <vector>

#include "../ir/hir.hpp"
#include "../ir/semantic_table.hpp"
#include "flow_analysis.hpp"

namespace mpf::detail {

struct AnalysisResult {
  hir::SemanticTable semantics;
  FlowTable flow;
  std::vector<Diagnostic> diagnostics;

  [[nodiscard]] bool empty() const noexcept { return diagnostics.empty(); }
};

// Analysis preallocates and writes its revision-bound dense side table directly. Existing HIR
// semantic payload is never annotated or consumed after initialization. Argument association may
// structurally normalize call operands, after which HIR IDs and side-table slots are compacted
// together; semantic consumers must use AnalysisResult::semantics.
[[nodiscard]] AnalysisResult analyze_program(hir::Program& program);
[[nodiscard]] const char* to_string(ValueType type) noexcept;

}  // namespace mpf::detail
