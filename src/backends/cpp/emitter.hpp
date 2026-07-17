#pragma once

#include <string>

#include "backends/common/artifact.hpp"

namespace mpf::detail {

[[nodiscard]] std::string emit_cpp(const BackendArtifact& artifact,
                                   const TranspileOptions& options);

}  // namespace mpf::detail
