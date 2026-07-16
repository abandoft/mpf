#include <algorithm>
#include <cstdint>
#include <sstream>
#include <string>

#include "source_map_builder.hpp"

namespace mpf {
namespace {

constexpr char base64_digits[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string encode_vlq(const std::int64_t value) {
  const auto magnitude =
      value < 0 ? static_cast<std::uint64_t>(-(value + 1)) + 1U : static_cast<std::uint64_t>(value);
  auto encoded = (magnitude << 1U) | (value < 0 ? 1U : 0U);
  std::string result;
  do {
    auto digit = encoded & 31U;
    encoded >>= 5U;
    if (encoded != 0) digit |= 32U;
    result.push_back(base64_digits[digit]);
  } while (encoded != 0);
  return result;
}

std::string json_escape(const std::string& value) {
  std::string result;
  result.reserve(value.size() + 2U);
  for (const char raw_character : value) {
    const auto character = static_cast<unsigned char>(raw_character);
    switch (character) {
      case '"': result += "\\\""; break;
      case '\\': result += "\\\\"; break;
      case '\b': result += "\\b"; break;
      case '\f': result += "\\f"; break;
      case '\n': result += "\\n"; break;
      case '\r': result += "\\r"; break;
      case '\t': result += "\\t"; break;
      default:
        if (character < 0x20U) {
          constexpr char hex[] = "0123456789abcdef";
          result += "\\u00";
          result.push_back(hex[(character >> 4U) & 0x0fU]);
          result.push_back(hex[character & 0x0fU]);
        } else {
          result.push_back(static_cast<char>(character));
        }
    }
  }
  return result;
}

}  // namespace

std::string SourceMap::mappings() const {
  std::vector<SourceMapSegment> ordered = segments;
  std::stable_sort(ordered.begin(), ordered.end(), [](const auto& left, const auto& right) {
    return left.generated_line < right.generated_line ||
           (left.generated_line == right.generated_line &&
            left.generated_column < right.generated_column);
  });
  std::string result;
  std::size_t generated_line = 1;
  std::int64_t previous_source = 0;
  std::int64_t previous_original_line = 0;
  std::int64_t previous_original_column = 0;
  std::int64_t previous_generated_column = 0;
  bool first_in_line = true;
  for (const auto& segment : ordered) {
    while (generated_line < segment.generated_line) {
      result.push_back(';');
      ++generated_line;
      previous_generated_column = 0;
      first_in_line = true;
    }
    if (!first_in_line) result.push_back(',');
    const auto generated_column = static_cast<std::int64_t>(segment.generated_column - 1U);
    const auto source = static_cast<std::int64_t>(segment.source_index);
    const auto original_line = static_cast<std::int64_t>(segment.original_line - 1U);
    const auto original_column = static_cast<std::int64_t>(segment.original_column - 1U);
    result += encode_vlq(generated_column - previous_generated_column);
    result += encode_vlq(source - previous_source);
    result += encode_vlq(original_line - previous_original_line);
    result += encode_vlq(original_column - previous_original_column);
    previous_generated_column = generated_column;
    previous_source = source;
    previous_original_line = original_line;
    previous_original_column = original_column;
    first_in_line = false;
  }
  return result;
}

std::string SourceMap::to_json() const {
  std::ostringstream output;
  output << "{\"version\":" << version << ",\"file\":\"" << json_escape(file) << "\",\"sources\":[";
  for (std::size_t index = 0; index < sources.size(); ++index) {
    if (index != 0) output << ',';
    output << '"' << json_escape(sources[index]) << '"';
  }
  output << "],\"names\":[],\"mappings\":\"" << mappings() << "\"}";
  return output.str();
}

}  // namespace mpf

namespace mpf::detail {

SourceMap build_source_map(const std::vector<SerializedChunk>& chunks,
                           const std::string_view source_name,
                           const std::string_view generated_name) {
  SourceMap result;
  result.file = std::string(generated_name);
  result.sources.push_back(source_name.empty() ? "<memory>" : std::string(source_name));
  std::size_t line = 1;
  std::size_t column = 1;
  for (const auto& chunk : chunks) {
    if (chunk.source.line != 0) {
      if (result.segments.empty() || result.segments.back().generated_line != line ||
          result.segments.back().generated_column != column) {
        result.segments.push_back(
            {line, column, 0, chunk.source.line, std::max<std::size_t>(chunk.source.column, 1U)});
      } else {
        result.segments.back().original_line = chunk.source.line;
        result.segments.back().original_column = std::max<std::size_t>(chunk.source.column, 1U);
      }
    }
    for (const char character : chunk.text) {
      if (character == '\n') {
        ++line;
        column = 1;
      } else {
        ++column;
      }
    }
  }
  return result;
}

}  // namespace mpf::detail
