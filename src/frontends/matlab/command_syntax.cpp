#include "frontends/matlab/command_syntax.hpp"

#include <cctype>
#include <string_view>

namespace mpf::detail {
namespace {

bool identifier_start(const unsigned char character) noexcept {
  return std::isalpha(character) != 0 || character == '_';
}

bool identifier_continue(const unsigned char character) noexcept {
  return std::isalnum(character) != 0 || character == '_';
}

bool whitespace(const char character) noexcept {
  return std::isspace(static_cast<unsigned char>(character)) != 0;
}

bool equal_case_insensitive(const std::string_view left, const std::string_view right) noexcept {
  if (left.size() != right.size()) return false;
  for (std::size_t index = 0; index < left.size(); ++index) {
    if (std::tolower(static_cast<unsigned char>(left[index])) !=
        std::tolower(static_cast<unsigned char>(right[index]))) {
      return false;
    }
  }
  return true;
}

bool reserved_command_callee(const std::string_view callee) noexcept {
  constexpr std::string_view reserved[]{
      "function", "if",         "elseif",    "else",       "end",      "while",  "for",
      "switch",   "case",       "otherwise", "break",      "continue", "return", "try",
      "catch",    "arguments",  "classdef",  "properties", "methods",  "events", "enumeration",
      "global",   "persistent", "parfor",    "spmd"};
  for (const auto keyword : reserved) {
    if (equal_case_insensitive(callee, keyword)) return true;
  }
  return false;
}

std::size_t potential_operator_length(const std::string_view source,
                                      const std::size_t begin) noexcept {
  if (begin >= source.size()) return 0;
  if (begin + 1U < source.size()) {
    const auto pair = source.substr(begin, 2U);
    if (pair == "==" || pair == "~=" || pair == "<=" || pair == ">=" || pair == "&&" ||
        pair == "||" || pair == ".*" || pair == "./" || pair == ".\\" || pair == ".^" ||
        pair == ".'") {
      return 2U;
    }
  }
  constexpr std::string_view operators{"+-*/\\^~<>&|.:"};
  return operators.find(source[begin]) == std::string_view::npos ? 0U : 1U;
}

void finish_argument(MatlabCommandSyntax& syntax, std::string& value, const std::size_t begin,
                     const std::size_t end, const bool quoted) {
  if (value.empty() && begin == end) return;
  syntax.arguments.push_back(
      {std::move(value), begin, end,
       quoted ? MatlabCommandArgumentForm::single_quoted : MatlabCommandArgumentForm::unquoted});
  value.clear();
}

}  // namespace

std::optional<MatlabCommandSyntax> scan_matlab_command_syntax(const std::string_view source) {
  std::size_t index = 0;
  while (index < source.size() && whitespace(source[index])) ++index;
  if (index >= source.size() || !identifier_start(static_cast<unsigned char>(source[index]))) {
    return std::nullopt;
  }

  MatlabCommandSyntax result;
  result.callee_begin = index;
  ++index;
  while (index < source.size() && identifier_continue(static_cast<unsigned char>(source[index]))) {
    ++index;
  }
  result.callee_end = index;
  result.callee =
      std::string(source.substr(result.callee_begin, result.callee_end - result.callee_begin));
  if (reserved_command_callee(result.callee)) return std::nullopt;

  if (index >= source.size() || !whitespace(source[index])) return std::nullopt;
  while (index < source.size() && whitespace(source[index])) ++index;
  if (index >= source.size() || source[index] == '=' || source[index] == '(') {
    return std::nullopt;
  }

  // Matlab resolves an operator-looking tail by spacing. A space after the potential operator
  // keeps expression syntax; an adjacent operand makes the tail command text.
  const auto operator_length = potential_operator_length(source, index);
  if (operator_length != 0U && index + operator_length < source.size() &&
      whitespace(source[index + operator_length])) {
    return std::nullopt;
  }

  std::size_t command_end = source.size();
  while (command_end > index && whitespace(source[command_end - 1U])) --command_end;
  if (command_end > index && source[command_end - 1U] == ';') --command_end;
  while (command_end > index && whitespace(source[command_end - 1U])) --command_end;

  std::string value;
  std::size_t argument_begin = index;
  bool in_single_quote = false;
  bool argument_quoted = false;
  while (index < command_end) {
    const auto character = source[index];
    if (character == '\'' && in_single_quote) {
      if (index + 1U < command_end && source[index + 1U] == '\'') {
        value.push_back('\'');
        index += 2U;
        continue;
      }
      in_single_quote = false;
      ++index;
      continue;
    }
    if (character == '\'' && !in_single_quote) {
      in_single_quote = true;
      argument_quoted = true;
      ++index;
      continue;
    }
    if (!in_single_quote && whitespace(character)) {
      finish_argument(result, value, argument_begin, index, argument_quoted);
      while (index < source.size() && whitespace(source[index])) ++index;
      argument_begin = index;
      argument_quoted = false;
      continue;
    }
    value.push_back(character);
    ++index;
  }
  finish_argument(result, value, argument_begin, index, argument_quoted);
  result.terminated = !in_single_quote;
  return result;
}

}  // namespace mpf::detail
