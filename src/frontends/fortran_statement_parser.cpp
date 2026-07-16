#include "fortran_statement_parser.hpp"

#include <cstddef>
#include <iterator>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "common.hpp"

namespace mpf::detail {
namespace {

using Kind = FortranStatementTokenKind;

std::size_t token_count(const FortranStatementLine& line) noexcept {
  return line.tokens.empty() ? 0 : line.tokens.size() - 1;
}

std::string token_slice(const FortranStatementLine& line, const std::size_t first,
                        const std::size_t last) {
  if (first >= last || last > token_count(line)) return {};
  const auto begin = line.tokens[first].begin;
  const auto end = line.tokens[last - 1].end;
  return frontend::trim(std::string_view(line.source.text).substr(begin, end - begin));
}

bool is_opening(const Kind kind) noexcept {
  return kind == Kind::left_parenthesis || kind == Kind::left_bracket;
}

bool is_closing(const Kind kind) noexcept {
  return kind == Kind::right_parenthesis || kind == Kind::right_bracket;
}

bool matches(const Kind opening, const Kind closing) noexcept {
  return (opening == Kind::left_parenthesis && closing == Kind::right_parenthesis) ||
         (opening == Kind::left_bracket && closing == Kind::right_bracket);
}

bool is_name_kind(const Kind kind) noexcept {
  return kind == Kind::identifier ||
         (kind >= Kind::keyword_program && kind <= Kind::unsupported_keyword);
}

std::size_t matching_token(const FortranStatementLine& line, const std::size_t opening) noexcept {
  if (opening >= token_count(line) || !is_opening(line.tokens[opening].kind)) {
    return token_count(line);
  }
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

std::vector<std::size_t> top_level_tokens(const FortranStatementLine& line, const Kind wanted,
                                          const std::size_t first, const std::size_t last) {
  std::vector<std::size_t> result;
  std::vector<Kind> stack;
  for (std::size_t token = first; token < last; ++token) {
    const auto kind = line.tokens[token].kind;
    if (is_opening(kind)) {
      stack.push_back(kind);
    } else if (is_closing(kind)) {
      if (!stack.empty() && matches(stack.back(), kind)) stack.pop_back();
    } else if (kind == wanted && stack.empty()) {
      result.push_back(token);
    }
  }
  return result;
}

std::optional<std::size_t> parse_dimension(const std::string_view digits) {
  if (digits.empty()) return std::nullopt;
  std::size_t value = 0;
  for (const char character : digits) {
    if (character < '0' || character > '9') return std::nullopt;
    const auto digit = static_cast<std::size_t>(character - '0');
    if (value > (std::numeric_limits<std::size_t>::max() - digit) / 10U) {
      return std::nullopt;
    }
    value = value * 10U + digit;
  }
  return value == 0 ? std::nullopt : std::optional<std::size_t>{value};
}

class Parser final {
 public:
  Parser(std::vector<FortranStatementLine> lines, std::vector<Diagnostic> diagnostics)
      : lines_(std::move(lines)), diagnostics_(std::move(diagnostics)) {}

  ParseResult parse() {
    ParseResult result;
    result.program.language = SourceLanguage::fortran;
    result.program.statements = parse_block();
    while (index_ < lines_.size()) {
      frontend::unsupported(diagnostics_, lines_[index_].source.number,
                            "unexpected Fortran block terminator");
      ++index_;
      auto recovered = parse_block();
      result.program.statements.insert(result.program.statements.end(),
                                       std::make_move_iterator(recovered.begin()),
                                       std::make_move_iterator(recovered.end()));
    }
    result.diagnostics = std::move(diagnostics_);
    return result;
  }

 private:
  bool starts_with(const FortranStatementLine& line, const Kind kind) const noexcept {
    return token_count(line) != 0 && line.tokens.front().kind == kind;
  }

  bool exact(const FortranStatementLine& line, const Kind kind) const noexcept {
    return token_count(line) == 1 && line.tokens.front().kind == kind;
  }

  bool is_else_if(const FortranStatementLine& line) const noexcept {
    return starts_with(line, Kind::keyword_elseif) ||
           (token_count(line) >= 2 && line.tokens[0].kind == Kind::keyword_else &&
            line.tokens[1].kind == Kind::keyword_if);
  }

  bool is_end_if(const FortranStatementLine& line) const noexcept {
    return exact(line, Kind::keyword_endif) ||
           (token_count(line) == 2 && line.tokens[0].kind == Kind::keyword_end &&
            line.tokens[1].kind == Kind::keyword_if);
  }

  bool is_end_do(const FortranStatementLine& line) const noexcept {
    return exact(line, Kind::keyword_enddo) ||
           (token_count(line) == 2 && line.tokens[0].kind == Kind::keyword_end &&
            line.tokens[1].kind == Kind::keyword_do);
  }

  bool is_end_select(const FortranStatementLine& line) const noexcept {
    return exact(line, Kind::keyword_endselect) ||
           (token_count(line) == 2 && line.tokens[0].kind == Kind::keyword_end &&
            line.tokens[1].kind == Kind::keyword_select);
  }

  bool is_end_function(const FortranStatementLine& line) const noexcept {
    const auto count = token_count(line);
    return count >= 2 && count <= 3 && line.tokens[0].kind == Kind::keyword_end &&
           line.tokens[1].kind == Kind::keyword_function &&
           (count == 2 || is_name_kind(line.tokens[2].kind));
  }

  bool is_end_subroutine(const FortranStatementLine& line) const noexcept {
    const auto count = token_count(line);
    return count >= 2 && count <= 3 && line.tokens[0].kind == Kind::keyword_end &&
           line.tokens[1].kind == Kind::keyword_subroutine &&
           (count == 2 || is_name_kind(line.tokens[2].kind));
  }

  bool is_terminator(const FortranStatementLine& line) const noexcept {
    return exact(line, Kind::keyword_else) || is_else_if(line) || is_end_if(line) ||
           is_end_do(line) || starts_with(line, Kind::keyword_case) || is_end_select(line) ||
           is_end_function(line) || is_end_subroutine(line);
  }

  bool procedure_type_prefix(const FortranStatementLine& line, std::size_t& cursor,
                             ValueType& type) const noexcept {
    const auto count = token_count(line);
    if (cursor >= count) return false;
    const auto first = line.tokens[cursor].kind;
    if (first == Kind::keyword_integer)
      type = ValueType::integer;
    else if (first == Kind::keyword_real)
      type = ValueType::real;
    else if (first == Kind::keyword_logical)
      type = ValueType::boolean;
    else if (first == Kind::keyword_character)
      type = ValueType::string;
    else if (first == Kind::keyword_complex)
      type = ValueType::unknown;
    else if (first == Kind::keyword_double && cursor + 1 < count &&
             line.tokens[cursor + 1].kind == Kind::keyword_precision) {
      type = ValueType::real;
      cursor += 2;
      return true;
    } else {
      return false;
    }
    ++cursor;
    if (cursor < count && line.tokens[cursor].kind == Kind::left_parenthesis) {
      const auto closing = matching_token(line, cursor);
      if (closing >= count) return false;
      cursor = closing + 1;
    } else if (cursor + 1 < count && line.tokens[cursor].kind == Kind::star &&
               line.tokens[cursor + 1].kind == Kind::number) {
      cursor += 2;
    }
    return true;
  }

  bool is_procedure_header(const FortranStatementLine& line) const noexcept {
    const auto count = token_count(line);
    std::size_t cursor = 0;
    if (cursor < count && line.tokens[cursor].kind == Kind::keyword_recursive) ++cursor;
    if (cursor < count && (line.tokens[cursor].kind == Kind::keyword_function ||
                           line.tokens[cursor].kind == Kind::keyword_subroutine)) {
      return cursor + 2 < count && is_name_kind(line.tokens[cursor + 1].kind) &&
             line.tokens[cursor + 2].kind == Kind::left_parenthesis;
    }
    ValueType ignored = ValueType::unknown;
    return procedure_type_prefix(line, cursor, ignored) && cursor + 2 < count &&
           line.tokens[cursor].kind == Kind::keyword_function &&
           is_name_kind(line.tokens[cursor + 1].kind) &&
           line.tokens[cursor + 2].kind == Kind::left_parenthesis;
  }

  void expect_end_if(const std::size_t owner_line) {
    if (index_ >= lines_.size() || !is_end_if(lines_[index_])) {
      frontend::unsupported(diagnostics_, owner_line, "Fortran if statement is missing END IF");
      return;
    }
    ++index_;
  }

  void expect_end_do(const std::size_t owner_line, const std::string_view owner) {
    if (index_ >= lines_.size() || !is_end_do(lines_[index_])) {
      frontend::unsupported(diagnostics_, owner_line,
                            "Fortran " + std::string(owner) + " is missing END DO");
      return;
    }
    ++index_;
  }

  void expect_end_select(const std::size_t owner_line) {
    if (index_ >= lines_.size() || !is_end_select(lines_[index_])) {
      frontend::unsupported(diagnostics_, owner_line, "Fortran SELECT CASE is missing END SELECT");
      return;
    }
    ++index_;
  }

  bool parse_parameter_list(const FortranStatementLine& line, const std::size_t opening,
                            const std::size_t closing, std::vector<std::string>& parameters) {
    if (closing == opening + 1) return true;
    const auto commas = top_level_tokens(line, Kind::comma, opening + 1, closing);
    std::size_t begin = opening + 1;
    for (std::size_t part = 0; part <= commas.size(); ++part) {
      const auto end = part < commas.size() ? commas[part] : closing;
      if (end != begin + 1 || !is_name_kind(line.tokens[begin].kind)) return false;
      parameters.push_back(line.tokens[begin].text);
      begin = end + 1;
    }
    return true;
  }

  void expect_end_procedure(const Statement& procedure, const bool subroutine) {
    if (index_ >= lines_.size() ||
        (subroutine ? !is_end_subroutine(lines_[index_]) : !is_end_function(lines_[index_]))) {
      frontend::unsupported(diagnostics_, procedure.line,
                            "Fortran procedure is missing its END " +
                                std::string(subroutine ? "SUBROUTINE" : "FUNCTION"));
      return;
    }
    const auto& terminator = lines_[index_];
    if (token_count(terminator) == 3 && terminator.tokens[2].text != procedure.name) {
      frontend::unsupported(diagnostics_, terminator.source.number,
                            "Fortran procedure END name does not match its header");
    }
    ++index_;
  }

  Statement parse_procedure() {
    const auto& line = lines_[index_];
    const auto count = token_count(line);
    Statement statement;
    statement.kind = StatementKind::function;
    statement.line = line.source.number;

    std::size_t cursor = 0;
    const bool recursive = cursor < count && line.tokens[cursor].kind == Kind::keyword_recursive;
    if (recursive) ++cursor;
    ValueType prefixed_type = ValueType::unknown;
    bool has_prefixed_type = false;
    if (cursor < count && line.tokens[cursor].kind != Kind::keyword_function &&
        line.tokens[cursor].kind != Kind::keyword_subroutine) {
      has_prefixed_type = procedure_type_prefix(line, cursor, prefixed_type);
    }
    const bool subroutine = cursor < count && line.tokens[cursor].kind == Kind::keyword_subroutine;
    const bool function = cursor < count && line.tokens[cursor].kind == Kind::keyword_function;
    bool valid = function || subroutine;
    if (subroutine && has_prefixed_type) valid = false;
    ++cursor;
    if (valid && cursor < count && is_name_kind(line.tokens[cursor].kind)) {
      statement.name = line.tokens[cursor].text;
      ++cursor;
    } else {
      valid = false;
    }
    if (valid && cursor < count && line.tokens[cursor].kind == Kind::left_parenthesis) {
      const auto closing = matching_token(line, cursor);
      if (closing >= count || !parse_parameter_list(line, cursor, closing, statement.parameters)) {
        valid = false;
      }
      cursor = closing + 1;
    } else {
      valid = false;
    }
    if (function) {
      std::string result_name = statement.name;
      bool explicit_result = false;
      if (valid && cursor < count && line.tokens[cursor].kind == Kind::keyword_result) {
        const auto result_opening = cursor + 1;
        if (result_opening + 2 >= count ||
            line.tokens[result_opening].kind != Kind::left_parenthesis ||
            matching_token(line, result_opening) != result_opening + 2 ||
            !is_name_kind(line.tokens[result_opening + 1].kind)) {
          valid = false;
        } else {
          result_name = line.tokens[result_opening + 1].text;
          explicit_result = true;
          cursor = result_opening + 3;
        }
      }
      if (recursive && !explicit_result) valid = false;
      statement.return_names.push_back(std::move(result_name));
      if (has_prefixed_type) statement.return_types.push_back(prefixed_type);
    }
    const std::unordered_set<std::string> unique_parameters(statement.parameters.begin(),
                                                            statement.parameters.end());
    if (unique_parameters.size() != statement.parameters.size() ||
        (!statement.return_names.empty() &&
         unique_parameters.count(statement.return_names.front()) != 0U)) {
      valid = false;
    }
    if (cursor != count) valid = false;
    if (has_prefixed_type && prefixed_type == ValueType::unknown) {
      frontend::unsupported(
          diagnostics_, line.source.number,
          "Fortran complex function results require the complex runtime milestone");
    }
    if (!valid) {
      frontend::unsupported(diagnostics_, line.source.number,
                            "malformed Fortran FUNCTION/SUBROUTINE header");
    }

    ++index_;
    ++procedure_depth_;
    procedure_returns_.push_back(statement.return_names);
    statement.body = parse_block();
    procedure_returns_.pop_back();
    --procedure_depth_;
    expect_end_procedure(statement, subroutine);
    return statement;
  }

  bool append_if_condition(Statement& statement, const FortranStatementLine& line,
                           const std::size_t if_token, const std::string_view label) {
    const auto count = token_count(line);
    const auto opening = if_token + 1;
    if (opening >= count || line.tokens[opening].kind != Kind::left_parenthesis) {
      frontend::unsupported(diagnostics_, line.source.number,
                            "malformed Fortran " + std::string(label));
      return false;
    }
    const auto closing = matching_token(line, opening);
    if (closing + 1 >= count || closing != count - 2 ||
        line.tokens[closing + 1].kind != Kind::keyword_then) {
      frontend::unsupported(diagnostics_, line.source.number,
                            "malformed Fortran " + std::string(label));
      return false;
    }
    frontend::append_expression(statement, token_slice(line, opening + 1, closing),
                                SourceLanguage::fortran, line.source.number, diagnostics_);
    return statement.has_expression;
  }

  Statement parse_else_if() {
    const auto& line = lines_[index_];
    Statement statement;
    statement.kind = StatementKind::if_statement;
    statement.line = line.source.number;
    const auto if_token = starts_with(line, Kind::keyword_elseif) ? std::size_t{0} : std::size_t{1};
    static_cast<void>(append_if_condition(statement, line, if_token, "ELSE IF clause"));
    ++index_;
    statement.body = parse_block();
    if (index_ < lines_.size() && exact(lines_[index_], Kind::keyword_else)) {
      ++index_;
      statement.alternative = parse_block();
    } else if (index_ < lines_.size() && is_else_if(lines_[index_])) {
      statement.alternative.push_back(parse_else_if());
    }
    return statement;
  }

  Statement parse_if() {
    const auto line_number = lines_[index_].source.number;
    Statement statement;
    statement.kind = StatementKind::if_statement;
    statement.line = line_number;
    static_cast<void>(append_if_condition(statement, lines_[index_], 0, "IF statement"));
    ++index_;
    statement.body = parse_block();
    if (index_ < lines_.size() && exact(lines_[index_], Kind::keyword_else)) {
      ++index_;
      statement.alternative = parse_block();
    } else if (index_ < lines_.size() && is_else_if(lines_[index_])) {
      statement.alternative.push_back(parse_else_if());
    }
    expect_end_if(line_number);
    return statement;
  }

  void append_case_selector(const FortranStatementLine& line, const std::size_t first,
                            const std::size_t last, Statement& clause) {
    if (first >= last) {
      frontend::unsupported(diagnostics_, line.source.number,
                            "Fortran CASE selector cannot be empty");
      return;
    }
    const auto colons = top_level_tokens(line, Kind::colon, first, last);
    if (colons.size() > 1) {
      frontend::unsupported(diagnostics_, line.source.number,
                            "Fortran CASE selector range has too many colons");
      return;
    }

    CaseSelector selector;
    selector.range = !colons.empty();
    if (colons.empty()) {
      frontend::append_expression(selector.lower, selector.has_lower,
                                  token_slice(line, first, last), SourceLanguage::fortran,
                                  line.source.number, diagnostics_);
    } else {
      const auto colon = colons.front();
      if (first < colon) {
        frontend::append_expression(selector.lower, selector.has_lower,
                                    token_slice(line, first, colon), SourceLanguage::fortran,
                                    line.source.number, diagnostics_);
      }
      if (colon + 1 < last) {
        frontend::append_expression(selector.upper, selector.has_upper,
                                    token_slice(line, colon + 1, last), SourceLanguage::fortran,
                                    line.source.number, diagnostics_);
      }
      if (!selector.has_lower && !selector.has_upper) {
        frontend::unsupported(diagnostics_, line.source.number,
                              "Fortran CASE selector range requires a bound");
        return;
      }
    }
    clause.case_selectors.push_back(std::move(selector));
  }

  Statement parse_case_clause() {
    const auto& line = lines_[index_];
    const auto count = token_count(line);
    Statement clause;
    clause.kind = StatementKind::case_clause;
    clause.line = line.source.number;

    if (count == 2 && line.tokens[0].kind == Kind::keyword_case &&
        line.tokens[1].kind == Kind::keyword_default) {
      clause.default_case = true;
    } else if (count >= 4 && line.tokens[0].kind == Kind::keyword_case &&
               line.tokens[1].kind == Kind::left_parenthesis &&
               matching_token(line, 1) == count - 1) {
      const auto closing = count - 1;
      const auto commas = top_level_tokens(line, Kind::comma, 2, closing);
      std::size_t begin = 2;
      for (std::size_t part = 0; part <= commas.size(); ++part) {
        const auto end = part < commas.size() ? commas[part] : closing;
        append_case_selector(line, begin, end, clause);
        begin = end + 1;
      }
      if (clause.case_selectors.empty()) {
        frontend::unsupported(diagnostics_, line.source.number,
                              "Fortran CASE requires at least one selector");
      }
    } else {
      frontend::unsupported(diagnostics_, line.source.number, "malformed Fortran CASE clause");
    }

    ++index_;
    clause.body = parse_block();
    return clause;
  }

  Statement parse_select_case() {
    const auto& line = lines_[index_];
    const auto count = token_count(line);
    Statement statement;
    statement.kind = StatementKind::select_case;
    statement.line = line.source.number;

    const bool valid_header = count >= 5 && line.tokens[0].kind == Kind::keyword_select &&
                              line.tokens[1].kind == Kind::keyword_case &&
                              line.tokens[2].kind == Kind::left_parenthesis &&
                              matching_token(line, 2) == count - 1;
    if (!valid_header) {
      frontend::unsupported(diagnostics_, line.source.number,
                            "malformed Fortran SELECT CASE header");
    } else {
      frontend::append_expression(statement, token_slice(line, 3, count - 1),
                                  SourceLanguage::fortran, line.source.number, diagnostics_);
    }

    ++index_;
    bool saw_default = false;
    while (index_ < lines_.size() && starts_with(lines_[index_], Kind::keyword_case)) {
      auto clause = parse_case_clause();
      if (clause.default_case && saw_default) {
        frontend::unsupported(diagnostics_, clause.line,
                              "Fortran SELECT CASE contains more than one CASE DEFAULT");
      }
      saw_default = saw_default || clause.default_case;
      statement.body.push_back(std::move(clause));
    }
    if (statement.body.empty()) {
      frontend::unsupported(diagnostics_, statement.line,
                            "Fortran SELECT CASE requires at least one CASE clause");
    }
    expect_end_select(statement.line);
    return statement;
  }

  Statement parse_do_while() {
    const auto& line = lines_[index_];
    const auto count = token_count(line);
    Statement statement;
    statement.kind = StatementKind::while_loop;
    statement.line = line.source.number;
    bool valid = count >= 5 && line.tokens[0].kind == Kind::keyword_do &&
                 line.tokens[1].kind == Kind::keyword_while &&
                 line.tokens[2].kind == Kind::left_parenthesis;
    std::size_t closing = count;
    if (valid) {
      closing = matching_token(line, 2);
      valid = closing == count - 1;
    }
    if (!valid) {
      frontend::unsupported(diagnostics_, line.source.number, "malformed Fortran DO WHILE loop");
    } else {
      frontend::append_expression(statement, token_slice(line, 3, closing), SourceLanguage::fortran,
                                  line.source.number, diagnostics_);
    }
    ++index_;
    statement.body = parse_block();
    expect_end_do(statement.line, "DO WHILE loop");
    return statement;
  }

  Statement parse_counted_do() {
    const auto& line = lines_[index_];
    const auto count = token_count(line);
    Statement statement;
    statement.kind = StatementKind::range_loop;
    statement.line = line.source.number;
    statement.inclusive_stop = true;
    statement.retain_last_loop_value = false;
    const bool header = count >= 6 && line.tokens[0].kind == Kind::keyword_do &&
                        is_name_kind(line.tokens[1].kind) && line.tokens[2].kind == Kind::equal;
    const auto commas =
        header ? top_level_tokens(line, Kind::comma, 3, count) : std::vector<std::size_t>{};
    if (!header || (commas.size() != 1 && commas.size() != 2)) {
      frontend::unsupported(diagnostics_, line.source.number,
                            "Fortran counted DO requires start, stop, and optional step");
    } else {
      statement.name = line.tokens[1].text;
      frontend::append_expression(statement, token_slice(line, 3, commas[0]),
                                  SourceLanguage::fortran, line.source.number, diagnostics_);
      frontend::append_expression(
          statement.secondary_expression, statement.has_secondary_expression,
          token_slice(line, commas[0] + 1, commas.size() == 2 ? commas[1] : count),
          SourceLanguage::fortran, line.source.number, diagnostics_);
      if (commas.size() == 2) {
        frontend::append_expression(statement.tertiary_expression,
                                    statement.has_tertiary_expression,
                                    token_slice(line, commas[1] + 1, count),
                                    SourceLanguage::fortran, line.source.number, diagnostics_);
      }
    }
    ++index_;
    statement.body = parse_block();
    expect_end_do(statement.line, "counted DO loop");
    return statement;
  }

  bool declaration_type(const FortranStatementLine& line, ValueType& type, std::size_t& cursor) {
    const auto first = line.tokens[0].kind;
    cursor = 1;
    if (first == Kind::keyword_integer)
      type = ValueType::integer;
    else if (first == Kind::keyword_real)
      type = ValueType::real;
    else if (first == Kind::keyword_logical)
      type = ValueType::boolean;
    else if (first == Kind::keyword_character)
      type = ValueType::string;
    else if (first == Kind::keyword_complex) {
      type = ValueType::unknown;
      frontend::unsupported(diagnostics_, line.source.number,
                            "Fortran complex declarations require the complex runtime milestone");
    } else if (first == Kind::keyword_double && token_count(line) > 1 &&
               line.tokens[1].kind == Kind::keyword_precision) {
      type = ValueType::real;
      cursor = 2;
    } else {
      return false;
    }
    return true;
  }

  std::vector<std::size_t> parse_shape(const FortranStatementLine& line, const std::size_t opening,
                                       const std::size_t closing) {
    std::vector<std::size_t> shape;
    const auto commas = top_level_tokens(line, Kind::comma, opening + 1, closing);
    std::size_t begin = opening + 1;
    for (std::size_t part = 0; part <= commas.size(); ++part) {
      const auto end = part < commas.size() ? commas[part] : closing;
      if (end == begin + 1 && line.tokens[begin].kind == Kind::colon) {
        shape.push_back(dynamic_extent);
        begin = end + 1;
        continue;
      }
      if (end != begin + 1 || line.tokens[begin].kind != Kind::number) {
        shape.clear();
        return shape;
      }
      const auto dimension = parse_dimension(line.tokens[begin].text);
      if (!dimension.has_value()) {
        shape.clear();
        return shape;
      }
      shape.push_back(*dimension);
      begin = end + 1;
    }
    return shape;
  }

  void parse_declarator(const FortranStatementLine& line, const std::size_t first,
                        const std::size_t last, const ValueType declared_type,
                        const ParameterIntent parameter_intent, const bool optional_parameter,
                        std::vector<Statement>& statements) {
    const auto equals = top_level_tokens(line, Kind::equal, first, last);
    if (equals.size() > 1) {
      frontend::unsupported(diagnostics_, line.source.number,
                            "Fortran declarator has multiple initializers");
      return;
    }
    const auto lhs_end = equals.empty() ? last : equals[0];
    if (first >= lhs_end || !is_name_kind(line.tokens[first].kind)) {
      frontend::unsupported(diagnostics_, line.source.number, "malformed Fortran declarator");
      return;
    }
    Statement statement;
    statement.kind = StatementKind::declaration;
    statement.line = line.source.number;
    statement.name = line.tokens[first].text;
    statement.parameter_intent = parameter_intent;
    statement.optional_parameter = optional_parameter;
    if (lhs_end == first + 1) {
      statement.declared_type = declared_type;
    } else if (lhs_end >= first + 4 && line.tokens[first + 1].kind == Kind::left_parenthesis &&
               matching_token(line, first + 1) == lhs_end - 1) {
      statement.shape = parse_shape(line, first + 1, lhs_end - 1);
      if (statement.shape.empty() || statement.shape.size() > 2) {
        frontend::unsupported(diagnostics_, line.source.number,
                              "Fortran array extent is invalid or too large");
        return;
      }
      statement.declared_type = ValueType::list;
      statement.element_type = declared_type;
    } else {
      frontend::unsupported(
          diagnostics_, line.source.number,
          "only one- and two-dimensional constant-shape Fortran arrays are supported");
      return;
    }
    if (!equals.empty()) {
      if (equals[0] + 1 >= last) {
        frontend::unsupported(diagnostics_, line.source.number,
                              "Fortran declaration initializer requires an expression");
      } else {
        frontend::append_expression(statement, token_slice(line, equals[0] + 1, last),
                                    SourceLanguage::fortran, line.source.number, diagnostics_);
      }
    }
    statements.push_back(std::move(statement));
  }

  void parse_declaration(std::vector<Statement>& statements) {
    const auto& line = lines_[index_];
    const auto count = token_count(line);
    ValueType declared_type = ValueType::unknown;
    std::size_t cursor = 0;
    if (!declaration_type(line, declared_type, cursor)) {
      frontend::unsupported(diagnostics_, line.source.number, "malformed Fortran declaration type");
      ++index_;
      return;
    }
    if (cursor < count && line.tokens[cursor].kind == Kind::left_parenthesis) {
      const auto closing = matching_token(line, cursor);
      if (closing >= count) {
        frontend::unsupported(diagnostics_, line.source.number,
                              "malformed Fortran kind/len selector");
        ++index_;
        return;
      }
      cursor = closing + 1;
    } else if (cursor + 1 < count && line.tokens[cursor].kind == Kind::star &&
               line.tokens[cursor + 1].kind == Kind::number) {
      cursor += 2;
    }
    const auto separators = top_level_tokens(line, Kind::double_colon, cursor, count);
    if (separators.size() > 1) {
      frontend::unsupported(diagnostics_, line.source.number,
                            "malformed Fortran declaration separator");
      ++index_;
      return;
    }
    std::size_t declarations_begin = cursor;
    ParameterIntent parameter_intent = ParameterIntent::none;
    bool optional_parameter = false;
    if (!separators.empty()) {
      if (separators[0] != cursor) {
        const auto commas = top_level_tokens(line, Kind::comma, cursor, separators[0]);
        bool valid_attributes = !commas.empty() && commas.front() == cursor;
        for (std::size_t part = 0; part < commas.size(); ++part) {
          const auto begin = commas[part] + 1;
          const auto end = part + 1 < commas.size() ? commas[part + 1] : separators[0];
          const bool intent = end == begin + 4 && line.tokens[begin].text == "intent" &&
                              line.tokens[begin + 1].kind == Kind::left_parenthesis &&
                              line.tokens[begin + 3].kind == Kind::right_parenthesis;
          if (intent && parameter_intent == ParameterIntent::none &&
              line.tokens[begin + 2].text == "in") {
            parameter_intent = ParameterIntent::in;
          } else if (intent && parameter_intent == ParameterIntent::none &&
                     line.tokens[begin + 2].text == "out") {
            parameter_intent = ParameterIntent::out;
          } else if (intent && parameter_intent == ParameterIntent::none &&
                     line.tokens[begin + 2].text == "inout") {
            parameter_intent = ParameterIntent::inout;
          } else if (end == begin + 1 && line.tokens[begin].text == "optional" &&
                     !optional_parameter) {
            optional_parameter = true;
          } else {
            valid_attributes = false;
          }
        }
        if (!valid_attributes) {
          frontend::unsupported(diagnostics_, line.source.number,
                                "unsupported or duplicate Fortran declaration attribute");
        }
      }
      declarations_begin = separators[0] + 1;
    }
    if (declarations_begin >= count) {
      frontend::unsupported(diagnostics_, line.source.number,
                            "Fortran declaration requires at least one entity");
      ++index_;
      return;
    }
    const auto commas = top_level_tokens(line, Kind::comma, declarations_begin, count);
    std::size_t begin = declarations_begin;
    for (std::size_t part = 0; part <= commas.size(); ++part) {
      const auto end = part < commas.size() ? commas[part] : count;
      parse_declarator(line, begin, end, declared_type, parameter_intent, optional_parameter,
                       statements);
      begin = end + 1;
    }
    ++index_;
  }

  bool is_print(const FortranStatementLine& line, std::size_t& expression_begin) const {
    const auto count = token_count(line);
    if (count >= 4 && line.tokens[0].kind == Kind::keyword_print &&
        line.tokens[1].kind == Kind::star && line.tokens[2].kind == Kind::comma) {
      expression_begin = 3;
      return true;
    }
    if (count >= 7 && line.tokens[0].kind == Kind::keyword_write &&
        line.tokens[1].kind == Kind::left_parenthesis && line.tokens[2].kind == Kind::star &&
        line.tokens[3].kind == Kind::comma && line.tokens[4].kind == Kind::star &&
        line.tokens[5].kind == Kind::right_parenthesis) {
      expression_begin = line.tokens[6].kind == Kind::comma ? 7 : 6;
      return expression_begin < count;
    }
    return false;
  }

  void parse_simple_statement(std::vector<Statement>& statements) {
    const auto& line = lines_[index_];
    const auto count = token_count(line);
    const auto first = count == 0 ? Kind::end : line.tokens[0].kind;
    if (first == Kind::keyword_exit || first == Kind::keyword_cycle) {
      if (count != 1) {
        frontend::unsupported(diagnostics_, line.source.number,
                              "named Fortran EXIT/CYCLE is not supported");
      }
      Statement statement;
      statement.kind = first == Kind::keyword_exit ? StatementKind::break_statement
                                                   : StatementKind::continue_statement;
      statement.line = line.source.number;
      statements.push_back(std::move(statement));
      ++index_;
      return;
    }

    if (first == Kind::keyword_return) {
      if (count != 1) {
        frontend::unsupported(diagnostics_, line.source.number,
                              "alternate-return Fortran syntax is not supported");
      }
      Statement statement;
      statement.kind = StatementKind::return_statement;
      statement.line = line.source.number;
      if (procedure_depth_ == 0) {
        frontend::unsupported(diagnostics_, line.source.number,
                              "Fortran RETURN is only valid inside a procedure");
      } else if (!procedure_returns_.back().empty()) {
        frontend::append_expression(statement, procedure_returns_.back().front(),
                                    SourceLanguage::fortran, line.source.number, diagnostics_);
      }
      statements.push_back(std::move(statement));
      ++index_;
      return;
    }

    std::size_t expression_begin = count;
    if (is_print(line, expression_begin)) {
      Statement statement;
      statement.kind = StatementKind::print;
      statement.line = line.source.number;
      frontend::append_expression(statement, token_slice(line, expression_begin, count),
                                  SourceLanguage::fortran, line.source.number, diagnostics_);
      statements.push_back(std::move(statement));
      ++index_;
      return;
    }

    if (first == Kind::keyword_call) {
      Statement statement;
      statement.kind = StatementKind::expression;
      statement.line = line.source.number;
      statement.procedure_call = true;
      if (count == 2 && is_name_kind(line.tokens[1].kind)) {
        frontend::append_expression(statement, line.tokens[1].text + "()", SourceLanguage::fortran,
                                    line.source.number, diagnostics_);
      } else if (count < 4 || !is_name_kind(line.tokens[1].kind) ||
                 line.tokens[2].kind != Kind::left_parenthesis ||
                 matching_token(line, 2) != count - 1) {
        frontend::unsupported(diagnostics_, line.source.number, "malformed Fortran CALL statement");
      } else {
        frontend::append_expression(statement, token_slice(line, 1, count), SourceLanguage::fortran,
                                    line.source.number, diagnostics_);
      }
      statements.push_back(std::move(statement));
      ++index_;
      return;
    }

    const auto equals = top_level_tokens(line, Kind::equal, 0, count);
    if (!equals.empty()) {
      if (equals.size() != 1 || equals[0] == 0 || equals[0] + 1 >= count) {
        frontend::unsupported(diagnostics_, line.source.number,
                              "chained or valueless Fortran assignment is not supported");
        ++index_;
        return;
      }
      const auto equal = equals[0];
      Statement statement;
      statement.line = line.source.number;
      if (equal == 1 && is_name_kind(line.tokens[0].kind)) {
        statement.kind = StatementKind::assignment;
        statement.name = line.tokens[0].text;
      } else if (equal >= 4 && is_name_kind(line.tokens[0].kind) &&
                 line.tokens[1].kind == Kind::left_parenthesis &&
                 matching_token(line, 1) == equal - 1) {
        statement.kind = StatementKind::indexed_assignment;
        statement.name = line.tokens[0].text;
        frontend::append_expression(statement.target_expression, statement.has_target_expression,
                                    token_slice(line, 0, equal), SourceLanguage::fortran,
                                    line.source.number, diagnostics_);
      } else {
        frontend::unsupported(diagnostics_, line.source.number,
                              "Fortran assignment target must be a name or array element/section");
        ++index_;
        return;
      }
      frontend::append_expression(statement, token_slice(line, equal + 1, count),
                                  SourceLanguage::fortran, line.source.number, diagnostics_);
      statements.push_back(std::move(statement));
      ++index_;
      return;
    }

    const auto keyword = count == 0 ? std::string{} : line.tokens[0].text;
    frontend::unsupported(
        diagnostics_, line.source.number,
        "unsupported Fortran statement in the current language subset: " + keyword);
    ++index_;
  }

  bool skip_program_scaffolding() {
    const auto& line = lines_[index_];
    const auto count = token_count(line);
    const bool program_header = count == 2 && line.tokens[0].kind == Kind::keyword_program &&
                                is_name_kind(line.tokens[1].kind);
    const bool program_end = count >= 2 && count <= 3 && line.tokens[0].kind == Kind::keyword_end &&
                             line.tokens[1].kind == Kind::keyword_program &&
                             (count == 2 || is_name_kind(line.tokens[2].kind));
    if (program_header ||
        (count == 2 && line.tokens[0].kind == Kind::keyword_implicit &&
         line.tokens[1].kind == Kind::keyword_none) ||
        exact(line, Kind::keyword_contains) || exact(line, Kind::keyword_end) || program_end) {
      ++index_;
      return true;
    }
    return false;
  }

  bool is_declaration(const Kind kind) const noexcept {
    return kind == Kind::keyword_integer || kind == Kind::keyword_real ||
           kind == Kind::keyword_double || kind == Kind::keyword_complex ||
           kind == Kind::keyword_logical || kind == Kind::keyword_character;
  }

  std::vector<Statement> parse_block() {
    std::vector<Statement> statements;
    while (index_ < lines_.size()) {
      const auto& line = lines_[index_];
      if (is_terminator(line)) break;
      if (skip_program_scaffolding()) continue;
      const auto first = token_count(line) == 0 ? Kind::end : line.tokens[0].kind;
      if (is_procedure_header(line)) {
        const auto nested = procedure_depth_ != 0;
        auto procedure = parse_procedure();
        if (nested) {
          frontend::unsupported(diagnostics_, procedure.line,
                                "nested/internal procedures inside a procedure are not supported");
        } else {
          statements.push_back(std::move(procedure));
        }
      } else if (first == Kind::keyword_if) {
        statements.push_back(parse_if());
      } else if (first == Kind::keyword_select) {
        statements.push_back(parse_select_case());
      } else if (first == Kind::keyword_do && token_count(line) > 1 &&
                 line.tokens[1].kind == Kind::keyword_while) {
        statements.push_back(parse_do_while());
      } else if (first == Kind::keyword_do) {
        statements.push_back(parse_counted_do());
      } else if (is_declaration(first)) {
        parse_declaration(statements);
      } else {
        parse_simple_statement(statements);
      }
    }
    return statements;
  }

  std::vector<FortranStatementLine> lines_;
  std::vector<Diagnostic> diagnostics_;
  std::size_t index_{0};
  std::size_t procedure_depth_{0};
  std::vector<std::vector<std::string>> procedure_returns_;
};

}  // namespace

ParseResult parse_fortran_statements(std::vector<FortranStatementLine> lines,
                                     std::vector<Diagnostic> diagnostics) {
  return Parser{std::move(lines), std::move(diagnostics)}.parse();
}

}  // namespace mpf::detail
