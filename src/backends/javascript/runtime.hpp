#pragma once

#include <iosfwd>
#include <vector>

#include "lir.hpp"

namespace mpf::detail {

void emit_javascript_runtime(std::ostream& output,
                             const std::vector<javascript::lir::RuntimeFragment>& fragments);

}  // namespace mpf::detail
