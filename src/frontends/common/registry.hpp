#pragma once

#include <cstddef>
#include <string_view>

#include "frontends/common/descriptor.hpp"

namespace mpf::detail {

[[nodiscard]] const FrontendDescriptor* find_frontend(SourceLanguage language) noexcept;
[[nodiscard]] const FrontendDescriptor* find_frontend(std::string_view name) noexcept;
[[nodiscard]] std::size_t frontend_count() noexcept;
[[nodiscard]] const FrontendDescriptor* frontend_at(std::size_t index) noexcept;
[[nodiscard]] const FrontendDescriptor* detect_frontend(std::string_view source,
                                                        std::string_view filename) noexcept;
[[nodiscard]] bool validate_frontend_catalog(const FrontendDescriptor* const* descriptors,
                                             std::size_t count) noexcept;

}  // namespace mpf::detail
