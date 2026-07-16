#pragma once

#include "backend_artifact.hpp"
#include "javascript_lir.hpp"

namespace mpf::detail {

[[nodiscard]] RenderedOutput render_javascript(const javascript::lir::SemanticProgram& program,
                                               const TranspileOptions& options);

}  // namespace mpf::detail
