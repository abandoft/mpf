#pragma once

#include "backends/common/descriptor.hpp"

namespace mpf::detail {

[[nodiscard]] const BackendDescriptor& javascript_backend() noexcept;

}  // namespace mpf::detail
