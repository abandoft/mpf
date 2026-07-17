#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "ir/ids.hpp"
#include "mpf/diagnostic.hpp"
#include "mpf/transpiler.hpp"

namespace mpf::detail {

struct RenderMarker {
  std::size_t offset{0};
  SourceLocation source{};
  HirNodeId origin{};
};

struct SourceSegment {
  LirNodeId node{};
  SourceLocation source{};
  HirNodeId origin{};

  [[nodiscard]] bool valid() const noexcept { return node.valid(); }
};

struct SourceSegmentPlan {
  bool valid{false};
  std::vector<SourceSegment> nodes;

  [[nodiscard]] const SourceSegment* find(const LirNodeId node) const noexcept {
    if (!valid || !node.valid() || node.value() >= nodes.size() || !nodes[node.value()].valid()) {
      return nullptr;
    }
    return &nodes[node.value()];
  }
};

struct SerializedChunk {
  std::string text;
  SourceLocation source{};
  HirNodeId origin{};
};

struct RenderedOutput {
  std::string text;
  std::vector<RenderMarker> markers;
};

[[nodiscard]] std::vector<SerializedChunk> materialize_chunks(RenderedOutput output);
[[nodiscard]] std::string serialize_chunks(const std::vector<SerializedChunk>& chunks);

class BackendArtifact {
 public:
  BackendArtifact(const BackendArtifact&) = delete;
  BackendArtifact& operator=(const BackendArtifact&) = delete;
  BackendArtifact(BackendArtifact&&) = delete;
  BackendArtifact& operator=(BackendArtifact&&) = delete;
  virtual ~BackendArtifact() = default;

  [[nodiscard]] virtual TargetLanguage target() const noexcept = 0;
  [[nodiscard]] virtual std::size_t node_count_hint() const noexcept = 0;
  [[nodiscard]] virtual const std::vector<SerializedChunk>& serialized_chunks() const noexcept = 0;
  [[nodiscard]] virtual const std::vector<std::string_view>& dependencies() const noexcept = 0;
  [[nodiscard]] virtual std::string debug_dump() const = 0;

 protected:
  BackendArtifact() = default;
};

[[nodiscard]] std::string dump_backend_artifact(const BackendArtifact& artifact);

struct BackendLoweringResult {
  std::unique_ptr<BackendArtifact> artifact;
  std::vector<Diagnostic> diagnostics;
};

}  // namespace mpf::detail
