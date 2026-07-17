#pragma once

#include "../backend.hpp"

namespace mpf::detail {

[[nodiscard]] const BackendDescriptor& javascript_backend() noexcept;

}  // namespace mpf::detail
