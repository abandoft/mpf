#pragma once

#include <cstddef>
#include <vector>

#include "ir.hpp"

namespace mpf::detail {

struct FunctionDependencyGraph {
  std::vector<std::size_t> definition_order;
  std::vector<std::vector<std::size_t>> dependencies;
  std::vector<bool> recursive;
};

[[nodiscard]] FunctionDependencyGraph build_function_dependency_graph(
    const std::vector<Statement>& statements);

}  // namespace mpf::detail
