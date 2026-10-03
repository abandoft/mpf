#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "mir.hpp"

namespace mpf::detail::mir {

// A set of possible binding states, not a runtime Boolean. In particular, joining
// assigned and unassigned paths must not make an output definitely assigned.
enum class OutputAssignmentState : std::uint8_t {
  unreachable = 0U,
  unassigned = 1U,
  assigned = 2U,
  path_dependent = 3U,
  invalid = 4U
};

[[nodiscard]] constexpr OutputAssignmentState join_output_assignment(
    const OutputAssignmentState left, const OutputAssignmentState right) noexcept {
  if (static_cast<std::uint8_t>(left) >
          static_cast<std::uint8_t>(OutputAssignmentState::path_dependent) ||
      static_cast<std::uint8_t>(right) >
          static_cast<std::uint8_t>(OutputAssignmentState::path_dependent))
    return OutputAssignmentState::invalid;
  return static_cast<OutputAssignmentState>(static_cast<std::uint8_t>(left) |
                                            static_cast<std::uint8_t>(right));
}

struct OutputAssignmentSlot {
  std::size_t ordinal{0U};
  SymbolId symbol{};
  StorageId workspace{};
  TypeId workspace_type{};
  ShapeId workspace_shape{};
  TypeId result_type{};
  ShapeId result_shape{};
  bool assigned_at_entry{false};

  friend bool operator==(const OutputAssignmentSlot& left,
                         const OutputAssignmentSlot& right) noexcept {
    return left.ordinal == right.ordinal && left.symbol == right.symbol &&
           left.workspace == right.workspace && left.workspace_type == right.workspace_type &&
           left.workspace_shape == right.workspace_shape && left.result_type == right.result_type &&
           left.result_shape == right.result_shape &&
           left.assigned_at_entry == right.assigned_at_entry;
  }
};

struct FunctionOutputAssignmentFacts {
  MirFunctionId origin{};
  MirStatementId owner{};
  std::vector<OutputAssignmentSlot> outputs;
};

struct BlockOutputAssignmentFacts {
  BlockId origin{};
  MirFunctionId function{};
  // Three adjacent rows: entry, normal exit, and pre-instruction exceptional exit.
  std::size_t state_offset{dynamic_extent};
};

struct OutputAssignmentWrite {
  InstructionId instruction{};
  std::size_t output{0U};
  std::size_t memory_access{0U};

  friend bool operator==(const OutputAssignmentWrite left,
                         const OutputAssignmentWrite right) noexcept {
    return left.instruction == right.instruction && left.output == right.output &&
           left.memory_access == right.memory_access;
  }
};

struct InstructionOutputAssignmentFacts {
  InstructionId origin{};
  BlockId block{};
  MirFunctionId function{};
  // Two adjacent rows: before and after successful execution. No value is read
  // from the output workspace in order to determine these states.
  std::size_t state_offset{dynamic_extent};
  std::size_t write_offset{0U};
  std::size_t write_count{0U};
};

struct OutputAssignmentTable {
  std::uint64_t mir_revision{0U};
  std::size_t storage_count{0U};
  std::size_t instruction_count{0U};
  std::size_t block_count{0U};
  std::size_t function_count{0U};
  bool complete{false};
  std::vector<FunctionOutputAssignmentFacts> functions;
  std::vector<BlockOutputAssignmentFacts> blocks;
  std::vector<InstructionOutputAssignmentFacts> instructions;
  // Compact, contiguous byte-sized states avoid per-instruction allocations.
  std::vector<OutputAssignmentState> states;
  std::vector<OutputAssignmentWrite> writes;

  [[nodiscard]] OutputAssignmentState block_entry(BlockId block, std::size_t output) const noexcept;
  [[nodiscard]] OutputAssignmentState block_exit(BlockId block, std::size_t output) const noexcept;
  [[nodiscard]] OutputAssignmentState block_exception(BlockId block,
                                                      std::size_t output) const noexcept;
  [[nodiscard]] OutputAssignmentState before(InstructionId instruction,
                                             std::size_t output) const noexcept;
  [[nodiscard]] OutputAssignmentState after(InstructionId instruction,
                                            std::size_t output) const noexcept;
};

[[nodiscard]] OutputAssignmentTable analyze_output_assignments(
    const Program& program, const AliasEffectTable& alias_effects);
[[nodiscard]] bool output_assignments_current(const Program& program,
                                              const AliasEffectTable& alias_effects,
                                              const OutputAssignmentTable& analysis) noexcept;
[[nodiscard]] std::vector<Diagnostic> verify_output_assignments(
    const Program& program, const AliasEffectTable& alias_effects,
    const OutputAssignmentTable& analysis, std::string_view stage);
[[nodiscard]] std::string dump_output_assignments(const OutputAssignmentTable& analysis);

}  // namespace mpf::detail::mir
