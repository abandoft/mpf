#pragma once

#include <cstddef>

#include "ids.hpp"

namespace mpf::detail::mir {

// Invocation metadata is separate from source formals and their optional/default ordinals.
// The count is a binary64 scalar, matching the Matlab nargout result representation.
struct InvocationFrame {
  ValueId output_count{};
  TypeId type{};
  ShapeId shape{};

  [[nodiscard]] bool active() const noexcept { return output_count.valid(); }
  friend bool operator==(const InvocationFrame& left, const InvocationFrame& right) noexcept {
    return left.output_count == right.output_count && left.type == right.type &&
           left.shape == right.shape;
  }
  friend bool operator!=(const InvocationFrame& left, const InvocationFrame& right) noexcept {
    return !(left == right);
  }
};

// A typed immediate for the hidden count formal, not an additional source actual argument.
struct InvocationDemand {
  TypeId type{};
  ShapeId shape{};
  std::size_t count{0U};

  [[nodiscard]] bool active() const noexcept { return type.valid(); }
  friend bool operator==(const InvocationDemand& left, const InvocationDemand& right) noexcept {
    return left.type == right.type && left.shape == right.shape && left.count == right.count;
  }
  friend bool operator!=(const InvocationDemand& left, const InvocationDemand& right) noexcept {
    return !(left == right);
  }
};

}  // namespace mpf::detail::mir
