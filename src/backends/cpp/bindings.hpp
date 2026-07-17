#pragma once

#include "compiler/binding.hpp"

namespace mpf::detail {

[[nodiscard]] const CodeBinding* cpp_code_binding(IntrinsicId intrinsic) noexcept;

}  // namespace mpf::detail
