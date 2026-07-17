#pragma once

#include "backends/common/artifact.hpp"
#include "lir.hpp"

namespace mpf::detail {

[[nodiscard]] RenderedOutput render_cpp(const cpp::lir::SemanticProgram& program);

}  // namespace mpf::detail
