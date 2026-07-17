#pragma once

#include <vector>

#include "lir.hpp"
#include "mpf/diagnostic.hpp"

namespace mpf::detail::javascript {

void plan_lir_resources(lir::SemanticProgram& program, const TranspileOptions& options);
void verify_lir_resources(const lir::SemanticProgram& program,
                          std::vector<Diagnostic>& diagnostics);

}  // namespace mpf::detail::javascript
