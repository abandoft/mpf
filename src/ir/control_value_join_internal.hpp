#pragma once

#include <algorithm>
#include <limits>
#include <utility>

#include "control_value_join.hpp"

namespace mpf::detail::mir::control_join_internal {

struct Contract {
  TypeId type{};
  ShapeId shape{};
};

struct Fact {
  bool defined{false};
  // An invalid type denotes the dynamic/top value domain, not an absent value.
  TypeId type{};
  ShapeData shape;
};

inline TypeId logical_type(const Program& program, const TypeId id) {
  const auto* data = mir::type(program, id);
  const auto logical = data != nullptr && data->kind == TypeKind::reference ? data->referent : id;
  data = mir::type(program, logical);
  return data == nullptr || data->value_type == ValueType::unknown ? TypeId{} : logical;
}

inline Fact fact(const Program& program, const Contract contract) {
  const auto* shape = mir::shape(program, contract.shape);
  if (mir::type(program, contract.type) == nullptr || shape == nullptr) return {};
  return {true, logical_type(program, contract.type).valid() ? contract.type : TypeId{}, *shape};
}

inline bool same_shape(const ShapeData& left, const ShapeData& right) {
  return left.extents == right.extents && left.strides == right.strides &&
         left.layout == right.layout && left.dynamic_rank == right.dynamic_rank;
}

inline bool same_fact(const Fact& left, const Fact& right) {
  return left.defined == right.defined &&
         (!left.defined || (left.type == right.type && same_shape(left.shape, right.shape)));
}

inline void rebuild_strides(ShapeData& shape) {
  shape.strides.assign(shape.extents.size(), 1U);
  std::size_t stride = 1U;
  for (std::size_t offset = 0U; offset < shape.extents.size(); ++offset) {
    const auto axis = shape.layout == semantic::IndexLayout::column_major
                          ? offset
                          : shape.extents.size() - offset - 1U;
    shape.strides[axis] = stride;
    const auto extent = shape.extents[axis];
    if (extent == dynamic_extent || stride == dynamic_extent ||
        (extent != 0U && stride > std::numeric_limits<std::size_t>::max() / extent))
      stride = dynamic_extent;
    else
      stride *= extent;
  }
}

inline Fact join(const Program& program, Fact left, const Fact& right) {
  if (!left.defined) return right;
  if (!right.defined) return left;
  if (left.type != right.type) {
    const auto logical = logical_type(program, left.type);
    left.type = logical == logical_type(program, right.type) ? logical : TypeId{};
  }
  if (same_shape(left.shape, right.shape)) return left;
  if (left.shape.dynamic_rank || right.shape.dynamic_rank ||
      left.shape.layout != right.shape.layout ||
      left.shape.extents.size() != right.shape.extents.size()) {
    left.shape = {{}, {}, semantic::IndexLayout::row_major, true};
    return left;
  }
  for (std::size_t axis = 0U; axis < left.shape.extents.size(); ++axis)
    if (left.shape.extents[axis] != right.shape.extents[axis])
      left.shape.extents[axis] = dynamic_extent;
  rebuild_strides(left.shape);
  return left;
}

}  // namespace mpf::detail::mir::control_join_internal
