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
  std::size_t explicit_operand_count{0U};
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
       {ArgumentValidator::valid_variable_name, validator_expansion_release, "R2020b"}},
      {"mustBeGreaterThan", {ArgumentValidator::greater_than, arguments_release, "R2019b", 1U}},
      {"mustBeGreaterThanOrEqual",
       {ArgumentValidator::greater_than_or_equal, arguments_release, "R2019b", 1U}},
      {"mustBeLessThan", {ArgumentValidator::less_than, arguments_release, "R2019b", 1U}},
      {"mustBeLessThanOrEqual",
       {ArgumentValidator::less_than_or_equal, arguments_release, "R2019b", 1U}},
      {"mustBeInRange", {ArgumentValidator::in_range, validator_expansion_release, "R2020b", 2U}}};
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

std::optional<ArgumentValidatorOperandSyntax> parse_validator_threshold(
    const MatlabStatementLine& line, const std::size_t first, const std::size_t last,
    std::vector<Diagnostic>& diagnostics) {
  ArgumentValidatorOperandSyntax operand;
  if (first + 1U == last && line.tokens[first].kind == Kind::identifier) {
    operand.kind = ArgumentValidatorOperandKind::input_argument;
    operand.value = line.tokens[first].text;
    return operand;
  }
  const bool unsigned_number = first + 1U == last && line.tokens[first].kind == Kind::number;
  const bool signed_number = first + 2U == last && line.tokens[first].kind == Kind::other &&
                             (line.tokens[first].text == "+" || line.tokens[first].text == "-") &&
                             line.tokens[first + 1U].kind == Kind::number;
  if (!unsigned_number && !signed_number) {
    diagnose(diagnostics, line.source.number,
             "parameterized Matlab validators currently require a scalar numeric literal or "
             "earlier scalar input argument");
    return std::nullopt;
  }
  operand.value =
      (signed_number ? line.tokens[first].text : std::string{}) + line.tokens[last - 1U].text;
  if (!valid_argument_numeric_literal(operand.value)) {
    diagnose(diagnostics, line.source.number,
             "parameterized Matlab validators require a decimal scalar threshold literal");
    return std::nullopt;
  }
  return operand;
}

std::optional<ArgumentRangeBoundary> parse_range_flag(const MatlabStatementLine& line,
                                                      const std::size_t first,
                                                      const std::size_t last) {
  if (first + 1U != last || line.tokens[first].kind != Kind::string_literal) return std::nullopt;
  const auto& token = line.tokens[first].text;
  if (token.size() < 2U) return std::nullopt;
  const auto value = std::string_view(token).substr(1U, token.size() - 2U);
  if (value == "inclusive") return ArgumentRangeBoundary::inclusive;
  if (value == "exclusive") return ArgumentRangeBoundary::exclusive;
  if (value == "exclude-lower") return ArgumentRangeBoundary::exclude_lower;
  if (value == "exclude-upper") return ArgumentRangeBoundary::exclude_upper;
  return std::nullopt;
}

bool parse_validator_call(const MatlabStatementLine& line, const std::size_t opening,
                          const std::size_t closing, const std::size_t operand_count,
                          const std::string_view argument_name, ArgumentValidatorSyntax& validator,
                          std::vector<Diagnostic>& diagnostics) {
  std::vector<std::pair<std::size_t, std::size_t>> arguments;
  auto first = opening + 1U;
  for (auto token = first; token < closing; ++token) {
    if (is_opening(line.tokens[token].kind)) {
      token = matching_token(line, token);
    } else if (line.tokens[token].kind == Kind::comma) {
      arguments.emplace_back(first, token);
      first = token + 1U;
    }
  }
  arguments.emplace_back(first, closing);
  const auto expected = 1U + operand_count;
  const auto maximum = expected + (validator.validator == ArgumentValidator::in_range ? 2U : 0U);
  if (arguments.size() < expected || arguments.size() > maximum) {
    diagnose(diagnostics, line.source.number, "Matlab validator call has incorrect operand arity");
    return false;
  }
  const auto& value = arguments.front();
  if (value.first + 1U != value.second || line.tokens[value.first].kind != Kind::identifier ||
      line.tokens[value.first].text != argument_name) {
    diagnose(diagnostics, line.source.number,
             "Matlab validator call must name the declared argument first");
    return false;
  }
  for (std::size_t index = 1U; index < expected; ++index) {
    const auto operand = parse_validator_threshold(line, arguments[index].first,
                                                   arguments[index].second, diagnostics);
    if (!operand.has_value()) return false;
    validator.operands.push_back(*operand);
  }
  for (auto index = expected; index < arguments.size(); ++index) {
    const auto flag = parse_range_flag(line, arguments[index].first, arguments[index].second);
    if (!flag.has_value()) {
      diagnose(diagnostics, line.source.number,
               "Matlab mustBeInRange flags must be literal inclusive, exclusive, exclude-lower, "
               "or exclude-upper text");
      return false;
    }
    validator.range_flags.push_back(*flag);
  }
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
  std::size_t token = cursor + 1U;
  while (token < closing) {
    if (!expect_validator) {
      if (line.tokens[token].kind != Kind::comma) {
        diagnose(diagnostics, line.source.number,
                 "Matlab arguments validators require a comma-separated function list");
        return false;
      }
      ++token;
      expect_validator = true;
      continue;
    }
    if (line.tokens[token].kind != Kind::identifier) {
      diagnose(diagnostics, line.source.number,
               "Matlab arguments validators must be named validation functions");
      return false;
    }
    const auto validator_name = line.tokens[token].text;
    const auto validator_start = token;
    const auto definition = argument_validator(validator_name);
    if (!definition.has_value()) {
      diagnose(diagnostics, line.source.number,
               "custom Matlab arguments validator '" + validator_name + "' is not yet supported");
      return false;
    }
    if (version < definition->minimum_version) {
      diagnose_version(diagnostics, line.source.number,
                       "Matlab validator '" + validator_name + "' requires Matlab " +
                           std::string(definition->minimum_release) + " or newer");
      return false;
    }

    ArgumentValidatorSyntax validator;
    validator.validator = definition->validator;
    ++token;
    if (token < closing && line.tokens[token].kind == Kind::left_parenthesis) {
      const auto call_closing = matching_token(line, token);
      if (call_closing >= closing) {
        diagnose(diagnostics, line.source.number,
                 "Matlab arguments validator call has no matching right parenthesis");
        return false;
      }
      if (!parse_validator_call(line, token, call_closing, definition->explicit_operand_count,
                                declaration.syntax.name, validator, diagnostics))
        return false;
      token = call_closing + 1U;
    } else if (definition->explicit_operand_count != 0U) {
      diagnose(
          diagnostics, line.source.number,
          "Matlab validator '" + validator_name +
              "' requires an explicit call with the validated argument and threshold operands");
      return false;
    }
    declaration.syntax.validators.push_back(std::move(validator));
    declaration.validator_sources.push_back(
        token > validator_start + 1U ? token_slice(line, validator_start, token)
                                     : validator_name + "(" + declaration.syntax.name + ")");
    expect_validator = false;
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
