#pragma once

#include <cstddef>
#include <string_view>

#include "mpf/transpiler.hpp"

namespace mpf::detail {

enum class IntrinsicId : std::size_t {
  none,
  absolute,
  arc_cosine,
  arc_sine,
  arc_tangent,
  ceiling,
  cosine,
  exponential,
  floor,
  logarithm,
  maximum,
  minimum,
  round,
  sine,
  square_root,
  sum,
  tangent,
  not_a_number,
  infinity,
  python_float,
  python_length,
  matlab_length,
  element_count,
  reshape,
  present,
  count
};

struct IntrinsicDescriptor {
  IntrinsicId id{IntrinsicId::none};
  std::string_view name;
};

struct SourceIntrinsicBinding {
  std::string_view spelling;
  IntrinsicId intrinsic{IntrinsicId::none};
};

struct SourceIntrinsicTable {
  const SourceIntrinsicBinding* data{nullptr};
  std::size_t size{0};
};

[[nodiscard]] IntrinsicId lookup_source_intrinsic(const SourceIntrinsicBinding* bindings,
                                                  std::size_t count,
                                                  std::string_view source_name) noexcept;
[[nodiscard]] SourceIntrinsicTable mathematical_intrinsic_table() noexcept;

[[nodiscard]] IntrinsicId find_intrinsic(SourceLanguage language,
                                         std::string_view source_name) noexcept;
[[nodiscard]] const IntrinsicDescriptor* intrinsic_descriptor(IntrinsicId id) noexcept;
[[nodiscard]] std::size_t intrinsic_count() noexcept;

}  // namespace mpf::detail
