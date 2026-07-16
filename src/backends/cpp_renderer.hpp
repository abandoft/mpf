#pragma once

#include "backend_artifact.hpp"
#include "cpp_lir.hpp"

namespace mpf::detail {

[[nodiscard]] RenderedOutput render_cpp(const cpp::lir::SemanticProgram& program,
                                        const TranspileOptions& options);

}  // namespace mpf::detail
