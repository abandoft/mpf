#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "mpf/diagnostic.hpp"

namespace mpf::detail {

struct SourceSpan {
  std::size_t offset{0};
  std::size_t length{0};
};

struct SourceLine {
  std::size_t number{1};
  std::size_t byte_offset{0};
  std::size_t indent{0};
  std::string text;
};

class SourceText final {
 public:
  explicit SourceText(std::string_view content, std::string filename = {});

  [[nodiscard]] std::string_view content() const noexcept { return content_; }
  [[nodiscard]] const std::string& filename() const noexcept { return filename_; }
  [[nodiscard]] std::size_t line_count() const noexcept { return line_starts_.size(); }
  [[nodiscard]] SourceLocation location(std::size_t byte_offset) const noexcept;
  [[nodiscard]] std::string_view line_text(std::size_t one_based_line) const noexcept;
  [[nodiscard]] std::vector<SourceLine> lines(bool measure_indentation) const;

 private:
  std::string content_;
  std::string filename_;
  std::vector<std::size_t> line_starts_;
};

}  // namespace mpf::detail
