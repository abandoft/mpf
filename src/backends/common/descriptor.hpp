#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "backends/common/artifact.hpp"
#include "backends/common/pipeline.hpp"
#include "compiler/code_binding.hpp"
#include "compiler/static_string_list.hpp"
#include "ir/mir.hpp"

namespace mpf::detail {

inline constexpr std::uint32_t backend_descriptor_api_version = 6;

enum class BackendOptionKind : std::uint8_t { boolean, enumeration, string };

struct BackendConfigurationField {
  const char* name{nullptr};
  BackendOptionKind kind{BackendOptionKind::string};
  const char* default_value{nullptr};
  StaticStringList allowed_values;
  bool affects_code{false};
};

struct BackendConfigurationSchema {
  std::uint32_t version{1};
  const BackendConfigurationField* fields{nullptr};
  std::size_t field_count{0};
};

struct RuntimeComponent {
  const char* name{nullptr};
  const char* version{nullptr};
  const char* license_spdx{nullptr};
  const char* origin{nullptr};
  const char* integrity{nullptr};
  bool bundled{false};
  bool external{false};
};

struct RuntimeSupplyChainManifest {
  const char* schema{nullptr};
  const RuntimeComponent* components{nullptr};
  std::size_t component_count{0};
};

struct BackendManifest {
  const char* target_standard{"unknown"};
  const char* artifact_schema{"unknown"};
  BackendConfigurationSchema configuration;
  RuntimeSupplyChainManifest runtime;
  bool deterministic{false};
  bool reentrant{false};
};

struct BackendDescriptor {
  std::uint32_t api_version{backend_descriptor_api_version};
  TargetLanguage target{TargetLanguage::javascript};
  const char* name{"unknown"};
  BackendManifest manifest;
  const TargetProfile& (*profile)() noexcept {nullptr};
  const LegalizationTable& (*legalizations)() noexcept {nullptr};
  CodeBindingLookup binding{nullptr};
  std::vector<Diagnostic> (*validate)(const mir::Program& program,
                                      const mir::AliasEffectTable& alias_effects){nullptr};
  BackendLoweringResult (*lower)(const mir::Program& program,
                                 const mir::AliasEffectTable& alias_effects,
                                 const TranspileOptions& options){nullptr};
  std::vector<Diagnostic> (*verify)(const BackendArtifact& artifact){nullptr};
  std::string (*dump)(const BackendArtifact& artifact){nullptr};
  std::string (*emit)(const BackendArtifact& artifact, const TranspileOptions& options){nullptr};
};

}  // namespace mpf::detail
