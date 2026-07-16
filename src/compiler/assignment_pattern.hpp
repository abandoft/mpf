#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "expression_ast.hpp"

namespace mpf::detail {

enum class AssignmentPatternKind { invalid, name, sequence, starred_name };

struct AssignmentAccess {
  std::size_t index{0};
  bool list{false};
};

struct AssignmentPattern {
  AssignmentPatternKind kind{AssignmentPatternKind::invalid};
  SourceLocation location{};
  std::string name;
  std::vector<AssignmentPattern> children;
  ValueType type{ValueType::unknown};
  ValueType element_type{ValueType::unknown};
  std::vector<std::size_t> shape;
  ValueType previous_type{ValueType::unknown};
  ValueType previous_element_type{ValueType::unknown};
  std::vector<AssignmentAccess> access_path;
  std::vector<std::vector<AssignmentAccess>> captured_paths;

  [[nodiscard]] bool valid() const noexcept { return kind != AssignmentPatternKind::invalid; }
};

inline void collect_assignment_names(const AssignmentPattern& pattern,
                                     std::vector<std::string>& names) {
  if (pattern.kind == AssignmentPatternKind::name ||
      pattern.kind == AssignmentPatternKind::starred_name) {
    names.push_back(pattern.name);
    return;
  }
  for (const auto& child : pattern.children) collect_assignment_names(child, names);
}

inline void collect_assignment_leaves(AssignmentPattern& pattern,
                                      std::vector<AssignmentPattern*>& leaves) {
  if (pattern.kind == AssignmentPatternKind::name ||
      pattern.kind == AssignmentPatternKind::starred_name) {
    leaves.push_back(&pattern);
    return;
  }
  for (auto& child : pattern.children) collect_assignment_leaves(child, leaves);
}

inline void collect_assignment_leaves(const AssignmentPattern& pattern,
                                      std::vector<const AssignmentPattern*>& leaves) {
  if (pattern.kind == AssignmentPatternKind::name ||
      pattern.kind == AssignmentPatternKind::starred_name) {
    leaves.push_back(&pattern);
    return;
  }
  for (const auto& child : pattern.children) collect_assignment_leaves(child, leaves);
}

}  // namespace mpf::detail
