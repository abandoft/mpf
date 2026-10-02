#pragma once

#include "ir/hir.hpp"
#include "ir/semantic_table.hpp"

namespace mpf::detail {

void analyze_output_demands(const hir::Program& program, hir::SemanticTable& semantics);

}  // namespace mpf::detail
