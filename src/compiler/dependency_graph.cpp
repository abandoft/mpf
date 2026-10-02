#include <algorithm>
#include <limits>

#include "function_graph.hpp"

namespace mpf::detail {

void analyze_function_dependencies(FunctionDependencyGraph& graph,
                                   const std::vector<std::size_t>& function_indices) {
  constexpr auto unvisited = std::numeric_limits<std::size_t>::max();
  struct Frame {
    std::size_t function{0U};
    std::size_t next_dependency{0U};
  };
  const auto count = graph.dependencies.size();
  graph.definition_order.clear();
  graph.definition_order.reserve(function_indices.size());
  graph.recursive.assign(count, false);
  std::vector<std::size_t> discovery(count, unvisited);
  std::vector<std::size_t> lowlink(count, unvisited);
  std::vector<bool> active(count, false);
  std::vector<std::size_t> component_stack;
  std::vector<std::size_t> component;
  std::vector<Frame> traversal;
  component_stack.reserve(function_indices.size());
  component.reserve(function_indices.size());
  traversal.reserve(function_indices.size());
  std::size_t next_discovery = 0U;
  const auto enter = [&](const std::size_t index) {
    discovery[index] = next_discovery;
    lowlink[index] = next_discovery++;
    active[index] = true;
    component_stack.push_back(index);
    traversal.push_back({index, 0U});
  };
  for (const auto root : function_indices) {
    if (discovery[root] != unvisited) continue;
    enter(root);
    while (!traversal.empty()) {
      auto& frame = traversal.back();
      const auto index = frame.function;
      const auto& dependencies = graph.dependencies[index];
      if (frame.next_dependency < dependencies.size()) {
        const auto dependency = dependencies[frame.next_dependency++];
        if (discovery[dependency] == unvisited) {
          enter(dependency);
        } else if (active[dependency]) {
          lowlink[index] = std::min(lowlink[index], discovery[dependency]);
        }
        continue;
      }
      graph.definition_order.push_back(index);
      traversal.pop_back();
      if (!traversal.empty()) {
        const auto parent = traversal.back().function;
        lowlink[parent] = std::min(lowlink[parent], lowlink[index]);
      }
      if (lowlink[index] != discovery[index]) continue;
      component.clear();
      while (true) {
        const auto member = component_stack.back();
        component_stack.pop_back();
        active[member] = false;
        component.push_back(member);
        if (member == index) break;
      }
      const bool recursive =
          component.size() > 1U ||
          std::find(dependencies.begin(), dependencies.end(), index) != dependencies.end();
      for (const auto function : component) graph.recursive[function] = recursive;
    }
  }
}

}  // namespace mpf::detail
