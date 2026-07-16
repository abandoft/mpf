#pragma once

#include <vector>

#include "../source/source_text.hpp"
#include "mpf/transpiler.hpp"

namespace mpf::detail {

struct FortranLogicalLinesResult {
  FortranSourceForm source_form{FortranSourceForm::free};
  std::vector<SourceLine> lines;
  std::vector<Diagnostic> diagnostics;
};

[[nodiscard]] FortranLogicalLinesResult normalize_fortran_source(const SourceText& source,
                                                                 FortranSourceForm requested_form);

}  // namespace mpf::detail
