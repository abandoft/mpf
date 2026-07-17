#pragma once

#include <string_view>
#include <vector>

#include "backends/common/artifact.hpp"
#include "mpf/source_map.hpp"

namespace mpf::detail {

[[nodiscard]] SourceMap build_source_map(const std::vector<SerializedChunk>& chunks,
                                         std::string_view source_name,
                                         std::string_view generated_name);

}  // namespace mpf::detail
