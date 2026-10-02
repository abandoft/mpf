#pragma once

#include "lir.hpp"

namespace mpf::detail::cpp {

// Target-owned reconstruction uses resolved SymbolId, never mangled/source spelling.
[[nodiscard]] FunctionDependencyGraph analyze_function_dependencies(
    const std::vector<lir::Statement>& statements);

}  // namespace mpf::detail::cpp
