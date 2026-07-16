#pragma once

#include <string>

#include "semantic_table.hpp"

namespace mpf::detail {

[[nodiscard]] std::string dump_semantics(const hir::SemanticTable& table);

}  // namespace mpf::detail
