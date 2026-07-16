#pragma once

#include <cstdint>
#include <functional>
#include <type_traits>

namespace mpf::detail {

template <typename Tag>
class IrId final {
 public:
  using value_type = std::uint32_t;

  constexpr IrId() noexcept = default;
  explicit constexpr IrId(const value_type value) noexcept : value_(value) {}

  [[nodiscard]] constexpr value_type value() const noexcept { return value_; }
  [[nodiscard]] constexpr bool valid() const noexcept { return value_ != 0; }

  friend constexpr bool operator==(const IrId left, const IrId right) noexcept {
    return left.value_ == right.value_;
  }
  friend constexpr bool operator!=(const IrId left, const IrId right) noexcept {
    return !(left == right);
  }
  friend constexpr bool operator<(const IrId left, const IrId right) noexcept {
    return left.value_ < right.value_;
  }

 private:
  value_type value_{0};
};

struct AstNodeIdTag;
struct HirNodeIdTag;
struct SymbolIdTag;
struct MirFunctionIdTag;
struct BlockIdTag;
struct InstructionIdTag;
struct ValueIdTag;
struct TypeIdTag;
struct ShapeIdTag;
struct StorageIdTag;
struct LirNodeIdTag;
struct RuntimeSymbolIdTag;

using AstNodeId = IrId<AstNodeIdTag>;
using HirNodeId = IrId<HirNodeIdTag>;
using SymbolId = IrId<SymbolIdTag>;
using MirFunctionId = IrId<MirFunctionIdTag>;
using BlockId = IrId<BlockIdTag>;
using InstructionId = IrId<InstructionIdTag>;
using ValueId = IrId<ValueIdTag>;
using TypeId = IrId<TypeIdTag>;
using ShapeId = IrId<ShapeIdTag>;
using StorageId = IrId<StorageIdTag>;
using LirNodeId = IrId<LirNodeIdTag>;
using RuntimeSymbolId = IrId<RuntimeSymbolIdTag>;

template <typename Id>
class IrIdAllocator final {
 public:
  [[nodiscard]] Id next() noexcept { return Id{next_++}; }
  [[nodiscard]] std::size_t count() const noexcept { return static_cast<std::size_t>(next_ - 1U); }

 private:
  typename Id::value_type next_{1};
};

}  // namespace mpf::detail

namespace std {

template <typename Tag>
struct hash<mpf::detail::IrId<Tag>> {
  std::size_t operator()(const mpf::detail::IrId<Tag> id) const noexcept {
    return std::hash<typename mpf::detail::IrId<Tag>::value_type>{}(id.value());
  }
};

}  // namespace std
