#pragma once

#include "ir/hir.hpp"
#include "name_analysis.hpp"

namespace mpf::detail {

// Resolve Matlab's bare function invocation only after lexical binding. Call callee names
// remain references; variables, parameters and builtins are never guessed from spelling.
[[nodiscard]] bool normalize_matlab_bare_calls(hir::Program& program, const NameTable& names);

}  // namespace mpf::detail
