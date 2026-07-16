#pragma once

#include <cstddef>
#include <vector>

namespace mpf::detail {

struct FunctionDependencyGraph {
  std::vector<std::size_t> definition_order;
  std::vector<std::vector<std::size_t>> dependencies;
  std::vector<bool> recursive;
};

}  // namespace mpf::detail
