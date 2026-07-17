#include "backends/common/artifact.hpp"

#include <algorithm>
#include <utility>

namespace mpf::detail {

std::vector<SerializedChunk> materialize_chunks(RenderedOutput output) {
  std::stable_sort(output.markers.begin(), output.markers.end(),
                   [](const RenderMarker& left, const RenderMarker& right) {
                     return left.offset < right.offset;
                   });
  std::vector<RenderMarker> markers;
  markers.reserve(output.markers.size());
  for (const auto& marker : output.markers) {
    if (marker.offset > output.text.size() || marker.source.line == 0) continue;
    if (!markers.empty() && markers.back().offset == marker.offset) {
      markers.back() = marker;
    } else {
      markers.push_back(marker);
    }
  }

  std::vector<SerializedChunk> chunks;
  chunks.reserve(markers.size() + 1U);
  std::size_t cursor = 0;
  SourceLocation source{};
  HirNodeId origin{};
  for (const auto& marker : markers) {
    if (marker.offset > cursor) {
      chunks.push_back({output.text.substr(cursor, marker.offset - cursor), source, origin});
    }
    cursor = marker.offset;
    source = marker.source;
    origin = marker.origin;
  }
  if (cursor < output.text.size() || chunks.empty()) {
    chunks.push_back({output.text.substr(cursor), source, origin});
  }
  return chunks;
}

std::string serialize_chunks(const std::vector<SerializedChunk>& chunks) {
  std::size_t size = 0;
  for (const auto& chunk : chunks) size += chunk.text.size();
  std::string result;
  result.reserve(size);
  for (const auto& chunk : chunks) result += chunk.text;
  return result;
}

std::string dump_backend_artifact(const BackendArtifact& artifact) {
  return artifact.debug_dump();
}

}  // namespace mpf::detail
