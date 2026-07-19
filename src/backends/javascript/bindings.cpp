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
    {IntrinsicId::complex_value, CodeBindingKind::custom, "__mpf_complex"},
    {IntrinsicId::conjugate, CodeBindingKind::custom, "__mpf_conj"},
    {IntrinsicId::imaginary_part, CodeBindingKind::custom, "__mpf_imag"},
    {IntrinsicId::real_part, CodeBindingKind::custom, "__mpf_real"},
    {IntrinsicId::imaginary_unit, CodeBindingKind::constant, "__mpf_complex(0, 1)"},
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
    {IntrinsicId::matlab_sparse, CodeBindingKind::custom, "__mpf_sparse"},
    {IntrinsicId::matlab_full, CodeBindingKind::custom, "__mpf_full"},
    {IntrinsicId::matlab_is_sparse, CodeBindingKind::custom, "__mpf_issparse"},
    {IntrinsicId::matlab_nonzero_count, CodeBindingKind::custom, "__mpf_nnz"},
    {IntrinsicId::present, CodeBindingKind::custom, "present"},
}};

}  // namespace

const CodeBinding* javascript_code_binding(const IntrinsicId intrinsic) noexcept {
  const auto index = static_cast<std::size_t>(intrinsic);
  if (index >= bindings.size() || bindings[index].intrinsic != intrinsic) return nullptr;
  return &bindings[index];
}

}  // namespace mpf::detail
