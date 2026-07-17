#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "ir/mir.hpp"

namespace mpf::detail {

enum class LegalizationAction : std::uint8_t { unspecified, unavailable, direct, rewrite, runtime };

struct TargetProfile {
  TargetLanguage target{TargetLanguage::javascript};
  std::string_view language_standard;
  bool supports_dynamic_values{false};
  bool supports_exceptions{false};
  bool supports_modules{false};
};

constexpr std::size_t mir_opcode_count = static_cast<std::size_t>(mir::Opcode::control) + 1U;

using LegalizationTable = std::array<LegalizationAction, mir_opcode_count>;

[[nodiscard]] inline bool legalization_table_complete(const LegalizationTable& table) noexcept {
  for (std::size_t index = 1; index < table.size(); ++index) {
    if (table[index] == LegalizationAction::unspecified) return false;
  }
  return true;
}

[[nodiscard]] std::vector<Diagnostic> validate_legalizations(const mir::Program& program,
                                                             const LegalizationTable& table,
                                                             std::string_view target_name);

}  // namespace mpf::detail
