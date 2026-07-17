#pragma once

#include "../backend.hpp"

namespace mpf::detail {

[[nodiscard]] const BackendDescriptor& cpp_backend() noexcept;

}  // namespace mpf::detail
