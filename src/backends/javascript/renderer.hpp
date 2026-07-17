#pragma once

#include "../backend_artifact.hpp"
#include "lir.hpp"

namespace mpf::detail {

[[nodiscard]] RenderedOutput render_javascript(const javascript::lir::SemanticProgram& program);

}  // namespace mpf::detail
