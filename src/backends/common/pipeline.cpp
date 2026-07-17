#include "backends/common/pipeline.hpp"

#include <string>

namespace mpf::detail {

std::vector<Diagnostic> validate_legalizations(const mir::Program& program,
                                               const LegalizationTable& table,
                                               const std::string_view target_name) {
  std::vector<Diagnostic> diagnostics;
  for (std::size_t index = 1; index < program.instructions.size(); ++index) {
    const auto& instruction = program.instructions[index];
    const auto opcode = static_cast<std::size_t>(instruction.opcode);
    const auto action = opcode < table.size() ? table[opcode] : LegalizationAction::unspecified;
    if (action == LegalizationAction::unspecified || action == LegalizationAction::unavailable) {
      diagnostics.push_back({DiagnosticSeverity::error, "MPF0007",
                             "target '" + std::string(target_name) +
                                 "' has no legal lowering for MIR opcode " + std::to_string(opcode),
                             instruction.location});
    }
  }
  return diagnostics;
}

}  // namespace mpf::detail
