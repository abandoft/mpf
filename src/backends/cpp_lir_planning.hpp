#pragma once

#include <vector>

#include "cpp_lir.hpp"
#include "mpf/diagnostic.hpp"

namespace mpf::detail::cpp {

void plan_lir_resources(lir::SemanticProgram& program);
void verify_lir_resources(const lir::SemanticProgram& program,
                          std::vector<Diagnostic>& diagnostics);

}  // namespace mpf::detail::cpp
