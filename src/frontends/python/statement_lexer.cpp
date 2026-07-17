#include "frontends/python/statement_lexer.hpp"

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

std::size_t utf8_column(const std::string_view text, const std::size_t byte_offset,
                        const std::size_t base_column) noexcept {
  auto column = base_column;
  for (std::size_t index = 0; index < byte_offset; ++index) {
    if ((static_cast<unsigned char>(text[index]) & 0xC0U) != 0x80U) ++column;
  }
  return column;
}

PythonStatementTokenKind keyword_kind(const std::string& word) {
  static const std::unordered_map<std::string, PythonStatementTokenKind> keywords{
      {"def", PythonStatementTokenKind::keyword_def},
      {"if", PythonStatementTokenKind::keyword_if},
      {"elif", PythonStatementTokenKind::keyword_elif},
      {"else", PythonStatementTokenKind::keyword_else},
      {"while", PythonStatementTokenKind::keyword_while},
      {"for", PythonStatementTokenKind::keyword_for},
      {"in", PythonStatementTokenKind::keyword_in},
      {"return", PythonStatementTokenKind::keyword_return},
      {"break", PythonStatementTokenKind::keyword_break},
      {"continue", PythonStatementTokenKind::keyword_continue}};
  static const std::unordered_map<std::string, bool> unsupported{
      {"class", true},  {"import", true},   {"from", true},   {"try", true},   {"with", true},
      {"match", true},  {"async", true},    {"raise", true},  {"yield", true}, {"pass", true},
      {"global", true}, {"nonlocal", true}, {"assert", true}, {"del", true},   {"await", true}};
  const auto found = keywords.find(word);
  if (found != keywords.end()) return found->second;
  return unsupported.find(word) != unsupported.end() ? PythonStatementTokenKind::unsupported_keyword
                                                     : PythonStatementTokenKind::identifier;
}

void append_token(PythonStatementLine& output, const PythonStatementTokenKind kind,
                  const std::string_view text, const std::size_t begin, const std::size_t end) {
  output.tokens.push_back(
      {kind,
       std::string(text.substr(begin, end - begin)),
       begin,
       end,
       {output.source.number, utf8_column(text, begin, output.source.indent + 1)}});
}

bool is_two_character_operator(const std::string_view value) noexcept {
  return value == "==" || value == "!=" || value == "<=" || value == ">=" || value == "+=" ||
         value == "-=" || value == "*=" || value == "/=" || value == "%=" || value == "&=" ||
         value == "|=" || value == "^=" || value == "@=" || value == ":=" || value == "//" ||
         value == "**" || value == "<<" || value == ">>";
}

PythonStatementLine lex_line(SourceLine line, std::vector<Diagnostic>& diagnostics) {
  PythonStatementLine output;
  output.source = std::move(line);
  const auto& text = output.source.text;
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
      append_token(output, PythonStatementTokenKind::number, text, begin, index);
      continue;
    }
    if (text[index] == '\'' || text[index] == '"') {
      const auto quote = text[index++];
      bool closed = false;
      while (index < text.size()) {
        if (text[index] == '\\' && index + 1 < text.size()) {
          index += 2;
        } else if (text[index] == quote) {
          ++index;
          closed = true;
          break;
        } else {
          ++index;
        }
      }
      if (!closed) {
        diagnostics.emplace_back(
            DiagnosticSeverity::error, "MPF1601",
            "unterminated string in Python statement token stream",
            SourceLocation{output.source.number,
                           utf8_column(text, begin, output.source.indent + 1)});
      }
      append_token(output, PythonStatementTokenKind::string_literal, text, begin, index);
      continue;
    }

    const auto view = std::string_view{text};
    const auto three = index + 2 < text.size() ? view.substr(index, 3) : std::string_view{};
    const auto two = index + 1 < text.size() ? view.substr(index, 2) : std::string_view{};
    if (three == "//=" || three == "**=" || three == "<<=" || three == ">>=") {
      index += 3;
      append_token(output, PythonStatementTokenKind::other, text, begin, index);
      continue;
    }
    if (two == "->") {
      index += 2;
      append_token(output, PythonStatementTokenKind::arrow, text, begin, index);
      continue;
    }
    if (is_two_character_operator(two)) {
      index += 2;
      append_token(output, PythonStatementTokenKind::other, text, begin, index);
      continue;
    }

    PythonStatementTokenKind kind = PythonStatementTokenKind::other;
    switch (text[index]) {
      case '(': kind = PythonStatementTokenKind::left_parenthesis; break;
      case ')': kind = PythonStatementTokenKind::right_parenthesis; break;
      case '[': kind = PythonStatementTokenKind::left_bracket; break;
      case ']': kind = PythonStatementTokenKind::right_bracket; break;
      case '{': kind = PythonStatementTokenKind::left_brace; break;
      case '}': kind = PythonStatementTokenKind::right_brace; break;
      case ',': kind = PythonStatementTokenKind::comma; break;
      case ':': kind = PythonStatementTokenKind::colon; break;
      case '=': kind = PythonStatementTokenKind::equal; break;
      case '/': kind = PythonStatementTokenKind::slash; break;
      case '*': kind = PythonStatementTokenKind::star; break;
      default: break;
    }
    if (std::iscntrl(character) != 0) {
      diagnostics.emplace_back(
          DiagnosticSeverity::error, "MPF1602", "invalid control character in Python statement",
          SourceLocation{output.source.number, utf8_column(text, index, output.source.indent + 1)});
    }
    ++index;
    append_token(output, kind, text, begin, index);
  }
  output.tokens.push_back(
      {PythonStatementTokenKind::end,
       {},
       text.size(),
       text.size(),
       {output.source.number, utf8_column(text, text.size(), output.source.indent + 1)}});
  return output;
}

}  // namespace

PythonStatementLexResult lex_python_statements(std::vector<SourceLine> lines) {
  PythonStatementLexResult result;
  result.lines.reserve(lines.size());
  for (auto& line : lines) {
    result.lines.push_back(lex_line(std::move(line), result.diagnostics));
  }
  return result;
}

const char* to_string(const PythonStatementTokenKind kind) noexcept {
  switch (kind) {
    case PythonStatementTokenKind::end: return "end of statement";
    case PythonStatementTokenKind::identifier: return "identifier";
    case PythonStatementTokenKind::number: return "number";
    case PythonStatementTokenKind::string_literal: return "string";
    case PythonStatementTokenKind::keyword_def: return "def";
    case PythonStatementTokenKind::keyword_if: return "if";
    case PythonStatementTokenKind::keyword_elif: return "elif";
    case PythonStatementTokenKind::keyword_else: return "else";
    case PythonStatementTokenKind::keyword_while: return "while";
    case PythonStatementTokenKind::keyword_for: return "for";
    case PythonStatementTokenKind::keyword_in: return "in";
    case PythonStatementTokenKind::keyword_return: return "return";
    case PythonStatementTokenKind::keyword_break: return "break";
    case PythonStatementTokenKind::keyword_continue: return "continue";
    case PythonStatementTokenKind::unsupported_keyword: return "unsupported keyword";
    case PythonStatementTokenKind::left_parenthesis: return "(";
    case PythonStatementTokenKind::right_parenthesis: return ")";
    case PythonStatementTokenKind::left_bracket: return "[";
    case PythonStatementTokenKind::right_bracket: return "]";
    case PythonStatementTokenKind::left_brace: return "{";
    case PythonStatementTokenKind::right_brace: return "}";
    case PythonStatementTokenKind::comma: return ",";
    case PythonStatementTokenKind::colon: return ":";
    case PythonStatementTokenKind::equal: return "=";
    case PythonStatementTokenKind::slash: return "/";
    case PythonStatementTokenKind::star: return "*";
    case PythonStatementTokenKind::arrow: return "->";
    case PythonStatementTokenKind::other: return "token";
  }
  return "token";
}

}  // namespace mpf::detail
