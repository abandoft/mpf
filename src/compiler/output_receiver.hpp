#pragma once

#include <cstdint>
#include <string>

#include "mpf/diagnostic.hpp"

namespace mpf::detail {

enum class OutputReceiverKind : std::uint8_t { invalid, binding, discard };

// One record per requested position. A discard owns no source variable or storage.
struct OutputReceiver {
  OutputReceiverKind kind{OutputReceiverKind::invalid};
  std::string name;
  SourceLocation location{};

  [[nodiscard]] bool binds() const noexcept { return kind == OutputReceiverKind::binding; }
  [[nodiscard]] bool valid() const noexcept {
    return location.line != 0U && location.column != 0U &&
           (binds() ? !name.empty() && name != "~"
                    : kind == OutputReceiverKind::discard && name.empty());
  }
  friend bool operator==(const OutputReceiver& left, const OutputReceiver& right) noexcept {
    return left.kind == right.kind && left.name == right.name &&
           left.location.line == right.location.line &&
           left.location.column == right.location.column;
  }
  friend bool operator!=(const OutputReceiver& left, const OutputReceiver& right) noexcept {
    return !(left == right);
  }
};

}  // namespace mpf::detail
