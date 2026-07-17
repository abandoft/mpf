#include "backend_registry.hpp"

#include <array>
#include <cctype>

#if MPF_HAS_JAVASCRIPT_BACKEND
#include "../backends/javascript_backend.hpp"
#endif
#if MPF_HAS_CPP_BACKEND
#include "../backends/cpp_backend.hpp"
#endif

namespace mpf::detail {
namespace {

constexpr std::string_view javascript_module_kinds[]{"script", "esm"};
constexpr BackendConfigurationField javascript_configuration[]{
    {"module_kind",
     BackendOptionKind::enumeration,
     "esm",
     {javascript_module_kinds, std::size(javascript_module_kinds)},
     true},
    {"emit_source_banner", BackendOptionKind::boolean, "true", {}, true}};
constexpr BackendConfigurationField cpp_configuration[]{
    {"emit_source_banner", BackendOptionKind::boolean, "true", {}, true}};
constexpr RuntimeComponent javascript_runtime[]{
    {"mpf-inline-javascript-runtime", "project-version", "LicenseRef-MPF-Project",
     "generated-from-source-tree", "built-in", true, false}};
constexpr RuntimeComponent cpp_runtime[]{{"mpf-inline-cpp-runtime", "project-version",
                                          "LicenseRef-MPF-Project", "generated-from-source-tree",
                                          "built-in", true, false}};

const BackendDescriptor javascript_metadata{
    backend_descriptor_api_version,
    TargetLanguage::javascript,
    "javascript",
    {"ECMAScript-2020",
     "mpf.javascript.lir.v12",
     {1, javascript_configuration, std::size(javascript_configuration)},
     {"mpf.runtime-supply-chain.v1", javascript_runtime, std::size(javascript_runtime)},
     true,
     true},
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr};
const BackendDescriptor cpp_metadata{
    backend_descriptor_api_version,
    TargetLanguage::cpp,
    "cpp",
    {"C++17",
     "mpf.cpp.lir.v12",
     {1, cpp_configuration, std::size(cpp_configuration)},
     {"mpf.runtime-supply-chain.v1", cpp_runtime, std::size(cpp_runtime)},
     true,
     true},
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr};

using BackendFactory = const BackendDescriptor& (*)() noexcept;

struct BackendRegistration {
  const BackendDescriptor* metadata;
  BackendFactory factory;
};

const std::array<BackendRegistration, 2> registrations{{
    {&javascript_metadata,
#if MPF_HAS_JAVASCRIPT_BACKEND
     &javascript_backend
#else
     nullptr
#endif
    },
    {&cpp_metadata,
#if MPF_HAS_CPP_BACKEND
     &cpp_backend
#else
     nullptr
#endif
    },
}};

bool equals_ci(const std::string_view left, const std::string_view right) noexcept {
  if (left.size() != right.size()) return false;
  for (std::size_t index = 0; index < left.size(); ++index) {
    if (std::tolower(static_cast<unsigned char>(left[index])) !=
        std::tolower(static_cast<unsigned char>(right[index]))) {
      return false;
    }
  }
  return true;
}

bool has_name(const BackendDescriptor& descriptor, const std::string_view name) noexcept {
  return equals_ci(descriptor.name, name);
}

}  // namespace

const BackendDescriptor* find_backend(const TargetLanguage target) noexcept {
  for (const auto& registration : registrations) {
    if (registration.metadata->target == target && registration.factory != nullptr) {
      return &registration.factory();
    }
  }
  return nullptr;
}

const BackendDescriptor* find_backend_descriptor(const TargetLanguage target) noexcept {
  for (const auto& registration : registrations) {
    if (registration.metadata->target != target) continue;
    return registration.factory == nullptr ? registration.metadata : &registration.factory();
  }
  return nullptr;
}

const BackendDescriptor* find_backend_descriptor(const std::string_view name) noexcept {
  for (const auto& registration : registrations) {
    if (!has_name(*registration.metadata, name)) continue;
    return registration.factory == nullptr ? registration.metadata : &registration.factory();
  }
  return nullptr;
}

std::size_t backend_descriptor_count() noexcept {
  return registrations.size();
}

const BackendDescriptor* backend_descriptor_at(const std::size_t index) noexcept {
  if (index >= registrations.size()) return nullptr;
  const auto& registration = registrations[index];
  return registration.factory == nullptr ? registration.metadata : &registration.factory();
}

bool validate_backend_catalog(const BackendDescriptor* const* descriptors, const std::size_t count,
                              const bool require_callbacks) noexcept {
  for (std::size_t left = 0; left < count; ++left) {
    const auto* descriptor = descriptors[left];
    if (descriptor == nullptr || descriptor->api_version != backend_descriptor_api_version ||
        descriptor->name == nullptr || descriptor->name[0] == '\0' ||
        descriptor->manifest.target_standard == nullptr ||
        descriptor->manifest.target_standard[0] == '\0' ||
        descriptor->manifest.artifact_schema == nullptr ||
        descriptor->manifest.artifact_schema[0] == '\0' ||
        descriptor->manifest.configuration.version == 0 ||
        descriptor->manifest.configuration.fields == nullptr ||
        descriptor->manifest.configuration.field_count == 0 ||
        descriptor->manifest.runtime.schema == nullptr ||
        descriptor->manifest.runtime.schema[0] == '\0' ||
        descriptor->manifest.runtime.components == nullptr ||
        descriptor->manifest.runtime.component_count == 0 || !descriptor->manifest.deterministic ||
        !descriptor->manifest.reentrant ||
        (require_callbacks &&
         (descriptor->profile == nullptr || descriptor->legalizations == nullptr ||
          descriptor->binding == nullptr || descriptor->validate == nullptr ||
          descriptor->lower == nullptr || descriptor->verify == nullptr ||
          descriptor->dump == nullptr || descriptor->emit == nullptr))) {
      return false;
    }
    for (std::size_t field_index = 0; field_index < descriptor->manifest.configuration.field_count;
         ++field_index) {
      const auto& field = descriptor->manifest.configuration.fields[field_index];
      if (field.name == nullptr || field.name[0] == '\0' || field.default_value == nullptr ||
          (field.kind == BackendOptionKind::enumeration &&
           (field.allowed_values.data == nullptr || field.allowed_values.size == 0))) {
        return false;
      }
      if (field.kind == BackendOptionKind::boolean &&
          std::string_view(field.default_value) != "true" &&
          std::string_view(field.default_value) != "false") {
        return false;
      }
      bool default_allowed = field.kind != BackendOptionKind::enumeration;
      for (std::size_t prior = 0; prior < field_index; ++prior) {
        if (std::string_view(descriptor->manifest.configuration.fields[prior].name) == field.name) {
          return false;
        }
      }
      for (std::size_t value = 0; value < field.allowed_values.size; ++value) {
        if (field.allowed_values.data[value].empty()) return false;
        if (field.allowed_values.data[value] == field.default_value) default_allowed = true;
      }
      if (!default_allowed) return false;
    }
    for (std::size_t component_index = 0;
         component_index < descriptor->manifest.runtime.component_count; ++component_index) {
      const auto& component = descriptor->manifest.runtime.components[component_index];
      if (component.name == nullptr || component.name[0] == '\0' || component.version == nullptr ||
          component.version[0] == '\0' || component.license_spdx == nullptr ||
          component.license_spdx[0] == '\0' || component.origin == nullptr ||
          component.origin[0] == '\0' || component.integrity == nullptr ||
          component.integrity[0] == '\0' || component.bundled == component.external) {
        return false;
      }
    }
    if (require_callbacks && (descriptor->profile().target != descriptor->target ||
                              !legalization_table_complete(descriptor->legalizations()))) {
      return false;
    }
    for (std::size_t right = left + 1; right < count; ++right) {
      const auto* other = descriptors[right];
      if (other == nullptr || descriptor->target == other->target ||
          equals_ci(descriptor->name, other->name)) {
        return false;
      }
    }
  }
  return true;
}

}  // namespace mpf::detail
