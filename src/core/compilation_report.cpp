#include <sstream>

#include "mpf/transpiler.hpp"

namespace mpf {

std::string CompilationReport::to_json() const {
  std::ostringstream output;
  output << "{\"sourceBytes\":" << source_bytes << ",\"totalNanoseconds\":" << total_nanoseconds
         << ",\"peakArenaBytes\":" << peak_arena_bytes << ",\"stages\":[";
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
