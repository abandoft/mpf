#pragma once

#include <iosfwd>

#include "javascript_lir.hpp"

namespace mpf::detail {

void emit_javascript_runtime_fragment(std::ostream& output,
                                      javascript::lir::RuntimeFragment fragment);

}  // namespace mpf::detail
