#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "mir.hpp"

namespace mpf::detail::mir {

enum class MemoryDependenceKind : std::uint8_t { flow, anti, output };

struct MemoryAccessSite {
  InstructionId instruction{};
  std::uint32_t ordinal{0};
  MemoryAccessMode mode{MemoryAccessMode::none};
  bool unknown{false};
};

[[nodiscard]] constexpr bool operator==(const MemoryAccessSite& left,
                                        const MemoryAccessSite& right) noexcept {
  return left.instruction == right.instruction && left.ordinal == right.ordinal &&
         left.mode == right.mode && left.unknown == right.unknown;
}

[[nodiscard]] constexpr bool operator!=(const MemoryAccessSite& left,
                                        const MemoryAccessSite& right) noexcept {
  return !(left == right);
}

struct MemoryDependence {
  MemoryDependenceId id{};
  MemoryAccessSite source;
  MemoryAccessSite target;
  MemoryDependenceKind kind{MemoryDependenceKind::flow};
  AliasClass relation{AliasClass::may_alias};
  bool barrier{false};
  bool loop_carried{false};
};

struct InstructionMemoryDependenceFacts {
  InstructionId origin{};
  std::vector<MemoryDependenceId> incoming;
  std::vector<MemoryDependenceId> outgoing;
};

struct MemoryDependenceTable {
  std::uint64_t mir_revision{0};
  std::size_t instruction_count{0};
  std::size_t dependence_count{0};
  bool complete{false};
  std::vector<InstructionMemoryDependenceFacts> instructions;
  std::vector<MemoryDependence> dependences;

  [[nodiscard]] const InstructionMemoryDependenceFacts* instruction(
      InstructionId id) const noexcept;
  [[nodiscard]] const MemoryDependence* dependence(MemoryDependenceId id) const noexcept;
};

[[nodiscard]] MemoryDependenceTable analyze_memory_dependences(
    const Program& program, const AliasEffectTable& alias_effects);
[[nodiscard]] bool memory_dependences_current(const Program& program,
                                              const AliasEffectTable& alias_effects,
                                              const MemoryDependenceTable& analysis) noexcept;
[[nodiscard]] std::vector<Diagnostic> verify_memory_dependences(
    const Program& program, const AliasEffectTable& alias_effects,
    const MemoryDependenceTable& analysis, std::string_view stage);

}  // namespace mpf::detail::mir
