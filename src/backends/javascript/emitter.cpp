#include "emitter.hpp"

namespace mpf::detail {

std::string emit_javascript(const BackendArtifact& artifact, const TranspileOptions&) {
  return serialize_chunks(artifact.serialized_chunks());
}

}  // namespace mpf::detail
