#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "../compiler/code_binding.hpp"
#include "../compiler/descriptor.hpp"
#include "../ir/mir.hpp"
#include "backend_artifact.hpp"
#include "backend_pipeline.hpp"

namespace mpf::detail {

inline constexpr std::uint32_t backend_descriptor_api_version = 3;

struct BackendManifest {
  const char* target_standard{"unknown"};
  const char* artifact_schema{"unknown"};
  bool deterministic{false};
  bool reentrant{false};
};

struct BackendDescriptor {
  std::uint32_t api_version{backend_descriptor_api_version};
  TargetLanguage target{TargetLanguage::javascript};
  const char* name{"unknown"};
  StringViewList aliases;
  BackendManifest manifest;
  const TargetProfile& (*profile)() noexcept {nullptr};
  const LegalizationTable& (*legalizations)() noexcept {nullptr};
  CodeBindingLookup binding{nullptr};
  std::vector<Diagnostic> (*validate)(const mir::Program& program){nullptr};
  BackendLoweringResult (*lower)(const mir::Program& program,
                                 const TranspileOptions& options){nullptr};
  std::vector<Diagnostic> (*verify)(const BackendArtifact& artifact){nullptr};
  std::string (*emit)(const BackendArtifact& artifact, const TranspileOptions& options){nullptr};
};

}  // namespace mpf::detail
