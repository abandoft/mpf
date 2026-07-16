#pragma once

#include <vector>

#include "../ir/hir.hpp"
#include "../ir/semantic_table.hpp"

namespace mpf::detail {

struct AnalysisResult {
  hir::SemanticTable semantics;
  std::vector<Diagnostic> diagnostics;

  [[nodiscard]] bool empty() const noexcept { return diagnostics.empty(); }
};

// Analysis consumes its transient HIR annotations into the returned side table. After return,
// semantic consumers must use AnalysisResult::semantics rather than HIR fields.
[[nodiscard]] AnalysisResult analyze_program(hir::Program& program);
[[nodiscard]] const char* to_string(ValueType type) noexcept;

}  // namespace mpf::detail
