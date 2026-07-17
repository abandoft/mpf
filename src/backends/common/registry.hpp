#pragma once

#include <cstddef>
#include <string_view>

#include "backends/common/descriptor.hpp"

namespace mpf::detail {

[[nodiscard]] const BackendDescriptor* find_backend(TargetLanguage target) noexcept;
[[nodiscard]] const BackendDescriptor* find_backend_descriptor(TargetLanguage target) noexcept;
[[nodiscard]] const BackendDescriptor* find_backend_descriptor(std::string_view name) noexcept;
[[nodiscard]] std::size_t backend_descriptor_count() noexcept;
[[nodiscard]] const BackendDescriptor* backend_descriptor_at(std::size_t index) noexcept;
[[nodiscard]] bool validate_backend_catalog(const BackendDescriptor* const* descriptors,
                                            std::size_t count,
                                            bool require_callbacks = true) noexcept;

}  // namespace mpf::detail
