#include "frontends/fortran/statement_lexer.hpp"

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

FortranStatementTokenKind keyword_kind(const std::string& word) {
  static const std::unordered_map<std::string, FortranStatementTokenKind> keywords{
      {"program", FortranStatementTokenKind::keyword_program},
      {"end", FortranStatementTokenKind::keyword_end},
      {"endif", FortranStatementTokenKind::keyword_endif},
      {"enddo", FortranStatementTokenKind::keyword_enddo},
      {"implicit", FortranStatementTokenKind::keyword_implicit},
      {"none", FortranStatementTokenKind::keyword_none},
      {"contains", FortranStatementTokenKind::keyword_contains},
      {"recursive", FortranStatementTokenKind::keyword_recursive},
      {"function", FortranStatementTokenKind::keyword_function},
      {"subroutine", FortranStatementTokenKind::keyword_subroutine},
      {"result", FortranStatementTokenKind::keyword_result},
      {"return", FortranStatementTokenKind::keyword_return},
      {"if", FortranStatementTokenKind::keyword_if},
      {"then", FortranStatementTokenKind::keyword_then},
      {"else", FortranStatementTokenKind::keyword_else},
      {"elseif", FortranStatementTokenKind::keyword_elseif},
      {"select", FortranStatementTokenKind::keyword_select},
      {"case", FortranStatementTokenKind::keyword_case},
      {"default", FortranStatementTokenKind::keyword_default},
      {"endselect", FortranStatementTokenKind::keyword_endselect},
      {"do", FortranStatementTokenKind::keyword_do},
      {"while", FortranStatementTokenKind::keyword_while},
      {"exit", FortranStatementTokenKind::keyword_exit},
      {"cycle", FortranStatementTokenKind::keyword_cycle},
      {"print", FortranStatementTokenKind::keyword_print},
      {"write", FortranStatementTokenKind::keyword_write},
      {"call", FortranStatementTokenKind::keyword_call},
      {"integer", FortranStatementTokenKind::keyword_integer},
      {"real", FortranStatementTokenKind::keyword_real},
      {"double", FortranStatementTokenKind::keyword_double},
      {"precision", FortranStatementTokenKind::keyword_precision},
      {"complex", FortranStatementTokenKind::keyword_complex},
      {"logical", FortranStatementTokenKind::keyword_logical},
      {"character", FortranStatementTokenKind::keyword_character}};
  static const std::unordered_map<std::string, bool> unsupported{
      {"module", true},   {"submodule", true},   {"interface", true}, {"procedure", true},
      {"where", true},    {"elsewhere", true},   {"forall", true},    {"associate", true},
      {"type", true},     {"class", true},       {"use", true},       {"include", true},
      {"common", true},   {"equivalence", true}, {"save", true},      {"data", true},
      {"allocate", true}, {"deallocate", true},  {"stop", true},      {"error", true},
      {"sync", true}};
  const auto key = lower(word);
  const auto found = keywords.find(key);
  if (found != keywords.end()) return found->second;
  return unsupported.find(key) != unsupported.end() ? FortranStatementTokenKind::unsupported_keyword
                                                    : FortranStatementTokenKind::identifier;
}

void append_token(FortranStatementLine& output, const FortranStatementTokenKind kind,
                  const std::string_view text, const std::size_t begin, const std::size_t end) {
  output.tokens.push_back({kind,
                           kind == FortranStatementTokenKind::string_literal
                               ? std::string(text.substr(begin, end - begin))
                               : lower(std::string(text.substr(begin, end - begin))),
                           begin,
                           end,
                           {output.source.number, begin + 1}});
}

bool two_character_operator(const std::string_view value) noexcept {
  return value == "==" || value == "/=" || value == "<=" || value == ">=" || value == "**" ||
         value == "//" || value == "=>" || value == "(/" || value == "/)";
}

FortranStatementLine lex_line(SourceLine line, std::vector<Diagnostic>& diagnostics) {
  FortranStatementLine output;
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
        if (std::isalnum(next) == 0 && text[index] != '_' && text[index] != '.' &&
            text[index] != '+' && text[index] != '-') {
          break;
        }
        if ((text[index] == '+' || text[index] == '-') && index > begin && text[index - 1] != 'e' &&
            text[index - 1] != 'E' && text[index - 1] != 'd' && text[index - 1] != 'D') {
          break;
        }
        ++index;
      }
      append_token(output, FortranStatementTokenKind::number, text, begin, index);
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
        diagnostics.emplace_back(DiagnosticSeverity::error, "MPF1801",
                                 "unterminated string in Fortran statement token stream",
                                 SourceLocation{output.source.number, begin + 1});
      }
      append_token(output, FortranStatementTokenKind::string_literal, text, begin, index);
      continue;
    }
    if (text[index] == '.') {
      const auto closing = text.find('.', index + 1);
      if (closing != std::string::npos) {
        index = closing + 1;
        append_token(output, FortranStatementTokenKind::other, text, begin, index);
        continue;
      }
    }
    const auto two = index + 1 < text.size() ? view.substr(index, 2) : std::string_view{};
    if (two == "(/" || two == "/)") {
      index += 2;
      append_token(output,
                   two == "(/" ? FortranStatementTokenKind::left_bracket
                               : FortranStatementTokenKind::right_bracket,
                   text, begin, index);
      continue;
    }
    if (two == "::") {
      index += 2;
      append_token(output, FortranStatementTokenKind::double_colon, text, begin, index);
      continue;
    }
    if (two_character_operator(two)) {
      index += 2;
      append_token(output, FortranStatementTokenKind::other, text, begin, index);
      continue;
    }

    FortranStatementTokenKind kind = FortranStatementTokenKind::other;
    switch (text[index]) {
      case '(': kind = FortranStatementTokenKind::left_parenthesis; break;
      case ')': kind = FortranStatementTokenKind::right_parenthesis; break;
      case '[': kind = FortranStatementTokenKind::left_bracket; break;
      case ']': kind = FortranStatementTokenKind::right_bracket; break;
      case ',': kind = FortranStatementTokenKind::comma; break;
      case ':': kind = FortranStatementTokenKind::colon; break;
      case '=': kind = FortranStatementTokenKind::equal; break;
      case '*': kind = FortranStatementTokenKind::star; break;
      default: break;
    }
    if (std::iscntrl(character) != 0) {
      diagnostics.emplace_back(DiagnosticSeverity::error, "MPF1802",
                               "invalid control character in Fortran statement",
                               SourceLocation{output.source.number, index + 1});
    }
    ++index;
    append_token(output, kind, text, begin, index);
  }
  output.tokens.push_back({FortranStatementTokenKind::end,
                           {},
                           text.size(),
                           text.size(),
                           {output.source.number, text.size() + 1}});
  return output;
}

}  // namespace

FortranStatementLexResult lex_fortran_statements(std::vector<SourceLine> lines) {
  FortranStatementLexResult result;
  result.lines.reserve(lines.size());
  for (auto& line : lines) {
    result.lines.push_back(lex_line(std::move(line), result.diagnostics));
  }
  return result;
}

const char* to_string(const FortranStatementTokenKind kind) noexcept {
  switch (kind) {
    case FortranStatementTokenKind::end: return "end of statement";
    case FortranStatementTokenKind::identifier: return "identifier";
    case FortranStatementTokenKind::number: return "number";
    case FortranStatementTokenKind::string_literal: return "string";
    case FortranStatementTokenKind::keyword_program: return "program";
    case FortranStatementTokenKind::keyword_end: return "end";
    case FortranStatementTokenKind::keyword_endif: return "endif";
    case FortranStatementTokenKind::keyword_enddo: return "enddo";
    case FortranStatementTokenKind::keyword_implicit: return "implicit";
    case FortranStatementTokenKind::keyword_none: return "none";
    case FortranStatementTokenKind::keyword_contains: return "contains";
    case FortranStatementTokenKind::keyword_recursive: return "recursive";
    case FortranStatementTokenKind::keyword_function: return "function";
    case FortranStatementTokenKind::keyword_subroutine: return "subroutine";
    case FortranStatementTokenKind::keyword_result: return "result";
    case FortranStatementTokenKind::keyword_return: return "return";
    case FortranStatementTokenKind::keyword_if: return "if";
    case FortranStatementTokenKind::keyword_then: return "then";
    case FortranStatementTokenKind::keyword_else: return "else";
    case FortranStatementTokenKind::keyword_elseif: return "elseif";
    case FortranStatementTokenKind::keyword_select: return "select";
    case FortranStatementTokenKind::keyword_case: return "case";
    case FortranStatementTokenKind::keyword_default: return "default";
    case FortranStatementTokenKind::keyword_endselect: return "endselect";
    case FortranStatementTokenKind::keyword_do: return "do";
    case FortranStatementTokenKind::keyword_while: return "while";
    case FortranStatementTokenKind::keyword_exit: return "exit";
    case FortranStatementTokenKind::keyword_cycle: return "cycle";
    case FortranStatementTokenKind::keyword_print: return "print";
    case FortranStatementTokenKind::keyword_write: return "write";
    case FortranStatementTokenKind::keyword_call: return "call";
    case FortranStatementTokenKind::keyword_integer: return "integer";
    case FortranStatementTokenKind::keyword_real: return "real";
    case FortranStatementTokenKind::keyword_double: return "double";
    case FortranStatementTokenKind::keyword_precision: return "precision";
    case FortranStatementTokenKind::keyword_complex: return "complex";
    case FortranStatementTokenKind::keyword_logical: return "logical";
    case FortranStatementTokenKind::keyword_character: return "character";
    case FortranStatementTokenKind::unsupported_keyword: return "unsupported keyword";
    case FortranStatementTokenKind::left_parenthesis: return "(";
    case FortranStatementTokenKind::right_parenthesis: return ")";
    case FortranStatementTokenKind::left_bracket: return "[";
    case FortranStatementTokenKind::right_bracket: return "]";
    case FortranStatementTokenKind::comma: return ",";
    case FortranStatementTokenKind::colon: return ":";
    case FortranStatementTokenKind::double_colon: return "::";
    case FortranStatementTokenKind::equal: return "=";
    case FortranStatementTokenKind::star: return "*";
    case FortranStatementTokenKind::other: return "token";
  }
  return "token";
}

}  // namespace mpf::detail
