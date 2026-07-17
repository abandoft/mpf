#pragma once

#include "lir.hpp"

namespace mpf::detail::javascript {

void plan_lir_representation(lir::SemanticProgram& program);
void verify_lir_representation(const lir::SemanticProgram& program,
                               std::vector<Diagnostic>& diagnostics);

}  // namespace mpf::detail::javascript
