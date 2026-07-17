#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace mpf::detail {

enum class StorageRegionKind : std::uint8_t { unknown, rectangular, linearized };

struct StorageRegionDimension {
  std::size_t first{0};
  std::size_t stride{1};
  std::size_t count{0};
};

[[nodiscard]] constexpr bool operator==(const StorageRegionDimension& left,
                                        const StorageRegionDimension& right) noexcept {
  return left.first == right.first && left.stride == right.stride && left.count == right.count;
}

struct StorageRegion {
  StorageRegionKind kind{StorageRegionKind::unknown};
  std::vector<std::size_t> root_shape;
  std::vector<StorageRegionDimension> dimensions;
};

[[nodiscard]] bool operator==(const StorageRegion& left, const StorageRegion& right) noexcept;
[[nodiscard]] bool operator!=(const StorageRegion& left, const StorageRegion& right) noexcept;

enum class StorageRegionRelation : std::uint8_t { unknown, disjoint, overlaps, identical };

[[nodiscard]] StorageRegion full_storage_region(const std::vector<std::size_t>& shape);
[[nodiscard]] bool valid_storage_region(const StorageRegion& region) noexcept;
[[nodiscard]] bool empty_storage_region(const StorageRegion& region) noexcept;
[[nodiscard]] StorageRegionRelation storage_region_relation(const StorageRegion& left,
                                                            const StorageRegion& right) noexcept;

}  // namespace mpf::detail
