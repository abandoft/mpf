#include "cpp_backend.hpp"

#include "cpp_bindings.hpp"
#include "cpp_emitter.hpp"
#include "cpp_lowering.hpp"
#include "cpp_validator.hpp"

namespace mpf::detail {

const BackendDescriptor& cpp_backend() noexcept {
  static constexpr std::string_view aliases[]{"c++"};
  static const BackendDescriptor backend{backend_descriptor_api_version,
                                         TargetLanguage::cpp,
                                         "cpp",
                                         {aliases, std::size(aliases)},
                                         {"C++17", "mpf.cpp.lir.v2", true, true},
                                         &cpp::target_profile,
                                         &cpp::legalization_table,
                                         &cpp_code_binding,
                                         &validate_cpp_capabilities,
                                         &cpp::lower,
                                         &cpp::verify_artifact,
                                         &emit_cpp};
  return backend;
}

}  // namespace mpf::detail
