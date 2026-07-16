#pragma once

#include <string>

#include "backend_artifact.hpp"

namespace mpf::detail {

[[nodiscard]] std::string emit_javascript(const BackendArtifact& artifact,
                                          const TranspileOptions& options);

}  // namespace mpf::detail
