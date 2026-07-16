#pragma once

#include <cstddef>
#include <string>

#include "mpf/diagnostic.hpp"

namespace mpf::detail {

template <typename Kind>
struct BasicStatementToken {
  Kind kind{};
  std::string text;
  std::size_t begin{0};
  std::size_t end{0};
  SourceLocation location{};
};

}  // namespace mpf::detail
