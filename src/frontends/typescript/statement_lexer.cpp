#include "frontends/typescript/statement_lexer.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <string>
#include <string_view>
#include <utility>

namespace mpf::detail {
namespace {

using Kind = TypeScriptStatementTokenKind;

bool identifier_start(const unsigned char character) noexcept {
  return std::isalpha(character) != 0 || character == '_' || character == '$';
}

bool identifier_continue(const unsigned char character) noexcept {
  return std::isalnum(character) != 0 || character == '_' || character == '$';
}

Kind keyword_kind(const std::string_view word) {
  if (word == "function") return Kind::keyword_function;
  if (word == "let") return Kind::keyword_let;
  if (word == "const") return Kind::keyword_const;
  if (word == "export") return Kind::keyword_export;
  if (word == "if") return Kind::keyword_if;
  if (word == "else") return Kind::keyword_else;
  if (word == "while") return Kind::keyword_while;
  if (word == "for") return Kind::keyword_for;
  if (word == "return") return Kind::keyword_return;
  if (word == "break") return Kind::keyword_break;
  if (word == "continue") return Kind::keyword_continue;
  constexpr std::array<std::string_view, 42> unsupported{
      "abstract",   "as",     "asserts", "async",      "await",     "class",    "declare",
      "default",    "delete", "do",      "enum",       "extends",   "from",     "get",
      "implements", "import", "in",      "instanceof", "interface", "keyof",    "namespace",
      "new",        "of",     "private", "protected",  "public",    "readonly", "satisfies",
      "set",        "static", "super",   "switch",     "this",      "throw",    "try",
      "type",       "typeof", "using",   "var",        "void",      "with",     "yield",
  };
  return std::binary_search(unsupported.begin(), unsupported.end(), word)
             ? Kind::unsupported_keyword
             : Kind::identifier;
}

void append(TypeScriptStatementLexResult& result, const SourceText& source, const Kind kind,
            const std::size_t begin, const std::size_t end) {
  result.tokens.push_back({kind, std::string(source.content().substr(begin, end - begin)), begin,
                           end, source.location(begin)});
}

void diagnose(TypeScriptStatementLexResult& result, const SourceText& source,
              const std::size_t offset, std::string code, std::string message) {
  result.diagnostics.push_back(
      {DiagnosticSeverity::error, std::move(code), std::move(message), source.location(offset)});
}

bool other_operator(const std::string_view value) noexcept {
  return value == "==" || value == "!=" || value == "<=" || value == ">=" || value == "&&" ||
         value == "||" || value == "**" || value == "++" || value == "--" || value == "+=" ||
         value == "-=" || value == "*=" || value == "/=" || value == "%=" || value == "??" ||
         value == "?." || value == "<<" || value == ">>";
}

}  // namespace

TypeScriptStatementLexResult lex_typescript_statements(const SourceText& source) {
  TypeScriptStatementLexResult result;
  const auto input = source.content();
  result.tokens.reserve(input.size() / 3U + 1U);
  std::size_t index = 0;
  while (index < input.size()) {
    const auto character = static_cast<unsigned char>(input[index]);
    if (input[index] == '\r' || input[index] == '\n') {
      const auto begin = index++;
      if (input[begin] == '\r' && index < input.size() && input[index] == '\n') ++index;
      append(result, source, Kind::newline, begin, index);
      continue;
    }
    if (std::isspace(character) != 0) {
      ++index;
      continue;
    }
    if (input[index] == '/' && index + 1U < input.size() && input[index + 1U] == '/') {
      index += 2U;
      while (index < input.size() && input[index] != '\r' && input[index] != '\n') ++index;
      continue;
    }
    if (input[index] == '/' && index + 1U < input.size() && input[index + 1U] == '*') {
      const auto begin = index;
      index += 2U;
      bool closed = false;
      while (index < input.size()) {
        if (input[index] == '*' && index + 1U < input.size() && input[index + 1U] == '/') {
          index += 2U;
          closed = true;
          break;
        }
        if (input[index] == '\r' || input[index] == '\n') {
          const auto newline = index++;
          if (input[newline] == '\r' && index < input.size() && input[index] == '\n') ++index;
          append(result, source, Kind::newline, newline, index);
        } else {
          ++index;
        }
      }
      if (!closed) {
        diagnose(result, source, begin, "MPF1901", "unterminated TypeScript block comment");
      }
      continue;
    }

    const auto begin = index;
    if (identifier_start(character)) {
      ++index;
      while (index < input.size() &&
             identifier_continue(static_cast<unsigned char>(input[index]))) {
        ++index;
      }
      append(result, source, keyword_kind(input.substr(begin, index - begin)), begin, index);
      continue;
    }
    if (std::isdigit(character) != 0 ||
        (input[index] == '.' && index + 1U < input.size() &&
         std::isdigit(static_cast<unsigned char>(input[index + 1U])) != 0)) {
      ++index;
      while (index < input.size()) {
        const auto next = static_cast<unsigned char>(input[index]);
        if (std::isalnum(next) == 0 && input[index] != '_' && input[index] != '.') break;
        ++index;
      }
      append(result, source, Kind::number, begin, index);
      continue;
    }
    if (input[index] == '\'' || input[index] == '"') {
      const auto quote = input[index++];
      bool closed = false;
      while (index < input.size()) {
        if (input[index] == '\\' && index + 1U < input.size()) {
          index += 2U;
        } else if (input[index] == quote) {
          ++index;
          closed = true;
          break;
        } else if (input[index] == '\r' || input[index] == '\n') {
          break;
        } else {
          ++index;
        }
      }
      if (!closed) {
        diagnose(result, source, begin, "MPF1902", "unterminated TypeScript string literal");
      }
      append(result, source, Kind::string_literal, begin, index);
      continue;
    }
    if (input[index] == '`') {
      diagnose(result, source, begin, "MPF1200",
               "TypeScript template literals are not supported by the current grammar slice");
      ++index;
      while (index < input.size() && input[index] != '`') {
        if (input[index] == '\\' && index + 1U < input.size())
          index += 2U;
        else
          ++index;
      }
      if (index < input.size()) ++index;
      append(result, source, Kind::other, begin, index);
      continue;
    }

    const auto three = index + 2U < input.size() ? input.substr(index, 3U) : std::string_view{};
    const bool nullish_assign =
        three.size() == 3U && three[0] == '?' && three[1] == '?' && three[2] == '=';
    if (three == "===" || three == "!==" || three == "**=" || nullish_assign) {
      index += 3U;
      append(result, source,
             three == "==="   ? Kind::strict_equal
             : three == "!==" ? Kind::strict_not_equal
                              : Kind::other,
             begin, index);
      continue;
    }
    const auto two = index + 1U < input.size() ? input.substr(index, 2U) : std::string_view{};
    if (two == "=>") {
      index += 2U;
      append(result, source, Kind::arrow, begin, index);
      continue;
    }
    if (other_operator(two)) {
      index += 2U;
      append(result, source, Kind::other, begin, index);
      continue;
    }

    Kind kind = Kind::other;
    switch (input[index]) {
      case '(': kind = Kind::left_parenthesis; break;
      case ')': kind = Kind::right_parenthesis; break;
      case '[': kind = Kind::left_bracket; break;
      case ']': kind = Kind::right_bracket; break;
      case '{': kind = Kind::left_brace; break;
      case '}': kind = Kind::right_brace; break;
      case ',': kind = Kind::comma; break;
      case ':': kind = Kind::colon; break;
      case ';': kind = Kind::semicolon; break;
      case '=': kind = Kind::equal; break;
      case '?': kind = Kind::question; break;
      case '.': kind = Kind::dot; break;
      default: break;
    }
    if (std::iscntrl(character) != 0) {
      diagnose(result, source, begin, "MPF1903", "invalid control character in TypeScript source");
    }
    ++index;
    append(result, source, kind, begin, index);
  }
  result.tokens.push_back(
      {Kind::end, {}, input.size(), input.size(), source.location(input.size())});
  return result;
}

const char* to_string(const TypeScriptStatementTokenKind kind) noexcept {
  switch (kind) {
    case Kind::end: return "end of source";
    case Kind::newline: return "newline";
    case Kind::identifier: return "identifier";
    case Kind::number: return "number";
    case Kind::string_literal: return "string";
    case Kind::keyword_function: return "function";
    case Kind::keyword_let: return "let";
    case Kind::keyword_const: return "const";
    case Kind::keyword_export: return "export";
    case Kind::keyword_if: return "if";
    case Kind::keyword_else: return "else";
    case Kind::keyword_while: return "while";
    case Kind::keyword_for: return "for";
    case Kind::keyword_return: return "return";
    case Kind::keyword_break: return "break";
    case Kind::keyword_continue: return "continue";
    case Kind::unsupported_keyword: return "unsupported keyword";
    case Kind::left_parenthesis: return "(";
    case Kind::right_parenthesis: return ")";
    case Kind::left_bracket: return "[";
    case Kind::right_bracket: return "]";
    case Kind::left_brace: return "{";
    case Kind::right_brace: return "}";
    case Kind::comma: return ",";
    case Kind::colon: return ":";
    case Kind::semicolon: return ";";
    case Kind::equal: return "=";
    case Kind::question: return "?";
    case Kind::dot: return ".";
    case Kind::strict_equal: return "===";
    case Kind::strict_not_equal: return "!==";
    case Kind::arrow: return "=>";
    case Kind::other: return "token";
  }
  return "token";
}

}  // namespace mpf::detail
