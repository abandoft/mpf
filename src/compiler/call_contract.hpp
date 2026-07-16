#pragma once

#include <cstdint>

namespace mpf::detail {

// Target-independent argument association contract. Backends choose a concrete ABI for each
// transfer, but may not reinterpret whether an actual is borrowed, copied, forwarded, or omitted.
enum class ArgumentTransfer : std::uint8_t {
  value,
  read_only_borrow,
  mutable_borrow_out,
  mutable_borrow_inout,
  copy_out,
  copy_in_out,
  optional_forward_in,
  optional_forward_out,
  optional_forward_inout,
  omitted
};

[[nodiscard]] constexpr bool argument_transfer_reads(const ArgumentTransfer transfer) noexcept {
  return transfer == ArgumentTransfer::value || transfer == ArgumentTransfer::read_only_borrow ||
         transfer == ArgumentTransfer::mutable_borrow_inout ||
         transfer == ArgumentTransfer::copy_in_out ||
         transfer == ArgumentTransfer::optional_forward_in ||
         transfer == ArgumentTransfer::optional_forward_inout;
}

[[nodiscard]] constexpr bool argument_transfer_writes(const ArgumentTransfer transfer) noexcept {
  return transfer == ArgumentTransfer::mutable_borrow_out ||
         transfer == ArgumentTransfer::mutable_borrow_inout ||
         transfer == ArgumentTransfer::copy_out || transfer == ArgumentTransfer::copy_in_out ||
         transfer == ArgumentTransfer::optional_forward_out ||
         transfer == ArgumentTransfer::optional_forward_inout;
}

[[nodiscard]] constexpr bool argument_transfer_copies(const ArgumentTransfer transfer) noexcept {
  return transfer == ArgumentTransfer::copy_out || transfer == ArgumentTransfer::copy_in_out;
}

[[nodiscard]] constexpr bool argument_transfer_forwards_optional(
    const ArgumentTransfer transfer) noexcept {
  return transfer == ArgumentTransfer::optional_forward_in ||
         transfer == ArgumentTransfer::optional_forward_out ||
         transfer == ArgumentTransfer::optional_forward_inout;
}

}  // namespace mpf::detail
