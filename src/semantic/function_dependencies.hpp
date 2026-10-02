#pragma once

#include "compiler/function_graph.hpp"
#include "ir/hir.hpp"
#include "name_analysis.hpp"

namespace mpf::detail {

// Use declaration identities from the immutable name table, preserving each language's default
// argument scope and distinguishing functions from shadowing variables.
[[nodiscard]] FunctionDependencyGraph analyze_function_dependencies(const hir::Program& program,
                                                                    const NameTable& names);

}  // namespace mpf::detail
