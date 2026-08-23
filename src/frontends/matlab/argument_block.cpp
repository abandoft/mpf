#include "frontends/matlab/argument_block.hpp"

#include <charconv>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <utility>

#include "frontends/common/parser_support.hpp"

namespace mpf::detail {
namespace {

using Kind = MatlabStatementTokenKind;

std::size_t token_count(const MatlabStatementLine& line) noexcept {
  return line.tokens.empty() ? 0U : line.tokens.size() - 1U;
}

std::string token_slice(const MatlabStatementLine& line, const std::size_t first,
                        const std::size_t last) {
  if (first >= last || last > token_count(line)) return {};
  const auto begin = line.tokens[first].begin;
  const auto end = line.tokens[last - 1U].end;
  return frontend::trim(std::string_view(line.source.text).substr(begin, end - begin));
}

bool is_opening(const Kind kind) noexcept {
  return kind == Kind::left_parenthesis || kind == Kind::left_bracket || kind == Kind::left_brace;
}

bool is_closing(const Kind kind) noexcept {
  return kind == Kind::right_parenthesis || kind == Kind::right_bracket ||
         kind == Kind::right_brace;
}

bool matches(const Kind opening, const Kind closing) noexcept {
  return (opening == Kind::left_parenthesis && closing == Kind::right_parenthesis) ||
         (opening == Kind::left_bracket && closing == Kind::right_bracket) ||
         (opening == Kind::left_brace && closing == Kind::right_brace);
}

std::size_t matching_token(const MatlabStatementLine& line, const std::size_t opening) noexcept {
  std::vector<Kind> stack;
  for (std::size_t index = opening; index < token_count(line); ++index) {
    const auto kind = line.tokens[index].kind;
    if (is_opening(kind)) {
      stack.push_back(kind);
    } else if (is_closing(kind)) {
      if (stack.empty() || !matches(stack.back(), kind)) return token_count(line);
      stack.pop_back();
      if (stack.empty()) return index;
    }
  }
  return token_count(line);
}

void diagnose(std::vector<Diagnostic>& diagnostics, const std::size_t line, std::string message) {
  frontend::unsupported(diagnostics, line, std::move(message));
}

void diagnose_version(std::vector<Diagnostic>& diagnostics, const std::size_t line,
                      std::string message) {
  frontend::version_unsupported(diagnostics, line, std::move(message));
}

std::optional<std::size_t> nonnegative_integer(const std::string_view text) {
  std::size_t value = 0U;
  const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
  if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) return std::nullopt;
  return value;
}

std::optional<ArgumentClassConstraint> argument_class(const std::string_view name) noexcept {
  if (name == "double") return ArgumentClassConstraint::matlab_double;
  if (name == "logical") return ArgumentClassConstraint::matlab_logical;
  if (name == "char") return ArgumentClassConstraint::matlab_char;
  return std::nullopt;
}

struct ValidatorDefinition {
  ArgumentValidator validator;
  LanguageVersion minimum_version;
  std::string_view minimum_release;
};

std::optional<ValidatorDefinition> argument_validator(const std::string_view name) {
  constexpr auto arguments_release = LanguageVersion{2019, 2};
  constexpr auto validator_expansion_release = LanguageVersion{2020, 2};
  constexpr auto shape_validator_release = LanguageVersion{2024, 2};
  static const std::unordered_map<std::string_view, ValidatorDefinition> validators{
      {"mustBeNumeric", {ArgumentValidator::numeric, arguments_release, "R2019b"}},
      {"mustBeNumericOrLogical",
       {ArgumentValidator::numeric_or_logical, arguments_release, "R2019b"}},
      {"mustBeFloat", {ArgumentValidator::floating, validator_expansion_release, "R2020b"}},
      {"mustBeReal", {ArgumentValidator::real, arguments_release, "R2019b"}},
      {"mustBeFinite", {ArgumentValidator::finite, arguments_release, "R2019b"}},
      {"mustBeNonNan", {ArgumentValidator::non_nan, arguments_release, "R2019b"}},
      {"mustBePositive", {ArgumentValidator::positive, arguments_release, "R2019b"}},
      {"mustBeNonpositive", {ArgumentValidator::nonpositive, arguments_release, "R2019b"}},
      {"mustBeNonnegative", {ArgumentValidator::nonnegative, arguments_release, "R2019b"}},
      {"mustBeNegative", {ArgumentValidator::negative, arguments_release, "R2019b"}},
      {"mustBeNonzero", {ArgumentValidator::nonzero, arguments_release, "R2019b"}},
      {"mustBeInteger", {ArgumentValidator::integer, arguments_release, "R2019b"}},
      {"mustBeNonempty", {ArgumentValidator::nonempty, arguments_release, "R2019b"}},
      {"mustBeScalarOrEmpty",
       {ArgumentValidator::scalar_or_empty, validator_expansion_release, "R2020b"}},
      {"mustBeVector", {ArgumentValidator::vector, validator_expansion_release, "R2020b"}},
      {"mustBeRow", {ArgumentValidator::row, shape_validator_release, "R2024b"}},
      {"mustBeColumn", {ArgumentValidator::column, shape_validator_release, "R2024b"}},
      {"mustBeMatrix", {ArgumentValidator::matrix, shape_validator_release, "R2024b"}},
      {"mustBeNonmissing", {ArgumentValidator::nonmissing, validator_expansion_release, "R2020b"}},
      {"mustBeNonzeroLengthText",
       {ArgumentValidator::nonzero_length_text, validator_expansion_release, "R2020b"}},
      {"mustBeText", {ArgumentValidator::text, validator_expansion_release, "R2020b"}},
      {"mustBeTextScalar", {ArgumentValidator::text_scalar, validator_expansion_release, "R2020b"}},
      {"mustBeValidVariableName",
       {ArgumentValidator::valid_variable_name, validator_expansion_release, "R2020b"}}};
  const auto found = validators.find(name);
  return found == validators.end() ? std::nullopt
                                   : std::optional<ValidatorDefinition>{found->second};
}

bool parse_dimensions(const MatlabStatementLine& line, std::size_t& cursor,
                      MatlabArgumentDeclaration& declaration,
                      std::vector<Diagnostic>& diagnostics) {
  if (cursor >= token_count(line) || line.tokens[cursor].kind != Kind::left_parenthesis) {
    return true;
  }
  const auto closing = matching_token(line, cursor);
  if (closing == token_count(line)) {
    diagnose(diagnostics, line.source.number,
             "Matlab arguments dimension list has no matching right parenthesis");
    return false;
  }
  declaration.syntax.dimensions_declared = true;
  bool expect_dimension = true;
  for (std::size_t token = cursor + 1U; token < closing; ++token) {
    if (expect_dimension) {
      if (line.tokens[token].kind == Kind::colon) {
        declaration.syntax.dimensions.push_back({true, 0U});
      } else if (line.tokens[token].kind == Kind::number) {
        const auto extent = nonnegative_integer(line.tokens[token].text);
        if (!extent.has_value()) {
          diagnose(diagnostics, line.source.number,
                   "Matlab arguments dimensions require nonnegative integer literals or ':'");
          return false;
        }
        declaration.syntax.dimensions.push_back({false, *extent});
      } else {
        diagnose(diagnostics, line.source.number,
                 "Matlab arguments dimensions cannot contain expressions");
        return false;
      }
    } else if (line.tokens[token].kind != Kind::comma) {
      diagnose(diagnostics, line.source.number,
               "Matlab arguments dimensions require comma-separated extents");
      return false;
    }
    expect_dimension = !expect_dimension;
  }
  if (expect_dimension || declaration.syntax.dimensions.size() < 2U) {
    diagnose(diagnostics, line.source.number,
             "Matlab arguments dimensions require at least two comma-separated extents");
    return false;
  }
  cursor = closing + 1U;
  return true;
}

bool parse_validators(const MatlabStatementLine& line, std::size_t& cursor,
                      MatlabArgumentDeclaration& declaration, std::vector<Diagnostic>& diagnostics,
                      const LanguageVersion version) {
  if (cursor >= token_count(line) || line.tokens[cursor].kind != Kind::left_brace) return true;
  const auto closing = matching_token(line, cursor);
  if (closing == token_count(line)) {
    diagnose(diagnostics, line.source.number,
             "Matlab arguments validator list has no matching right brace");
    return false;
  }
  bool expect_validator = true;
  for (std::size_t token = cursor + 1U; token < closing; ++token) {
    if (expect_validator) {
      if (line.tokens[token].kind != Kind::identifier) {
        diagnose(diagnostics, line.source.number,
                 "Matlab arguments validators must be named validation functions");
        return false;
      }
      if (token + 1U < closing && line.tokens[token + 1U].kind == Kind::left_parenthesis) {
        diagnose(diagnostics, line.source.number,
                 "parameterized Matlab arguments validators are not yet supported");
        return false;
      }
      const auto validator = argument_validator(line.tokens[token].text);
      if (!validator.has_value()) {
        diagnose(diagnostics, line.source.number,
                 "custom Matlab arguments validator '" + line.tokens[token].text +
                     "' is not yet supported");
        return false;
      }
      if (version < validator->minimum_version) {
        diagnose_version(diagnostics, line.source.number,
                         "Matlab validator '" + line.tokens[token].text + "' requires Matlab " +
                             std::string(validator->minimum_release) + " or newer");
        return false;
      }
      declaration.syntax.validators.push_back(validator->validator);
    } else if (line.tokens[token].kind != Kind::comma) {
      diagnose(diagnostics, line.source.number,
               "Matlab arguments validators require a comma-separated function list");
      return false;
    }
    expect_validator = !expect_validator;
  }
  if (expect_validator || declaration.syntax.validators.empty()) {
    diagnose(diagnostics, line.source.number,
             "Matlab arguments validator list cannot be empty or end with a comma");
    return false;
  }
  cursor = closing + 1U;
  return true;
}

std::optional<MatlabArgumentDeclaration> parse_declaration(const MatlabStatementLine& line,
                                                           const ArgumentDirection direction,
                                                           const LanguageVersion version,
                                                           std::vector<Diagnostic>& diagnostics) {
  const auto count = token_count(line);
  if (count == 0U || line.tokens[0].kind != Kind::identifier) {
    diagnose(diagnostics, line.source.number,
             "Matlab arguments block entries must start with a formal argument name");
    return std::nullopt;
  }
  if (count >= 3U && line.tokens[1].kind == Kind::other && line.tokens[1].text == ".") {
    diagnose(diagnostics, line.source.number,
             "Matlab name-value arguments require the struct object model and are not yet "
             "supported");
    return std::nullopt;
  }
  MatlabArgumentDeclaration result;
  result.syntax.name = line.tokens[0].text;
  result.syntax.line = line.source.number;
  result.syntax.direction = direction;
  std::size_t cursor = 1U;
  if (!parse_dimensions(line, cursor, result, diagnostics)) return std::nullopt;
  if (cursor < count && line.tokens[cursor].kind == Kind::identifier) {
    const auto constraint = argument_class(line.tokens[cursor].text);
    if (!constraint.has_value()) {
      diagnose(diagnostics, line.source.number,
               "Matlab arguments class '" + line.tokens[cursor].text +
                   "' is not representable by the current scalar/NDArray ABI");
      return std::nullopt;
    }
    result.syntax.class_constraint = *constraint;
    ++cursor;
  }
  if (!parse_validators(line, cursor, result, diagnostics, version)) return std::nullopt;
  if (cursor < count && line.tokens[cursor].kind == Kind::equal) {
    if (direction == ArgumentDirection::output) {
      diagnose(diagnostics, line.source.number,
               "Matlab output arguments cannot declare default values");
      return std::nullopt;
    }
    if (cursor + 1U >= count) {
      diagnose(diagnostics, line.source.number,
               "Matlab optional argument requires a default value expression");
      return std::nullopt;
    }
    result.syntax.has_default = true;
    result.default_source = token_slice(line, cursor + 1U, count);
    cursor = count;
  }
  if (cursor != count) {
    diagnose(diagnostics, line.source.number,
             "malformed or unsupported Matlab arguments declaration");
    return std::nullopt;
  }
  return result;
}

std::optional<ArgumentDirection> parse_header(const MatlabStatementLine& line, bool& repeating,
                                              std::vector<Diagnostic>& diagnostics) {
  const auto count = token_count(line);
  repeating = false;
  if (count == 1U) return ArgumentDirection::input;
  if (count < 4U || line.tokens[1].kind != Kind::left_parenthesis ||
      line.tokens[count - 1U].kind != Kind::right_parenthesis ||
      matching_token(line, 1U) != count - 1U) {
    diagnose(diagnostics, line.source.number, "malformed Matlab arguments block attributes");
    return std::nullopt;
  }
  std::optional<ArgumentDirection> direction;
  bool expect_attribute = true;
  for (std::size_t token = 2U; token + 1U < count; ++token) {
    if (expect_attribute) {
      if (line.tokens[token].kind != Kind::identifier) {
        diagnose(diagnostics, line.source.number,
                 "Matlab arguments block attributes must be identifiers");
        return std::nullopt;
      }
      const auto attribute = frontend::lower(line.tokens[token].text);
      if (attribute == "input") {
        if (direction.has_value()) {
          diagnose(diagnostics, line.source.number,
                   "Matlab arguments block cannot repeat Input/Output attributes");
          return std::nullopt;
        }
        direction = ArgumentDirection::input;
      } else if (attribute == "output") {
        if (direction.has_value()) {
          diagnose(diagnostics, line.source.number,
                   "Matlab arguments block cannot combine Input and Output attributes");
          return std::nullopt;
        }
        direction = ArgumentDirection::output;
      } else if (attribute == "repeating") {
        repeating = true;
      } else {
        diagnose(diagnostics, line.source.number,
                 "unsupported Matlab arguments block attribute '" + line.tokens[token].text + "'");
        return std::nullopt;
      }
    } else if (line.tokens[token].kind != Kind::comma) {
      diagnose(diagnostics, line.source.number,
               "Matlab arguments block attributes require comma separators");
      return std::nullopt;
    }
    expect_attribute = !expect_attribute;
  }
  if (expect_attribute) {
    diagnose(diagnostics, line.source.number,
             "Matlab arguments block attribute list cannot end with a comma");
    return std::nullopt;
  }
  return direction.value_or(ArgumentDirection::input);
}

}  // namespace

MatlabArgumentBlockParseResult parse_matlab_argument_blocks(
    const std::vector<MatlabStatementLine>& lines, const std::size_t first_line,
    const LanguageVersion version) {
  MatlabArgumentBlockParseResult result;
  result.next_line = first_line;
  bool saw_output = false;
  while (result.next_line < lines.size() && token_count(lines[result.next_line]) != 0U &&
         lines[result.next_line].tokens[0].kind == Kind::keyword_arguments) {
    result.present = true;
    const auto header_line = lines[result.next_line].source.number;
    if (version < LanguageVersion{2019, 2}) {
      diagnose_version(result.diagnostics, header_line,
                       "Matlab arguments blocks require Matlab R2019b or newer");
    }
    bool repeating = false;
    const auto direction = parse_header(lines[result.next_line], repeating, result.diagnostics);
    if (direction.has_value() && *direction == ArgumentDirection::output &&
        version < LanguageVersion{2022, 2}) {
      diagnose_version(result.diagnostics, header_line,
                       "Matlab output arguments blocks require Matlab R2022b or newer");
    }
    if (direction.has_value() && *direction == ArgumentDirection::input && saw_output) {
      diagnose(result.diagnostics, header_line,
               "Matlab input arguments blocks must precede output arguments blocks");
    }
    if (direction.has_value() && *direction == ArgumentDirection::output) saw_output = true;
    if (repeating) {
      diagnose(result.diagnostics, header_line,
               "Matlab repeating arguments require the cell/varargs object model and are not yet "
               "supported");
    }
    ++result.next_line;
    while (result.next_line < lines.size() &&
           !(token_count(lines[result.next_line]) == 1U &&
             lines[result.next_line].tokens[0].kind == Kind::keyword_end)) {
      const auto first = token_count(lines[result.next_line]) == 0U
                             ? Kind::end
                             : lines[result.next_line].tokens[0].kind;
      if (first != Kind::identifier) {
        diagnose(result.diagnostics, header_line,
                 "Matlab arguments block is missing its terminating end");
        return result;
      }
      if (direction.has_value()) {
        auto declaration =
            parse_declaration(lines[result.next_line], *direction, version, result.diagnostics);
        if (declaration.has_value()) result.declarations.push_back(std::move(*declaration));
      }
      ++result.next_line;
    }
    if (result.next_line >= lines.size()) {
      diagnose(result.diagnostics, header_line,
               "Matlab arguments block is missing its terminating end");
      return result;
    }
    ++result.next_line;
  }
  return result;
}

}  // namespace mpf::detail
