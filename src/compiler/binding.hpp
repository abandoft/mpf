#pragma once

#include <string_view>

#include "intrinsic.hpp"

namespace mpf::detail {

enum class CodeBindingKind { unavailable, symbol, constant, custom };

struct CodeBinding {
  IntrinsicId intrinsic{IntrinsicId::none};
  CodeBindingKind kind{CodeBindingKind::unavailable};
  std::string_view code;
};

using CodeBindingLookup = const CodeBinding* (*)(IntrinsicId intrinsic) noexcept;

}  // namespace mpf::detail
