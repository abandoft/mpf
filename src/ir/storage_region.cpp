#include "storage_region.hpp"

#include <algorithm>
#include <limits>
#include <numeric>

#include "compiler/expression_ast.hpp"

namespace mpf::detail {
namespace {

bool checked_last(const StorageRegionDimension& dimension, std::size_t& last) noexcept {
  if (dimension.stride == 0U) return false;
  if (dimension.count == 0U) {
    last = dimension.first;
    return true;
  }
  const auto steps = dimension.count - 1U;
  if (steps != 0U &&
      dimension.stride > (std::numeric_limits<std::size_t>::max() - dimension.first) / steps) {
    return false;
  }
  last = dimension.first + steps * dimension.stride;
  return true;
}

bool checked_element_count(const std::vector<std::size_t>& shape, std::size_t& count) noexcept {
  count = 1U;
  for (const auto extent : shape) {
    if (extent == dynamic_extent ||
        (extent != 0U && count > std::numeric_limits<std::size_t>::max() / extent)) {
      return false;
    }
    count *= extent;
  }
  return true;
}

bool contains(const StorageRegionDimension& dimension, const std::size_t value) noexcept {
  if (dimension.count == 0U || value < dimension.first) return false;
  std::size_t last = 0U;
  return checked_last(dimension, last) && value <= last &&
         (value - dimension.first) % dimension.stride == 0U;
}

bool dimensions_disjoint(const StorageRegionDimension& left,
                         const StorageRegionDimension& right) noexcept {
  if (left.count == 0U || right.count == 0U) return true;
  std::size_t left_last = 0U;
  std::size_t right_last = 0U;
  if (!checked_last(left, left_last) || !checked_last(right, right_last)) return false;
  if (left_last < right.first || right_last < left.first) return true;
  if (left.count == 1U) return !contains(right, left.first);
  if (right.count == 1U) return !contains(left, right.first);

  const auto divisor = std::gcd(left.stride, right.stride);
  const auto residue =
      left.first >= right.first ? left.first - right.first : right.first - left.first;
  if (residue % divisor != 0U) return true;

  constexpr std::size_t exact_enumeration_limit = 4096U;
  const auto* smaller = &left;
  const auto* larger = &right;
  if (right.count < left.count) std::swap(smaller, larger);
  if (smaller->count <= exact_enumeration_limit) {
    auto value = smaller->first;
    for (std::size_t index = 0; index < smaller->count; ++index) {
      if (contains(*larger, value)) return false;
      if (index + 1U != smaller->count) value += smaller->stride;
    }
    return true;
  }
  return false;
}

}  // namespace

bool operator==(const StorageRegion& left, const StorageRegion& right) noexcept {
  return left.kind == right.kind && left.root_shape == right.root_shape &&
         left.dimensions == right.dimensions;
}

bool operator!=(const StorageRegion& left, const StorageRegion& right) noexcept {
  return !(left == right);
}

StorageRegion full_storage_region(const std::vector<std::size_t>& shape) {
  StorageRegion result;
  result.kind = StorageRegionKind::rectangular;
  result.root_shape = shape;
  result.dimensions.reserve(shape.size());
  for (const auto extent : shape) {
    if (extent == dynamic_extent) return {};
    result.dimensions.push_back({0U, 1U, extent});
  }
  return result;
}

bool valid_storage_region(const StorageRegion& region) noexcept {
  if (region.kind == StorageRegionKind::unknown) {
    return region.root_shape.empty() && region.dimensions.empty();
  }
  if (std::any_of(region.root_shape.begin(), region.root_shape.end(),
                  [](const std::size_t extent) { return extent == dynamic_extent; })) {
    return false;
  }
  if (region.kind == StorageRegionKind::rectangular) {
    if (region.dimensions.size() != region.root_shape.size()) return false;
    for (std::size_t index = 0; index < region.dimensions.size(); ++index) {
      const auto& dimension = region.dimensions[index];
      std::size_t last = 0U;
      if (!checked_last(dimension, last)) return false;
      if (dimension.count != 0U && last >= region.root_shape[index]) return false;
    }
    return true;
  }
  if (region.kind != StorageRegionKind::linearized || region.dimensions.size() != 1U ||
      region.root_shape.size() < 2U) {
    return false;
  }
  std::size_t elements = 0U;
  std::size_t last = 0U;
  return checked_element_count(region.root_shape, elements) &&
         checked_last(region.dimensions.front(), last) &&
         (region.dimensions.front().count == 0U || last < elements);
}

bool empty_storage_region(const StorageRegion& region) noexcept {
  return region.kind != StorageRegionKind::unknown &&
         std::any_of(region.dimensions.begin(), region.dimensions.end(),
                     [](const StorageRegionDimension& dimension) { return dimension.count == 0U; });
}

StorageRegionRelation storage_region_relation(const StorageRegion& left,
                                              const StorageRegion& right) noexcept {
  if (!valid_storage_region(left) || !valid_storage_region(right) ||
      left.kind == StorageRegionKind::unknown || right.kind == StorageRegionKind::unknown ||
      left.kind != right.kind || left.root_shape != right.root_shape ||
      left.dimensions.size() != right.dimensions.size()) {
    return StorageRegionRelation::unknown;
  }
  if (empty_storage_region(left) || empty_storage_region(right)) {
    return StorageRegionRelation::disjoint;
  }
  if (left == right) return StorageRegionRelation::identical;
  for (std::size_t index = 0; index < left.dimensions.size(); ++index) {
    if (dimensions_disjoint(left.dimensions[index], right.dimensions[index])) {
      return StorageRegionRelation::disjoint;
    }
  }
  return StorageRegionRelation::overlaps;
}

}  // namespace mpf::detail
