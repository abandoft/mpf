#pragma once

#include <string_view>
#include <vector>

#include "backends/common/artifact.hpp"
#include "backends/common/pipeline.hpp"
#include "ir/mir.hpp"
#include "lir.hpp"

namespace mpf::detail::javascript {

namespace semantic {

struct Program {
  TargetProfile profile{};
  LegalizationTable legalizations{};
  mpf::detail::semantic::Profile source_semantics{};
  SourceLanguage source_language{SourceLanguage::automatic};
  std::size_t hir_node_count{0};
  mir::Effect effects{};
  bool reads_unknown{false};
  bool writes_unknown{false};
  std::size_t function_summary_count{0};
  std::vector<CodeBinding> bindings;
  lir::RuntimeRequirements runtime;
  std::vector<std::string_view> dependencies;
};

}  // namespace semantic

[[nodiscard]] const TargetProfile& target_profile() noexcept;
[[nodiscard]] const LegalizationTable& legalization_table() noexcept;
[[nodiscard]] BackendLoweringResult lower(const mir::Program& program,
                                          const mir::AliasEffectTable& alias_effects,
                                          const TranspileOptions& options);
[[nodiscard]] std::vector<Diagnostic> verify_semantic_lir(const lir::SemanticProgram& program);
[[nodiscard]] std::vector<Diagnostic> verify_artifact(const BackendArtifact& artifact);

}  // namespace mpf::detail::javascript
