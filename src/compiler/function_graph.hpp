#pragma once

#include <cstddef>
#include <vector>

namespace mpf::detail {

struct FunctionDependencyGraph {
  std::vector<std::size_t> definition_order;
  std::vector<std::vector<std::size_t>> dependencies;
  std::vector<bool> recursive;
};

// Complete a resolved adjacency inventory with stable callee-first order and SCC-based recursion
// flags. The analysis is iterative and linear in the number of vertices and edges; source program
// depth must not become native call-stack depth.
void analyze_function_dependencies(FunctionDependencyGraph& graph,
                                   const std::vector<std::size_t>& function_indices);

}  // namespace mpf::detail
