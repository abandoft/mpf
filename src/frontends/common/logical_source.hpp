#pragma once

#include <vector>

#include "source/source_text.hpp"

namespace mpf::detail {

struct LogicalSourceResult {
  std::vector<SourceLine> lines;
  std::vector<Diagnostic> diagnostics;
};

}  // namespace mpf::detail
