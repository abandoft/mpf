#pragma once

#include <string>

#include "hir.hpp"

namespace mpf::detail {

[[nodiscard]] std::string dump_hir(const hir::Program& program);

}  // namespace mpf::detail
