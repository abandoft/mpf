#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "../ir/hir.hpp"

namespace mpf::detail {

enum class FlowNodeKind : std::uint8_t { absent, statement };

struct FlowStatementFacts {
  HirNodeId origin{};
  bool reachable{false};
  bool terminates{false};
  bool body_terminates{false};
  bool alternative_terminates{false};
  std::size_t loop_depth{0};
  std::size_t function_depth{0};
};

struct FlowNodeSlot {
  FlowNodeKind kind{FlowNodeKind::absent};
  std::uint32_t offset{0};
};

// Revision-bound, compact control-flow facts for HIR statements. HirNodeId provides the O(1)
// lookup key while expression IDs remain absent from this statement-only analysis.
struct FlowTable {
  std::uint64_t hir_revision{0};
  std::size_t hir_node_count{0};
  std::vector<FlowNodeSlot> nodes;
  std::vector<FlowStatementFacts> statements;

  [[nodiscard]] const FlowStatementFacts* statement(HirNodeId id) const noexcept;
};

struct FlowAnalysisResult {
  FlowTable flow;
  std::vector<Diagnostic> diagnostics;
};

// This analysis is pure with respect to HIR and can be cached by Program::revision.
[[nodiscard]] FlowAnalysisResult analyze_flow(const hir::Program& program);
[[nodiscard]] std::vector<Diagnostic> verify_flow(const hir::Program& program,
                                                  const FlowTable& flow, std::string_view stage);

}  // namespace mpf::detail
