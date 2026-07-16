#include <sstream>

#include "mpf/transpiler.hpp"

namespace mpf {

std::string CompilationReport::to_json() const {
  std::ostringstream output;
  output << "{\"sourceBytes\":" << source_bytes << ",\"totalNanoseconds\":" << total_nanoseconds
         << ",\"peakArenaBytes\":" << peak_arena_bytes << ",\"mirOptimization\":{"
         << "\"foldedExpressions\":" << mir_optimization.folded_expressions
         << ",\"retiredExpressions\":" << mir_optimization.retired_expressions
         << ",\"removedInstructions\":" << mir_optimization.removed_instructions
         << ",\"propagatedBlockArguments\":" << mir_optimization.propagated_block_arguments
         << ",\"removedBlocks\":" << mir_optimization.removed_blocks
         << ",\"canonicalizedShapes\":" << mir_optimization.canonicalized_shapes
         << ",\"instructionsBefore\":" << mir_optimization.instructions_before
         << ",\"instructionsAfter\":" << mir_optimization.instructions_after
         << ",\"blocksBefore\":" << mir_optimization.blocks_before
         << ",\"blocksAfter\":" << mir_optimization.blocks_after << "},\"stages\":[";
  for (std::size_t index = 0; index < stages.size(); ++index) {
    if (index != 0) output << ',';
    const auto& stage = stages[index];
    output << "{\"stage\":\"" << stage.stage << "\",\"nodes\":" << stage.nodes
           << ",\"durationNanoseconds\":" << stage.duration_nanoseconds
           << ",\"arenaBytes\":" << stage.arena_bytes << '}';
  }
  output << "]}";
  return output.str();
}

}  // namespace mpf
