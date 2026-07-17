#pragma once

#include <vector>

#include "backends/common/descriptor.hpp"

namespace mpf::detail {

[[nodiscard]] std::vector<Diagnostic> run_backend_conformance(
    const BackendDescriptor& descriptor, const mir::Program& program,
    const mir::AliasEffectTable& alias_effects, const TranspileOptions& options = {});

}  // namespace mpf::detail
