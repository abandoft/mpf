#include "cpp_backend.hpp"

#include "cpp_bindings.hpp"
#include "cpp_emitter.hpp"
#include "cpp_lowering.hpp"
#include "cpp_validator.hpp"

namespace mpf::detail {

const BackendDescriptor& cpp_backend() noexcept {
  static constexpr std::string_view aliases[]{"c++"};
  static constexpr BackendConfigurationField configuration[]{
      {"emit_source_banner", BackendOptionKind::boolean, "true", {}, true}};
  static constexpr RuntimeComponent runtime_components[]{
      {"mpf-inline-cpp-runtime", "project-version", "LicenseRef-MPF-Project",
       "generated-from-source-tree", "built-in", true, false}};
  static const BackendDescriptor backend{
      backend_descriptor_api_version,
      TargetLanguage::cpp,
      "cpp",
      {aliases, std::size(aliases)},
      {"C++17",
       "mpf.cpp.lir.v10",
       {1, configuration, std::size(configuration)},
       {"mpf.runtime-supply-chain.v1", runtime_components, std::size(runtime_components)},
       true,
       true},
      &cpp::target_profile,
      &cpp::legalization_table,
      &cpp_code_binding,
      &validate_cpp_capabilities,
      &cpp::lower,
      &cpp::verify_artifact,
      &dump_backend_artifact,
      &emit_cpp};
  return backend;
}

}  // namespace mpf::detail
