#pragma once

#include <cstddef>
#include <vector>

#include "mir.hpp"
#include "pass_manager.hpp"

namespace mpf::detail::mir {

struct OptimizationStatistics {
  std::size_t folded_expressions{0};
  std::size_t retired_expressions{0};
  std::size_t removed_instructions{0};
  std::size_t propagated_block_arguments{0};
  std::size_t removed_blocks{0};
  std::size_t canonicalized_shapes{0};
  std::size_t instructions_before{0};
  std::size_t instructions_after{0};
  std::size_t blocks_before{0};
  std::size_t blocks_after{0};
};

struct OptimizationResult {
  OptimizationStatistics statistics;
  std::vector<PassInstrumentation> instrumentation;
  std::vector<Diagnostic> diagnostics;
};

[[nodiscard]] OptimizationResult run_default_optimization_pipeline(Program& program);

}  // namespace mpf::detail::mir
