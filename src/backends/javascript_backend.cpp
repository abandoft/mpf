#include "javascript_backend.hpp"

#include "javascript_bindings.hpp"
#include "javascript_emitter.hpp"
#include "javascript_lowering.hpp"
#include "javascript_validator.hpp"

namespace mpf::detail {

const BackendDescriptor& javascript_backend() noexcept {
  static constexpr std::string_view aliases[]{"js"};
  static const BackendDescriptor backend{backend_descriptor_api_version,
                                         TargetLanguage::javascript,
                                         "javascript",
                                         {aliases, std::size(aliases)},
                                         {"ECMAScript-2020", "mpf.javascript.lir.v2", true, true},
                                         &javascript::target_profile,
                                         &javascript::legalization_table,
                                         &javascript_code_binding,
                                         &validate_javascript_capabilities,
                                         &javascript::lower,
                                         &javascript::verify_artifact,
                                         &emit_javascript};
  return backend;
}

}  // namespace mpf::detail
