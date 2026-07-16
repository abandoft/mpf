#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace mpf {

struct SourceMapSegment {
  std::size_t generated_line{1};
  std::size_t generated_column{1};
  std::size_t source_index{0};
  std::size_t original_line{1};
  std::size_t original_column{1};
};

struct SourceMap {
  unsigned version{3};
  std::string file;
  std::vector<std::string> sources;
  std::vector<SourceMapSegment> segments;

  [[nodiscard]] bool empty() const noexcept { return segments.empty(); }
  [[nodiscard]] std::string mappings() const;
  [[nodiscard]] std::string to_json() const;
};

}  // namespace mpf
