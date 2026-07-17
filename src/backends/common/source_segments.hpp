#pragma once

#include <cstddef>
#include <vector>

#include "backends/common/artifact.hpp"

namespace mpf::detail {

inline bool same_source_location(const SourceLocation left, const SourceLocation right) noexcept {
  return left.line == right.line && left.column == right.column;
}

inline bool same_source_segment_plan(const SourceSegmentPlan& left,
                                     const SourceSegmentPlan& right) noexcept {
  if (left.valid != right.valid || left.nodes.size() != right.nodes.size()) return false;
  for (std::size_t index = 0; index < left.nodes.size(); ++index) {
    const auto& left_segment = left.nodes[index];
    const auto& right_segment = right.nodes[index];
    if (left_segment.node != right_segment.node || left_segment.origin != right_segment.origin ||
        !same_source_location(left_segment.source, right_segment.source)) {
      return false;
    }
  }
  return true;
}

inline void bind_source_segment(SourceSegmentPlan& plan, const LirNodeId node,
                                const SourceLocation source, const HirNodeId origin) {
  if (!node.valid() || node.value() >= plan.nodes.size()) return;
  plan.nodes[node.value()] = {node, source, origin};
}

template <typename Expression>
void collect_expression_source_segments(SourceSegmentPlan& plan, const Expression& expression) {
  if (!expression.valid()) return;
  bind_source_segment(plan, expression.id, expression.location, expression.origin);
  for (const auto& child : expression.children) {
    collect_expression_source_segments(plan, child);
  }
}

template <typename Statement>
void collect_statement_source_segments(SourceSegmentPlan& plan,
                                       const std::vector<Statement>& statements) {
  for (const auto& statement : statements) {
    bind_source_segment(plan, statement.id, {statement.line, 1}, statement.origin);
    collect_expression_source_segments(plan, statement.expression);
    collect_expression_source_segments(plan, statement.secondary_expression);
    collect_expression_source_segments(plan, statement.tertiary_expression);
    collect_expression_source_segments(plan, statement.target_expression);
    for (const auto& expression : statement.parameter_defaults) {
      collect_expression_source_segments(plan, expression);
    }
    for (const auto& selector : statement.case_selectors) {
      collect_expression_source_segments(plan, selector.lower);
      collect_expression_source_segments(plan, selector.upper);
    }
    collect_statement_source_segments(plan, statement.body);
    collect_statement_source_segments(plan, statement.alternative);
  }
}

template <typename Statement>
SourceSegmentPlan build_source_segment_plan(const std::vector<Statement>& statements,
                                            const std::size_t node_count) {
  SourceSegmentPlan result;
  result.valid = true;
  result.nodes.resize(node_count + 1U);
  collect_statement_source_segments(result, statements);
  return result;
}

}  // namespace mpf::detail
