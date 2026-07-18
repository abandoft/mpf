#include "bindings.hpp"

#include <array>

namespace mpf::detail {
namespace {

constexpr std::array<CodeBinding, static_cast<std::size_t>(IntrinsicId::count)> bindings{{
    {IntrinsicId::none, CodeBindingKind::unavailable, {}},
    {IntrinsicId::absolute, CodeBindingKind::symbol, "Math.abs"},
    {IntrinsicId::arc_cosine, CodeBindingKind::symbol, "Math.acos"},
    {IntrinsicId::arc_sine, CodeBindingKind::symbol, "Math.asin"},
    {IntrinsicId::arc_tangent, CodeBindingKind::symbol, "Math.atan"},
    {IntrinsicId::ceiling, CodeBindingKind::symbol, "Math.ceil"},
    {IntrinsicId::cosine, CodeBindingKind::symbol, "Math.cos"},
    {IntrinsicId::exponential, CodeBindingKind::symbol, "Math.exp"},
    {IntrinsicId::floor, CodeBindingKind::symbol, "Math.floor"},
    {IntrinsicId::logarithm, CodeBindingKind::symbol, "Math.log"},
    {IntrinsicId::maximum, CodeBindingKind::symbol, "Math.max"},
    {IntrinsicId::minimum, CodeBindingKind::symbol, "Math.min"},
    {IntrinsicId::round, CodeBindingKind::symbol, "Math.round"},
    {IntrinsicId::sine, CodeBindingKind::symbol, "Math.sin"},
    {IntrinsicId::square_root, CodeBindingKind::symbol, "Math.sqrt"},
    {IntrinsicId::logical_all, CodeBindingKind::custom, "__mpf_matlab_all"},
    {IntrinsicId::logical_any, CodeBindingKind::custom, "__mpf_matlab_any"},
    {IntrinsicId::sum, CodeBindingKind::custom, "__mpf_sum"},
    {IntrinsicId::tangent, CodeBindingKind::symbol, "Math.tan"},
    {IntrinsicId::not_a_number, CodeBindingKind::constant, "Number.NaN"},
    {IntrinsicId::infinity, CodeBindingKind::constant, "Number.POSITIVE_INFINITY"},
    {IntrinsicId::python_float, CodeBindingKind::custom, "__mpf_py_float"},
    {IntrinsicId::python_length, CodeBindingKind::custom, "length"},
    {IntrinsicId::matlab_length, CodeBindingKind::custom, "__mpf_length"},
    {IntrinsicId::element_count, CodeBindingKind::custom, "__mpf_numel"},
    {IntrinsicId::reshape, CodeBindingKind::custom, "__mpf_reshape"},
    {IntrinsicId::present, CodeBindingKind::custom, "present"},
}};

}  // namespace

const CodeBinding* javascript_code_binding(const IntrinsicId intrinsic) noexcept {
  const auto index = static_cast<std::size_t>(intrinsic);
  if (index >= bindings.size() || bindings[index].intrinsic != intrinsic) return nullptr;
  return &bindings[index];
}

}  // namespace mpf::detail
