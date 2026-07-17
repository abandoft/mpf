#pragma once

#include "compiler/binding.hpp"

namespace mpf::detail {

[[nodiscard]] const CodeBinding* javascript_code_binding(IntrinsicId intrinsic) noexcept;

}  // namespace mpf::detail
