#pragma once

#include <cstddef>
#include <string_view>

namespace mpf::detail {

struct StaticStringList {
  const std::string_view* data{nullptr};
  std::size_t size{0};
};

}  // namespace mpf::detail
