#include "scanner.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace mpf::detail {
namespace {

std::string lower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char character) {
    return static_cast<char>(std::tolower(character));
  });
  return value;
}

std::size_t column_at(const std::string_view input, const std::size_t byte_offset,
                      const std::size_t base_column) noexcept {
  auto result = base_column;
  for (std::size_t index = 0; index < byte_offset; ++index) {
    const auto byte = static_cast<unsigned char>(input[index]);
    if ((byte & 0xC0U) != 0x80U) {
      ++result;
    }
  }
  return result;
}

bool word_start(const unsigned char character) noexcept {
  return std::isalpha(character) != 0 || character == '_';
}

bool word_continue(const unsigned char character) noexcept {
  return std::isalnum(character) != 0 || character == '_';
}

void add_error(LexerResult& result, const std::size_t line, const std::size_t column,
               std::string code, std::string message) {
  result.diagnostics.push_back(
      {DiagnosticSeverity::error, std::move(code), std::move(message), {line, column}});
}

std::string portable_string_literal(const std::string_view content, const bool preserve_escapes) {
  std::string result{"\""};
  for (std::size_t index = 0; index < content.size(); ++index) {
    const char character = content[index];
    if (character == '\\' && preserve_escapes && index + 1 < content.size()) {
      result.push_back(character);
      result.push_back(content[++index]);
    } else if (character == '\\' || character == '"') {
      result.push_back('\\');
      result.push_back(character);
    } else if (character == '\n') {
      result += "\\n";
    } else if (character == '\r') {
      result += "\\r";
    } else {
      result.push_back(character);
    }
  }
  result.push_back('"');
  return result;
}

TokenKind identifier_kind(const std::string& word, const SourceLanguage language) {
  const auto key = lower(word);
  if ((language == SourceLanguage::python && key == "true") ||
      (language == SourceLanguage::matlab && key == "true")) {
    return TokenKind::true_keyword;
  }
  if ((language == SourceLanguage::python && key == "false") ||
      (language == SourceLanguage::matlab && key == "false")) {
    return TokenKind::false_keyword;
  }
  if (language == SourceLanguage::python && key == "none") {
    return TokenKind::null_keyword;
  }
  if (language == SourceLanguage::python && key == "and") {
    return TokenKind::logical_and;
  }
  if (language == SourceLanguage::python && key == "or") {
    return TokenKind::logical_or;
  }
  if (language == SourceLanguage::python && key == "not") {
    return TokenKind::logical_not;
  }
  if (language == SourceLanguage::python && key == "if") {
    return TokenKind::conditional_if;
  }
  if (language == SourceLanguage::python && key == "else") {
    return TokenKind::conditional_else;
  }
  return TokenKind::identifier;
}

bool token_ends_matlab_vector_element(const TokenKind kind) noexcept {
  return kind == TokenKind::identifier || kind == TokenKind::number ||
         kind == TokenKind::string_literal || kind == TokenKind::true_keyword ||
         kind == TokenKind::false_keyword || kind == TokenKind::right_parenthesis ||
         kind == TokenKind::right_bracket;
}

bool starts_matlab_vector_element(const unsigned char character) noexcept {
  return word_start(character) || std::isdigit(character) != 0 || character == '\'' ||
         character == '"' || character == '[' || character == '(' || character == '+' ||
         character == '-' || character == '.';
}

}  // namespace

LexerResult scan_expression(const std::string_view input, const SourceLanguage language,
                            const std::size_t line, const std::size_t base_column) {
  LexerResult result;
  std::size_t index = 0;
  int bracket_depth = 0;
  while (index < input.size()) {
    const auto character = static_cast<unsigned char>(input[index]);
    if (std::isspace(character) != 0) {
      const auto whitespace_begin = index;
      while (index < input.size() && std::isspace(static_cast<unsigned char>(input[index])) != 0) {
        ++index;
      }
      if (language == SourceLanguage::matlab && bracket_depth > 0 && !result.tokens.empty() &&
          index < input.size() && token_ends_matlab_vector_element(result.tokens.back().kind) &&
          starts_matlab_vector_element(static_cast<unsigned char>(input[index]))) {
        result.tokens.push_back(
            {TokenKind::comma, ",", {line, column_at(input, whitespace_begin, base_column)}});
      }
      continue;
    }
    const auto token_column = column_at(input, index, base_column);

    if (language == SourceLanguage::fortran && character == '.') {
      const auto closing = input.find('.', index + 1);
      if (closing != std::string_view::npos) {
        const auto dotted = lower(std::string(input.substr(index, closing - index + 1)));
        static const std::unordered_map<std::string, TokenKind> dotted_tokens{
            {".true.", TokenKind::true_keyword}, {".false.", TokenKind::false_keyword},
            {".and.", TokenKind::logical_and},   {".or.", TokenKind::logical_or},
            {".not.", TokenKind::logical_not},   {".eq.", TokenKind::equal_equal},
            {".ne.", TokenKind::not_equal},      {".lt.", TokenKind::less},
            {".le.", TokenKind::less_equal},     {".gt.", TokenKind::greater},
            {".ge.", TokenKind::greater_equal}};
        const auto found = dotted_tokens.find(dotted);
        if (found != dotted_tokens.end()) {
          result.tokens.push_back({found->second, dotted, {line, token_column}});
          index = closing + 1;
          continue;
        }
      }
    }

    if (word_start(character)) {
      const auto begin = index++;
      while (index < input.size() && word_continue(static_cast<unsigned char>(input[index]))) {
        ++index;
      }
      auto word = std::string(input.substr(begin, index - begin));
      const auto kind = identifier_kind(word, language);
      if (language == SourceLanguage::fortran) {
        word = lower(std::move(word));
      }
      result.tokens.push_back({kind, std::move(word), {line, token_column}});
      continue;
    }

    if (std::isdigit(character) != 0 ||
        (character == '.' && index + 1 < input.size() &&
         std::isdigit(static_cast<unsigned char>(input[index + 1])) != 0)) {
      const auto begin = index;
      bool has_decimal = false;
      if (input[index] == '.') {
        has_decimal = true;
        ++index;
      }
      while (index < input.size() && std::isdigit(static_cast<unsigned char>(input[index])) != 0) {
        ++index;
      }
      if (!has_decimal && index < input.size() && input[index] == '.') {
        ++index;
        while (index < input.size() &&
               std::isdigit(static_cast<unsigned char>(input[index])) != 0) {
          ++index;
        }
      }
      if (index < input.size() &&
          (input[index] == 'e' || input[index] == 'E' ||
           (language == SourceLanguage::fortran && (input[index] == 'd' || input[index] == 'D')))) {
        ++index;
        if (index < input.size() && (input[index] == '+' || input[index] == '-')) {
          ++index;
        }
        const auto exponent_begin = index;
        while (index < input.size() &&
               std::isdigit(static_cast<unsigned char>(input[index])) != 0) {
          ++index;
        }
        if (exponent_begin == index) {
          add_error(result, line, token_column, "MPF1005", "numeric exponent requires digits");
        }
      }
      if (language == SourceLanguage::fortran && index < input.size() && input[index] == '_') {
        ++index;
        while (index < input.size() && word_continue(static_cast<unsigned char>(input[index]))) {
          ++index;
        }
      }
      auto number = std::string(input.substr(begin, index - begin));
      if (language == SourceLanguage::fortran) {
        const auto kind_separator = number.find('_');
        if (kind_separator != std::string::npos) {
          number.erase(kind_separator);
        }
        std::replace(number.begin(), number.end(), 'd', 'e');
        std::replace(number.begin(), number.end(), 'D', 'e');
      }
      result.tokens.push_back({TokenKind::number, std::move(number), {line, token_column}});
      continue;
    }

    if (character == '\'' || character == '"') {
      const char quote = static_cast<char>(character);
      ++index;
      std::string content;
      bool closed = false;
      while (index < input.size()) {
        if (input[index] == quote) {
          if (language != SourceLanguage::python && index + 1 < input.size() &&
              input[index + 1] == quote) {
            content.push_back(quote);
            index += 2;
            continue;
          }
          ++index;
          closed = true;
          break;
        }
        if (input[index] == '\\' && language == SourceLanguage::python &&
            index + 1 < input.size()) {
          content.push_back(input[index++]);
          content.push_back(input[index++]);
          continue;
        }
        content.push_back(input[index++]);
      }
      if (!closed) {
        add_error(result, line, token_column, "MPF1002", "unterminated string literal");
      }
      result.tokens.push_back({TokenKind::string_literal,
                               portable_string_literal(content, language == SourceLanguage::python),
                               {line, token_column}});
      continue;
    }

    const auto two = index + 1 < input.size() ? input.substr(index, 2) : std::string_view{};
    TokenKind kind = TokenKind::end;
    if (language == SourceLanguage::fortran && two == "(/")
      kind = TokenKind::left_bracket;
    else if (language == SourceLanguage::fortran && two == "/)")
      kind = TokenKind::right_bracket;
    else if (two == "**")
      kind = TokenKind::power;
    else if (two == "//" && language == SourceLanguage::python)
      kind = TokenKind::floor_slash;
    else if (two == "==")
      kind = TokenKind::equal_equal;
    else if (two == "!=" || two == "~=" || two == "/=")
      kind = TokenKind::not_equal;
    else if (two == "<=")
      kind = TokenKind::less_equal;
    else if (two == ">=")
      kind = TokenKind::greater_equal;
    else if (two == "&&")
      kind = TokenKind::logical_and;
    else if (two == "||")
      kind = TokenKind::logical_or;
    if (kind != TokenKind::end) {
      result.tokens.push_back({kind, std::string(two), {line, token_column}});
      index += 2;
      continue;
    }

    switch (static_cast<char>(character)) {
      case '+': kind = TokenKind::plus; break;
      case '-': kind = TokenKind::minus; break;
      case '*': kind = TokenKind::star; break;
      case '/': kind = TokenKind::slash; break;
      case '%': kind = TokenKind::percent; break;
      case '^':
        kind = language == SourceLanguage::matlab ? TokenKind::power : TokenKind::end;
        break;
      case '<': kind = TokenKind::less; break;
      case '>': kind = TokenKind::greater; break;
      case '!': kind = TokenKind::logical_not; break;
      case '=':
        kind = language == SourceLanguage::fortran || language == SourceLanguage::python
                   ? TokenKind::equal
                   : TokenKind::end;
        break;
      case '~':
        kind = language == SourceLanguage::matlab ? TokenKind::logical_not : TokenKind::end;
        break;
      case '&':
        kind = language == SourceLanguage::matlab ? TokenKind::logical_and : TokenKind::end;
        break;
      case '|':
        kind = language == SourceLanguage::matlab ? TokenKind::logical_or : TokenKind::end;
        break;
      case '(': kind = TokenKind::left_parenthesis; break;
      case ')': kind = TokenKind::right_parenthesis; break;
      case '[': kind = TokenKind::left_bracket; break;
      case ']': kind = TokenKind::right_bracket; break;
      case ',': kind = TokenKind::comma; break;
      case ':': kind = TokenKind::colon; break;
      case ';':
        kind = language == SourceLanguage::matlab ? TokenKind::semicolon : TokenKind::end;
        break;
      case '.': kind = TokenKind::dot; break;
      default: break;
    }
    if (kind == TokenKind::end) {
      add_error(result, line, token_column, "MPF1001",
                std::string("unexpected character in expression: '") +
                    static_cast<char>(character) + "'");
    } else {
      result.tokens.push_back(
          {kind, std::string(1, static_cast<char>(character)), {line, token_column}});
      if (kind == TokenKind::left_bracket)
        ++bracket_depth;
      else if (kind == TokenKind::right_bracket)
        --bracket_depth;
    }
    ++index;
  }
  result.tokens.push_back(
      {TokenKind::end, {}, {line, column_at(input, input.size(), base_column)}});
  return result;
}

}  // namespace mpf::detail
