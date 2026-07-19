#pragma once

#include <cstdint>

namespace mpf::detail {

// Logical array storage is orthogonal to element type, shape, and the compiler's StorageId.
// The latter identifies a program object; this enum describes the value representation that
// matrix semantics and target ABI planning must preserve.
enum class ArrayStorageFormat : std::uint8_t { none, unknown, dense, sparse_csc };

[[nodiscard]] constexpr bool array_storage_known(const ArrayStorageFormat format) noexcept {
  return format == ArrayStorageFormat::dense || format == ArrayStorageFormat::sparse_csc;
}

[[nodiscard]] constexpr ArrayStorageFormat join_array_storage_formats(
    const ArrayStorageFormat left, const ArrayStorageFormat right) noexcept {
  if (left == right) return left;
  // `none` is the absence of an array contract (for example an untyped binding before its
  // first assignment), while `unknown` is an array whose runtime representation is unresolved.
  // The former is an identity; the latter must conservatively absorb conflicting information.
  if (left == ArrayStorageFormat::none) return right;
  if (right == ArrayStorageFormat::none) return left;
  return ArrayStorageFormat::unknown;
}

}  // namespace mpf::detail
