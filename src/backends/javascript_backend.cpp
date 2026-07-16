#include "javascript_backend.hpp"

#include "javascript_bindings.hpp"
#include "javascript_emitter.hpp"
#include "javascript_lowering.hpp"
#include "javascript_validator.hpp"

namespace mpf::detail {

const BackendDescriptor& javascript_backend() noexcept {
  static constexpr std::string_view aliases[]{"js"};
  static constexpr std::string_view module_kinds[]{"script", "esm"};
  static constexpr BackendConfigurationField configuration[]{
      {"module_kind",
       BackendOptionKind::enumeration,
       "esm",
       {module_kinds, std::size(module_kinds)},
       true},
      {"emit_source_banner", BackendOptionKind::boolean, "true", {}, true}};
  static constexpr RuntimeComponent runtime_components[]{
      {"mpf-inline-javascript-runtime", "project-version", "LicenseRef-MPF-Project",
       "generated-from-source-tree", "built-in", true, false}};
  static const BackendDescriptor backend{
      backend_descriptor_api_version,
      TargetLanguage::javascript,
      "javascript",
      {aliases, std::size(aliases)},
      {"ECMAScript-2020",
       "mpf.javascript.lir.v8",
       {1, configuration, std::size(configuration)},
       {"mpf.runtime-supply-chain.v1", runtime_components, std::size(runtime_components)},
       true,
       true},
      &javascript::target_profile,
      &javascript::legalization_table,
      &javascript_code_binding,
      &validate_javascript_capabilities,
      &javascript::lower,
      &javascript::verify_artifact,
      &dump_backend_artifact,
      &emit_javascript};
  return backend;
}

}  // namespace mpf::detail
