#include "source_text.hpp"

#include <algorithm>
#include <cctype>
#include <string_view>
#include <utility>

namespace mpf::detail {
namespace {

std::size_t utf8_column(std::string_view text) noexcept {
  std::size_t column = 1;
  for (const char character : text) {
    const auto byte = static_cast<unsigned char>(character);
    if ((byte & 0xC0U) != 0x80U) {
      ++column;
    }
  }
  return column;
}

std::string trim(std::string_view value) {
  std::size_t first = 0;
  while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first])) != 0) {
    ++first;
  }
  std::size_t last = value.size();
  while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1])) != 0) {
    --last;
  }
  return std::string(value.substr(first, last - first));
}

}  // namespace

SourceText::SourceText(const std::string_view content, std::string filename)
    : content_(content), filename_(std::move(filename)), line_starts_{0} {
  for (std::size_t index = 0; index < content_.size(); ++index) {
    if (content_[index] == '\n' && index + 1 < content_.size()) {
      line_starts_.push_back(index + 1);
    }
  }
}

SourceLocation SourceText::location(const std::size_t byte_offset) const noexcept {
  const auto bounded = std::min(byte_offset, content_.size());
  const auto found = std::upper_bound(line_starts_.begin(), line_starts_.end(), bounded);
  const auto line_index =
      found == line_starts_.begin()
          ? std::size_t{0}
          : static_cast<std::size_t>(std::distance(line_starts_.begin(), found) - 1);
  const auto line_start = line_starts_[line_index];
  return {line_index + 1,
          utf8_column(std::string_view(content_).substr(line_start, bounded - line_start))};
}

std::string_view SourceText::line_text(const std::size_t one_based_line) const noexcept {
  if (one_based_line == 0 || one_based_line > line_starts_.size()) {
    return {};
  }
  const auto begin = line_starts_[one_based_line - 1];
  auto end =
      one_based_line < line_starts_.size() ? line_starts_[one_based_line] - 1 : content_.size();
  if (end > begin && content_[end - 1] == '\n') {
    --end;
  }
  if (end > begin && content_[end - 1] == '\r') {
    --end;
  }
  return std::string_view(content_).substr(begin, end - begin);
}

std::vector<SourceLine> SourceText::lines(const bool measure_indentation) const {
  std::vector<SourceLine> result;
  result.reserve(line_starts_.size());
  for (std::size_t index = 0; index < line_starts_.size(); ++index) {
    const auto raw = line_text(index + 1);
    std::size_t indent = 0;
    if (measure_indentation) {
      while (indent < raw.size() && raw[indent] == ' ') {
        ++indent;
      }
    }
    result.push_back({index + 1, line_starts_[index], indent, trim(raw)});
  }
  return result;
}

}  // namespace mpf::detail
