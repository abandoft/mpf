#pragma once

#include "backends/common/descriptor.hpp"

namespace mpf::detail {

[[nodiscard]] const BackendDescriptor& cpp_backend() noexcept;

}  // namespace mpf::detail
