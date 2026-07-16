#pragma once

#include <iosfwd>
#include <string_view>
#include <vector>

#include "cpp_lir.hpp"

namespace mpf::detail {

void emit_cpp_runtime(std::ostream& output, std::string_view runtime_namespace,
                      const std::vector<cpp::lir::RuntimeFragment>& fragments);

}  // namespace mpf::detail
