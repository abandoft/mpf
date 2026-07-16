#pragma once

#include <vector>

#include "../source/source_text.hpp"

namespace mpf::detail {

struct LogicalSourceResult {
  std::vector<SourceLine> lines;
  std::vector<Diagnostic> diagnostics;
};

[[nodiscard]] LogicalSourceResult normalize_python_source(const SourceText& source);
[[nodiscard]] LogicalSourceResult normalize_matlab_source(const SourceText& source);

}  // namespace mpf::detail
