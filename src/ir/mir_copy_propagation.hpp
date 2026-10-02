#pragma once

#include "mir_optimization.hpp"

namespace mpf::detail::mir {

// Propagate storage-backed block arguments; semantic value/tuple merges remain intact.
std::vector<Diagnostic> propagate_block_arguments(Program& program,
                                                  OptimizationStatistics& statistics);

}  // namespace mpf::detail::mir
