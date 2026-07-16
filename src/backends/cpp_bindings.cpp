#include "cpp_bindings.hpp"

#include <array>

namespace mpf::detail {
namespace {

constexpr std::array<CodeBinding, static_cast<std::size_t>(IntrinsicId::count)> bindings{{
    {IntrinsicId::none, CodeBindingKind::unavailable, {}},
    {IntrinsicId::absolute, CodeBindingKind::symbol, "std::abs"},
    {IntrinsicId::arc_cosine, CodeBindingKind::symbol, "std::acos"},
    {IntrinsicId::arc_sine, CodeBindingKind::symbol, "std::asin"},
    {IntrinsicId::arc_tangent, CodeBindingKind::symbol, "std::atan"},
    {IntrinsicId::ceiling, CodeBindingKind::symbol, "std::ceil"},
    {IntrinsicId::cosine, CodeBindingKind::symbol, "std::cos"},
    {IntrinsicId::exponential, CodeBindingKind::symbol, "std::exp"},
    {IntrinsicId::floor, CodeBindingKind::symbol, "std::floor"},
    {IntrinsicId::logarithm, CodeBindingKind::symbol, "std::log"},
    {IntrinsicId::maximum, CodeBindingKind::symbol, "std::max"},
    {IntrinsicId::minimum, CodeBindingKind::symbol, "std::min"},
    {IntrinsicId::round, CodeBindingKind::symbol, "std::round"},
    {IntrinsicId::sine, CodeBindingKind::symbol, "std::sin"},
    {IntrinsicId::square_root, CodeBindingKind::symbol, "std::sqrt"},
    {IntrinsicId::sum, CodeBindingKind::custom, "mpf_runtime::sum"},
    {IntrinsicId::tangent, CodeBindingKind::symbol, "std::tan"},
    {IntrinsicId::not_a_number, CodeBindingKind::constant,
     "std::numeric_limits<double>::quiet_NaN()"},
    {IntrinsicId::infinity, CodeBindingKind::constant, "std::numeric_limits<double>::infinity()"},
    {IntrinsicId::python_float, CodeBindingKind::custom, "mpf_runtime::py_float"},
    {IntrinsicId::python_length, CodeBindingKind::custom, "size"},
    {IntrinsicId::matlab_length, CodeBindingKind::custom, "mpf_runtime::length"},
    {IntrinsicId::element_count, CodeBindingKind::custom, "mpf_runtime::numel"},
    {IntrinsicId::reshape, CodeBindingKind::custom, "mpf_runtime::reshape_column_major"},
    {IntrinsicId::present, CodeBindingKind::custom, "has_value"},
}};

}  // namespace

const CodeBinding* cpp_code_binding(const IntrinsicId intrinsic) noexcept {
  const auto index = static_cast<std::size_t>(intrinsic);
  if (index >= bindings.size() || bindings[index].intrinsic != intrinsic) return nullptr;
  return &bindings[index];
}

}  // namespace mpf::detail
