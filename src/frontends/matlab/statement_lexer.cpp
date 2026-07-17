#include "frontends/matlab/statement_lexer.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace mpf::detail {
namespace {

bool identifier_start(const unsigned char character) noexcept {
  return std::isalpha(character) != 0 || character == '_';
}

bool identifier_continue(const unsigned char character) noexcept {
  return std::isalnum(character) != 0 || character == '_';
}

std::string lower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char character) {
    return static_cast<char>(std::tolower(character));
  });
  return value;
}

MatlabStatementTokenKind keyword_kind(const std::string& word) {
  static const std::unordered_map<std::string, MatlabStatementTokenKind> keywords{
      {"function", MatlabStatementTokenKind::keyword_function},
      {"if", MatlabStatementTokenKind::keyword_if},
      {"elseif", MatlabStatementTokenKind::keyword_elseif},
      {"else", MatlabStatementTokenKind::keyword_else},
      {"end", MatlabStatementTokenKind::keyword_end},
      {"while", MatlabStatementTokenKind::keyword_while},
      {"for", MatlabStatementTokenKind::keyword_for},
      {"switch", MatlabStatementTokenKind::keyword_switch},
      {"case", MatlabStatementTokenKind::keyword_case},
      {"otherwise", MatlabStatementTokenKind::keyword_otherwise},
      {"break", MatlabStatementTokenKind::keyword_break},
      {"continue", MatlabStatementTokenKind::keyword_continue}};
  static const std::unordered_map<std::string, bool> unsupported{
      {"try", true},        {"catch", true},  {"classdef", true},    {"properties", true},
      {"methods", true},    {"events", true}, {"enumeration", true}, {"global", true},
      {"persistent", true}, {"parfor", true}, {"spmd", true},        {"arguments", true}};
  const auto key = lower(word);
  const auto found = keywords.find(key);
  if (found != keywords.end()) return found->second;
  return unsupported.find(key) != unsupported.end() ? MatlabStatementTokenKind::unsupported_keyword
                                                    : MatlabStatementTokenKind::identifier;
}

void append_token(MatlabStatementLine& output, const MatlabStatementTokenKind kind,
                  const std::string_view text, const std::size_t begin, const std::size_t end) {
  output.tokens.push_back({kind,
                           std::string(text.substr(begin, end - begin)),
                           begin,
                           end,
                           {output.source.number, begin + 1}});
}

bool starts_character_vector(const std::string_view text, const std::size_t quote) noexcept {
  std::size_t previous = quote;
  while (previous > 0 && std::isspace(static_cast<unsigned char>(text[previous - 1])) != 0) {
    --previous;
  }
  if (previous == 0) return true;
  const auto character = text[previous - 1];
  const std::string_view prefixes{"=([{,:;+-*/\\^~<>&|"};
  return prefixes.find(character) != std::string_view::npos || character == '\'' ||
         character == '"';
}

bool two_character_operator(const std::string_view value) noexcept {
  return value == "==" || value == "~=" || value == "<=" || value == ">=" || value == "&&" ||
         value == "||" || value == ".*" || value == "./" || value == ".\\" || value == ".^";
}

MatlabStatementLine lex_line(SourceLine line, std::vector<Diagnostic>& diagnostics) {
  MatlabStatementLine output;
  output.source = std::move(line);
  const auto& text = output.source.text;
  const auto view = std::string_view{text};
  std::size_t index = 0;
  while (index < text.size()) {
    const auto character = static_cast<unsigned char>(text[index]);
    if (std::isspace(character) != 0) {
      ++index;
      continue;
    }
    const auto begin = index;
    if (identifier_start(character)) {
      ++index;
      while (index < text.size() && identifier_continue(static_cast<unsigned char>(text[index]))) {
        ++index;
      }
      const auto word = text.substr(begin, index - begin);
      append_token(output, keyword_kind(word), text, begin, index);
      continue;
    }
    if (std::isdigit(character) != 0 ||
        (text[index] == '.' && index + 1 < text.size() &&
         std::isdigit(static_cast<unsigned char>(text[index + 1])) != 0)) {
      ++index;
      while (index < text.size()) {
        const auto next = static_cast<unsigned char>(text[index]);
        if (std::isalnum(next) == 0 && text[index] != '_' && text[index] != '.') break;
        ++index;
      }
      append_token(output, MatlabStatementTokenKind::number, text, begin, index);
      continue;
    }
    const bool case_character_vector =
        text[index] == '\'' && output.tokens.size() == 1 &&
        output.tokens.front().kind == MatlabStatementTokenKind::keyword_case;
    if (text[index] == '\'' && !case_character_vector && !starts_character_vector(text, index)) {
      ++index;
      append_token(output, MatlabStatementTokenKind::transpose, text, begin, index);
      continue;
    }
    if (text[index] == '\'' || text[index] == '"') {
      const auto quote = text[index++];
      bool closed = false;
      while (index < text.size()) {
        if (text[index] == quote) {
          if (index + 1 < text.size() && text[index + 1] == quote) {
            index += 2;
          } else {
            ++index;
            closed = true;
            break;
          }
        } else {
          ++index;
        }
      }
      if (!closed) {
        diagnostics.emplace_back(DiagnosticSeverity::error, "MPF1701",
                                 "unterminated string in Matlab statement token stream",
                                 SourceLocation{output.source.number, begin + 1});
      }
      append_token(output, MatlabStatementTokenKind::string_literal, text, begin, index);
      continue;
    }
    const auto two = index + 1 < text.size() ? view.substr(index, 2) : std::string_view{};
    if (two == ".'") {
      index += 2;
      append_token(output, MatlabStatementTokenKind::transpose, text, begin, index);
      continue;
    }
    if (two_character_operator(two)) {
      index += 2;
      append_token(output, MatlabStatementTokenKind::other, text, begin, index);
      continue;
    }

    MatlabStatementTokenKind kind = MatlabStatementTokenKind::other;
    switch (text[index]) {
      case '(': kind = MatlabStatementTokenKind::left_parenthesis; break;
      case ')': kind = MatlabStatementTokenKind::right_parenthesis; break;
      case '[': kind = MatlabStatementTokenKind::left_bracket; break;
      case ']': kind = MatlabStatementTokenKind::right_bracket; break;
      case '{': kind = MatlabStatementTokenKind::left_brace; break;
      case '}': kind = MatlabStatementTokenKind::right_brace; break;
      case ',': kind = MatlabStatementTokenKind::comma; break;
      case ':': kind = MatlabStatementTokenKind::colon; break;
      case ';': kind = MatlabStatementTokenKind::semicolon; break;
      case '=': kind = MatlabStatementTokenKind::equal; break;
      default: break;
    }
    if (std::iscntrl(character) != 0) {
      diagnostics.emplace_back(DiagnosticSeverity::error, "MPF1702",
                               "invalid control character in Matlab statement",
                               SourceLocation{output.source.number, index + 1});
    }
    ++index;
    append_token(output, kind, text, begin, index);
  }
  output.tokens.push_back({MatlabStatementTokenKind::end,
                           {},
                           text.size(),
                           text.size(),
                           {output.source.number, text.size() + 1}});
  return output;
}

}  // namespace

MatlabStatementLexResult lex_matlab_statements(std::vector<SourceLine> lines) {
  MatlabStatementLexResult result;
  result.lines.reserve(lines.size());
  for (auto& line : lines) {
    result.lines.push_back(lex_line(std::move(line), result.diagnostics));
  }
  return result;
}

const char* to_string(const MatlabStatementTokenKind kind) noexcept {
  switch (kind) {
    case MatlabStatementTokenKind::end: return "end of statement";
    case MatlabStatementTokenKind::identifier: return "identifier";
    case MatlabStatementTokenKind::number: return "number";
    case MatlabStatementTokenKind::string_literal: return "string";
    case MatlabStatementTokenKind::keyword_function: return "function";
    case MatlabStatementTokenKind::keyword_if: return "if";
    case MatlabStatementTokenKind::keyword_elseif: return "elseif";
    case MatlabStatementTokenKind::keyword_else: return "else";
    case MatlabStatementTokenKind::keyword_end: return "end";
    case MatlabStatementTokenKind::keyword_while: return "while";
    case MatlabStatementTokenKind::keyword_for: return "for";
    case MatlabStatementTokenKind::keyword_switch: return "switch";
    case MatlabStatementTokenKind::keyword_case: return "case";
    case MatlabStatementTokenKind::keyword_otherwise: return "otherwise";
    case MatlabStatementTokenKind::keyword_break: return "break";
    case MatlabStatementTokenKind::keyword_continue: return "continue";
    case MatlabStatementTokenKind::unsupported_keyword: return "unsupported keyword";
    case MatlabStatementTokenKind::left_parenthesis: return "(";
    case MatlabStatementTokenKind::right_parenthesis: return ")";
    case MatlabStatementTokenKind::left_bracket: return "[";
    case MatlabStatementTokenKind::right_bracket: return "]";
    case MatlabStatementTokenKind::left_brace: return "{";
    case MatlabStatementTokenKind::right_brace: return "}";
    case MatlabStatementTokenKind::comma: return ",";
    case MatlabStatementTokenKind::colon: return ":";
    case MatlabStatementTokenKind::semicolon: return ";";
    case MatlabStatementTokenKind::equal: return "=";
    case MatlabStatementTokenKind::transpose: return "transpose";
    case MatlabStatementTokenKind::other: return "token";
  }
  return "token";
}

}  // namespace mpf::detail
