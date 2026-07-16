#pragma once

#include <vector>

#include "backend.hpp"

namespace mpf::detail {

[[nodiscard]] std::vector<Diagnostic> run_backend_conformance(const BackendDescriptor& descriptor,
                                                              const mir::Program& program,
                                                              const TranspileOptions& options = {});

}  // namespace mpf::detail
