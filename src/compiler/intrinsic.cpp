#include "intrinsic.hpp"

#include <algorithm>
#include <array>

#include "frontends/common/registry.hpp"

namespace mpf::detail {
namespace {

constexpr std::array<IntrinsicDescriptor, static_cast<std::size_t>(IntrinsicId::count)> descriptors{
    {
        {IntrinsicId::none, "none"},
        {IntrinsicId::absolute, "absolute"},
        {IntrinsicId::arc_cosine, "arc_cosine"},
        {IntrinsicId::arc_sine, "arc_sine"},
        {IntrinsicId::arc_tangent, "arc_tangent"},
        {IntrinsicId::ceiling, "ceiling"},
        {IntrinsicId::cosine, "cosine"},
        {IntrinsicId::exponential, "exponential"},
        {IntrinsicId::floor, "floor"},
        {IntrinsicId::logarithm, "logarithm"},
        {IntrinsicId::maximum, "maximum"},
        {IntrinsicId::minimum, "minimum"},
        {IntrinsicId::round, "round"},
        {IntrinsicId::sine, "sine"},
        {IntrinsicId::square_root, "square_root"},
        {IntrinsicId::complex_value, "complex_value"},
        {IntrinsicId::conjugate, "conjugate"},
        {IntrinsicId::imaginary_part, "imaginary_part"},
        {IntrinsicId::real_part, "real_part"},
        {IntrinsicId::imaginary_unit, "imaginary_unit"},
        {IntrinsicId::logical_all, "logical_all"},
        {IntrinsicId::logical_any, "logical_any"},
        {IntrinsicId::sum, "sum"},
        {IntrinsicId::tangent, "tangent"},
        {IntrinsicId::not_a_number, "not_a_number"},
        {IntrinsicId::infinity, "infinity"},
        {IntrinsicId::python_float, "python_float"},
        {IntrinsicId::python_length, "python_length"},
        {IntrinsicId::matlab_length, "matlab_length"},
        {IntrinsicId::element_count, "element_count"},
        {IntrinsicId::reshape, "reshape"},
        {IntrinsicId::present, "present"},
    }};

constexpr std::array<SourceIntrinsicBinding, 18> common_bindings{{
    {"abs", IntrinsicId::absolute},
    {"acos", IntrinsicId::arc_cosine},
    {"asin", IntrinsicId::arc_sine},
    {"atan", IntrinsicId::arc_tangent},
    {"ceil", IntrinsicId::ceiling},
    {"cos", IntrinsicId::cosine},
    {"exp", IntrinsicId::exponential},
    {"floor", IntrinsicId::floor},
    {"inf", IntrinsicId::infinity},
    {"log", IntrinsicId::logarithm},
    {"max", IntrinsicId::maximum},
    {"min", IntrinsicId::minimum},
    {"nan", IntrinsicId::not_a_number},
    {"round", IntrinsicId::round},
    {"sin", IntrinsicId::sine},
    {"sqrt", IntrinsicId::square_root},
    {"sum", IntrinsicId::sum},
    {"tan", IntrinsicId::tangent},
}};

}  // namespace

IntrinsicId lookup_source_intrinsic(const SourceIntrinsicBinding* bindings, const std::size_t count,
                                    const std::string_view source_name) noexcept {
  if (bindings == nullptr || count == 0) return IntrinsicId::none;
  const auto found =
      std::lower_bound(bindings, bindings + count, source_name,
                       [](const SourceIntrinsicBinding& binding, const std::string_view value) {
                         return binding.spelling < value;
                       });
  return found != bindings + count && found->spelling == source_name ? found->intrinsic
                                                                     : IntrinsicId::none;
}

SourceIntrinsicTable mathematical_intrinsic_table() noexcept {
  return {common_bindings.data(), common_bindings.size()};
}

IntrinsicId find_intrinsic(const SourceLanguage language,
                           const std::string_view source_name) noexcept {
  const auto* frontend = find_frontend(language);
  if (frontend == nullptr) return IntrinsicId::none;
  for (std::size_t index = 0; index < frontend->intrinsic_table_count; ++index) {
    const auto& table = frontend->intrinsic_tables[index];
    const auto intrinsic = lookup_source_intrinsic(table.data, table.size, source_name);
    if (intrinsic != IntrinsicId::none) return intrinsic;
  }
  return IntrinsicId::none;
}

const IntrinsicDescriptor* intrinsic_descriptor(const IntrinsicId id) noexcept {
  const auto index = static_cast<std::size_t>(id);
  return index < descriptors.size() ? &descriptors[index] : nullptr;
}

std::size_t intrinsic_count() noexcept {
  return descriptors.size();
}

}  // namespace mpf::detail
