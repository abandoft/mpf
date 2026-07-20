#include "frontends/matlab/statement_parser.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "frontends/common/ast_builder.hpp"
#include "frontends/common/parser_support.hpp"
#include "frontends/matlab/expression_lexer.hpp"

namespace mpf::detail {
namespace {

using Kind = MatlabStatementTokenKind;

std::size_t token_count(const MatlabStatementLine& line) noexcept {
  return line.tokens.empty() ? 0 : line.tokens.size() - 1;
}

std::string token_slice(const MatlabStatementLine& line, const std::size_t first,
                        const std::size_t last) {
  if (first >= last || last > token_count(line)) return {};
  const auto begin = line.tokens[first].begin;
  const auto end = line.tokens[last - 1].end;
  return frontend::trim(std::string_view(line.source.text).substr(begin, end - begin));
}

std::string lower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char character) {
    return static_cast<char>(std::tolower(character));
  });
  return value;
}

std::string quoted_character_vector(const std::string_view value) {
  std::string result;
  result.reserve(value.size() + 2U);
  result.push_back('\'');
  for (const char character : value) {
    result.push_back(character);
    if (character == '\'') result.push_back('\'');
  }
  result.push_back('\'');
  return result;
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

std::vector<std::size_t> top_level_tokens(const MatlabStatementLine& line, const Kind wanted,
                                          const std::size_t first = 0) {
  std::vector<std::size_t> result;
  std::vector<Kind> stack;
  for (std::size_t token = first; token < token_count(line); ++token) {
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

class Parser final {
 public:
  Parser(std::vector<MatlabStatementLine> lines, std::vector<Diagnostic> diagnostics,
         const LanguageVersion version, std::pmr::memory_resource* resource)
      : lines_(std::move(lines)),
        diagnostics_(std::move(diagnostics)),
        version_(version),
        builder_(SourceLanguage::matlab, &lex_matlab_expression, resource) {
    builder_.reserve(lines_.size(), lines_.size() * 2U);
  }

  matlab::ast::ParseResult parse() {
    auto roots = parse_block();
    while (index_ < lines_.size()) {
      frontend::unsupported(diagnostics_, lines_[index_].source.number,
                            "unexpected Matlab block terminator");
      ++index_;
      auto recovered = parse_block();
      roots.insert(roots.end(), std::make_move_iterator(recovered.begin()),
                   std::make_move_iterator(recovered.end()));
    }
    builder_.set_roots(std::move(roots));
    return {std::move(builder_).finish(), std::move(diagnostics_)};
  }

 private:
  using Statement = matlab::ast::Statement;
  using CaseSelector = matlab::ast::CaseSelector;

  AstNodeId store(Statement statement) { return builder_.add_statement(std::move(statement)); }

  void append_expression(AstNodeId& destination, bool& present, const std::string_view source,
                         const std::size_t line) {
    destination = builder_.parse_expression(source, SourceLanguage::matlab, line, diagnostics_);
    present = destination.valid();
  }

  void append_expression(Statement& statement, const std::string_view source,
                         const std::size_t line) {
    append_expression(statement.expression, statement.has_expression, source, line);
  }

  bool starts_with(const MatlabStatementLine& line, const Kind kind) const noexcept {
    return token_count(line) != 0 && line.tokens.front().kind == kind;
  }

  bool exact_keyword(const MatlabStatementLine& line, const Kind kind) const noexcept {
    return token_count(line) == 1 && line.tokens.front().kind == kind;
  }

  bool is_terminator(const MatlabStatementLine& line) const noexcept {
    return starts_with(line, Kind::keyword_end) || starts_with(line, Kind::keyword_else) ||
           starts_with(line, Kind::keyword_elseif) || starts_with(line, Kind::keyword_case) ||
           starts_with(line, Kind::keyword_otherwise) || starts_with(line, Kind::keyword_catch);
  }

  void expect_end(const std::size_t owner_line, const std::string_view owner) {
    if (index_ >= lines_.size() || !starts_with(lines_[index_], Kind::keyword_end)) {
      frontend::unsupported(diagnostics_, owner_line,
                            "Matlab " + std::string(owner) + " is missing its terminating end");
      return;
    }
    if (!exact_keyword(lines_[index_], Kind::keyword_end)) {
      frontend::unsupported(diagnostics_, lines_[index_].source.number,
                            "malformed Matlab end statement");
    }
    ++index_;
  }

  void append_condition(Statement& statement, const MatlabStatementLine& line,
                        const std::string_view label) {
    const auto expression = token_slice(line, 1, token_count(line));
    if (expression.empty()) {
      frontend::unsupported(diagnostics_, line.source.number,
                            "Matlab " + std::string(label) + " requires a condition");
      return;
    }
    append_expression(statement, expression, line.source.number);
  }

  bool parse_identifier_list(const MatlabStatementLine& line, const std::size_t first,
                             const std::size_t last, std::vector<std::string>& output,
                             const bool allow_implicit_separator) {
    bool valid = first < last;
    bool expect_identifier = true;
    for (std::size_t token = first; token < last; ++token) {
      if (line.tokens[token].kind == Kind::comma) {
        if (expect_identifier) valid = false;
        expect_identifier = true;
      } else if (line.tokens[token].kind == Kind::identifier && expect_identifier) {
        output.push_back(line.tokens[token].text);
        expect_identifier = false;
      } else if (line.tokens[token].kind == Kind::identifier && !expect_identifier) {
        if (allow_implicit_separator) {
          output.push_back(line.tokens[token].text);
        } else {
          valid = false;
        }
      } else {
        valid = false;
      }
    }
    return valid && !expect_identifier && !output.empty();
  }

  bool parse_function_signature(const MatlabStatementLine& line, Statement& statement) {
    const auto count = token_count(line);
    if (count < 2 || line.tokens[0].kind != Kind::keyword_function) return false;
    const auto equals = top_level_tokens(line, Kind::equal, 1);
    if (equals.size() > 1) return false;
    std::size_t signature = 1;
    if (!equals.empty()) {
      const auto equal = equals.front();
      if (equal == 2 && line.tokens[1].kind == Kind::identifier) {
        statement.return_names.push_back(line.tokens[1].text);
      } else if (equal >= 4 && line.tokens[1].kind == Kind::left_bracket &&
                 matching_token(line, 1) == equal - 1) {
        if (!parse_identifier_list(line, 2, equal - 1, statement.return_names, true)) return false;
      } else {
        return false;
      }
      signature = equal + 1;
    }
    if (signature >= count || line.tokens[signature].kind != Kind::identifier) return false;
    statement.name = line.tokens[signature].text;
    ++signature;
    if (signature == count) return true;
    if (line.tokens[signature].kind != Kind::left_parenthesis) return false;
    const auto closing = matching_token(line, signature);
    if (closing != count - 1) return false;
    if (closing > signature + 1 &&
        !parse_identifier_list(line, signature + 1, closing, statement.parameters, false)) {
      return false;
    }
    return true;
  }

  Statement parse_function() {
    const auto line_number = lines_[index_].source.number;
    Statement statement;
    statement.kind = StatementKind::function;
    statement.line = line_number;
    if (!parse_function_signature(lines_[index_], statement)) {
      frontend::unsupported(diagnostics_, line_number, "malformed Matlab function signature");
    }
    ++index_;
    function_returns_.push_back(statement.return_names);
    statement.body = parse_block();
    function_returns_.pop_back();
    expect_end(line_number, "function");
    return statement;
  }

  Statement parse_elseif() {
    const auto& line = lines_[index_];
    Statement statement;
    statement.kind = StatementKind::if_statement;
    statement.line = line.source.number;
    append_condition(statement, line, "elseif clause");
    ++index_;
    statement.body = parse_block();
    if (index_ < lines_.size() && starts_with(lines_[index_], Kind::keyword_else)) {
      const auto else_line = lines_[index_].source.number;
      if (!exact_keyword(lines_[index_], Kind::keyword_else)) {
        frontend::unsupported(diagnostics_, else_line, "malformed Matlab else clause");
      }
      ++index_;
      statement.alternative = parse_block();
    } else if (index_ < lines_.size() && starts_with(lines_[index_], Kind::keyword_elseif)) {
      statement.alternative.push_back(store(parse_elseif()));
    }
    return statement;
  }

  Statement parse_if() {
    const auto line_number = lines_[index_].source.number;
    Statement statement;
    statement.kind = StatementKind::if_statement;
    statement.line = line_number;
    append_condition(statement, lines_[index_], "if statement");
    ++index_;
    statement.body = parse_block();
    if (index_ < lines_.size() && starts_with(lines_[index_], Kind::keyword_else)) {
      const auto else_line = lines_[index_].source.number;
      if (!exact_keyword(lines_[index_], Kind::keyword_else)) {
        frontend::unsupported(diagnostics_, else_line, "malformed Matlab else clause");
      }
      ++index_;
      statement.alternative = parse_block();
    } else if (index_ < lines_.size() && starts_with(lines_[index_], Kind::keyword_elseif)) {
      statement.alternative.push_back(store(parse_elseif()));
    }
    expect_end(line_number, "if statement");
    return statement;
  }

  Statement parse_while() {
    const auto line_number = lines_[index_].source.number;
    Statement statement;
    statement.kind = StatementKind::while_loop;
    statement.line = line_number;
    append_condition(statement, lines_[index_], "while loop");
    ++index_;
    statement.body = parse_block();
    expect_end(line_number, "while loop");
    return statement;
  }

  Statement parse_try() {
    const auto line_number = lines_[index_].source.number;
    Statement statement;
    statement.kind = StatementKind::try_statement;
    statement.line = line_number;
    if (!exact_keyword(lines_[index_], Kind::keyword_try)) {
      frontend::unsupported(diagnostics_, line_number, "malformed Matlab try statement");
    }
    ++index_;
    statement.body = parse_block();
    if (index_ >= lines_.size() || !starts_with(lines_[index_], Kind::keyword_catch)) {
      frontend::unsupported(diagnostics_, line_number,
                            "Matlab try statement requires exactly one catch clause");
      expect_end(line_number, "try statement");
      return statement;
    }

    const auto& catch_line = lines_[index_];
    const auto catch_count = token_count(catch_line);
    statement.has_exception_handler = true;
    statement.exception_handler_line = catch_line.source.number;
    if (catch_count == 2U && catch_line.tokens[1].kind == Kind::identifier) {
      statement.name = catch_line.tokens[1].text;
    } else if (catch_count != 1U) {
      frontend::unsupported(diagnostics_, catch_line.source.number,
                            "Matlab catch clause accepts at most one exception identifier");
    }
    ++index_;
    statement.alternative = parse_block();
    expect_end(line_number, "try statement");
    return statement;
  }

  Statement parse_for() {
    const auto& line = lines_[index_];
    const auto count = token_count(line);
    Statement statement;
    statement.kind = StatementKind::range_loop;
    statement.line = line.source.number;
    statement.inclusive_stop = true;
    const bool header = count >= 6 && line.tokens[0].kind == Kind::keyword_for &&
                        line.tokens[1].kind == Kind::identifier &&
                        line.tokens[2].kind == Kind::equal;
    const auto colons =
        header ? top_level_tokens(line, Kind::colon, 3) : std::vector<std::size_t>{};
    if (!header || (colons.size() != 1 && colons.size() != 2)) {
      frontend::unsupported(diagnostics_, line.source.number,
                            "Matlab for range requires start:stop or start:step:stop");
    } else {
      statement.name = line.tokens[1].text;
      const auto first_colon = colons[0];
      const auto second_colon = colons.size() == 2 ? colons[1] : count;
      append_expression(statement, token_slice(line, 3, first_colon), line.source.number);
      append_expression(statement.secondary_expression, statement.has_secondary_expression,
                        token_slice(line, colons.back() + 1, count), line.source.number);
      if (colons.size() == 2) {
        append_expression(statement.tertiary_expression, statement.has_tertiary_expression,
                          token_slice(line, first_colon + 1, second_colon), line.source.number);
      }
    }
    ++index_;
    statement.body = parse_block();
    expect_end(statement.line, "for loop");
    return statement;
  }

  Statement parse_case_clause() {
    const auto& line = lines_[index_];
    const auto count = token_count(line);
    Statement clause;
    clause.kind = StatementKind::case_clause;
    clause.line = line.source.number;
    if (line.tokens[0].kind == Kind::keyword_otherwise) {
      clause.default_case = true;
      if (!exact_keyword(line, Kind::keyword_otherwise)) {
        frontend::unsupported(diagnostics_, line.source.number,
                              "malformed Matlab otherwise clause");
      }
    } else {
      const auto expression = token_slice(line, 1, count);
      if (expression.empty()) {
        frontend::unsupported(diagnostics_, line.source.number,
                              "Matlab case clause requires an expression");
      } else {
        CaseSelector selector;
        append_expression(selector.lower, selector.has_lower, expression, line.source.number);
        clause.case_selectors.push_back(selector);
      }
    }
    ++index_;
    clause.body = parse_block();
    return clause;
  }

  Statement parse_switch() {
    const auto line_number = lines_[index_].source.number;
    Statement statement;
    statement.kind = StatementKind::select_case;
    statement.line = line_number;
    const auto expression = token_slice(lines_[index_], 1, token_count(lines_[index_]));
    if (expression.empty()) {
      frontend::unsupported(diagnostics_, line_number,
                            "Matlab switch statement requires an expression");
    } else {
      append_expression(statement, expression, line_number);
    }
    ++index_;

    bool has_otherwise = false;
    while (index_ < lines_.size() && (starts_with(lines_[index_], Kind::keyword_case) ||
                                      starts_with(lines_[index_], Kind::keyword_otherwise))) {
      const bool otherwise = starts_with(lines_[index_], Kind::keyword_otherwise);
      if (otherwise && has_otherwise) {
        frontend::unsupported(diagnostics_, lines_[index_].source.number,
                              "Matlab switch statement contains more than one otherwise clause");
      } else if (!otherwise && has_otherwise) {
        frontend::unsupported(diagnostics_, lines_[index_].source.number,
                              "Matlab case clause cannot follow otherwise");
      }
      has_otherwise = has_otherwise || otherwise;
      statement.body.push_back(store(parse_case_clause()));
    }
    if (statement.body.empty()) {
      frontend::unsupported(diagnostics_, line_number,
                            "Matlab switch statement requires at least one case or otherwise");
    }
    expect_end(line_number, "switch statement");
    return statement;
  }

  bool is_display_call(const MatlabStatementLine& line, std::size_t& closing) const {
    const auto count = token_count(line);
    if (count < 3 || line.tokens[0].kind != Kind::identifier ||
        line.tokens[1].kind != Kind::left_parenthesis) {
      return false;
    }
    const auto name = lower(line.tokens[0].text);
    if (name != "disp" && name != "display") return false;
    closing = matching_token(line, 1);
    return closing == count - 1;
  }

  std::string command_call_expression(const MatlabCommandSyntax& command) const {
    std::string result = command.callee + '(';
    for (std::size_t index = 0; index < command.arguments.size(); ++index) {
      if (index != 0U) result.push_back(',');
      result += quoted_character_vector(command.arguments[index].value);
    }
    result.push_back(')');
    return result;
  }

  bool parse_command_statement(std::vector<AstNodeId>& statements,
                               const MatlabStatementLine& line) {
    const auto count = token_count(line);
    if (count == 0 || line.tokens[0].kind != Kind::identifier || !line.command.has_value()) {
      return false;
    }
    if (!line.command->terminated) {
      ++index_;
      return true;
    }
    const auto name = lower(line.command->callee);
    if ((name == "disp" || name == "display") && line.command->arguments.size() != 1U) {
      frontend::unsupported(
          diagnostics_, line.source.number,
          "Matlab command-form disp/display requires exactly one character-vector input");
      ++index_;
      return true;
    }
    Statement statement;
    statement.line = line.source.number;
    if (name == "disp" || name == "display") {
      statement.kind = StatementKind::print;
      append_expression(statement, quoted_character_vector(line.command->arguments.front().value),
                        line.source.number);
    } else {
      statement.kind = StatementKind::expression;
      statement.name = "ans";
      statement.implicit_result = semantic::ImplicitResultPolicy::matlab_ans_if_value;
      append_expression(statement, command_call_expression(*line.command), line.source.number);
    }
    statements.push_back(store(std::move(statement)));
    ++index_;
    return true;
  }

  void parse_simple_statement(std::vector<AstNodeId>& statements) {
    const auto& line = lines_[index_];
    const auto count = token_count(line);
    const auto first = count == 0 ? Kind::end : line.tokens[0].kind;
    if (first == Kind::keyword_return) {
      if (count != 1) {
        frontend::unsupported(diagnostics_, line.source.number,
                              "Matlab return statement cannot have operands");
      }
      Statement statement;
      statement.kind = StatementKind::return_statement;
      statement.line = line.source.number;
      if (!function_returns_.empty()) statement.return_names = function_returns_.back();
      statements.push_back(store(std::move(statement)));
      ++index_;
      return;
    }
    if (first == Kind::keyword_break || first == Kind::keyword_continue) {
      if (count != 1) {
        frontend::unsupported(diagnostics_, line.source.number,
                              "Matlab break/continue statement cannot have operands");
      }
      Statement statement;
      statement.kind = first == Kind::keyword_break ? StatementKind::break_statement
                                                    : StatementKind::continue_statement;
      statement.line = line.source.number;
      statements.push_back(store(std::move(statement)));
      ++index_;
      return;
    }
    if (parse_command_statement(statements, line)) return;
    std::size_t display_closing = count;
    if (is_display_call(line, display_closing)) {
      Statement statement;
      statement.kind = StatementKind::print;
      statement.line = line.source.number;
      const auto arguments =
          std::string_view(line.source.text)
              .substr(line.tokens[1].end, line.tokens[display_closing].begin - line.tokens[1].end);
      append_expression(statement, arguments, line.source.number);
      statements.push_back(store(std::move(statement)));
      ++index_;
      return;
    }

    const auto equals = top_level_tokens(line, Kind::equal);
    if (!equals.empty()) {
      if (equals.size() != 1 || equals[0] == 0 || equals[0] + 1 >= count) {
        frontend::unsupported(
            diagnostics_, line.source.number,
            "chained, destructuring, or valueless Matlab assignment is not supported");
        ++index_;
        return;
      }
      const auto equal = equals[0];
      Statement statement;
      statement.line = line.source.number;
      if (equal == 1 && line.tokens[0].kind == Kind::identifier) {
        statement.kind = StatementKind::assignment;
        statement.name = line.tokens[0].text;
      } else if (equal >= 4 && line.tokens[0].kind == Kind::left_bracket &&
                 matching_token(line, 0) == equal - 1) {
        statement.kind = StatementKind::multi_assignment;
        if (!parse_identifier_list(line, 1, equal - 1, statement.target_names, true)) {
          frontend::unsupported(diagnostics_, line.source.number,
                                "Matlab multi-output assignment requires an identifier list");
          ++index_;
          return;
        }
      } else if (equal >= 4 && line.tokens[0].kind == Kind::identifier &&
                 line.tokens[1].kind == Kind::left_parenthesis &&
                 matching_token(line, 1) == equal - 1) {
        statement.kind = StatementKind::indexed_assignment;
        statement.name = line.tokens[0].text;
        append_expression(statement.target_expression, statement.has_target_expression,
                          token_slice(line, 0, equal), line.source.number);
      } else {
        frontend::unsupported(
            diagnostics_, line.source.number,
            "Matlab assignment target must be a name, identifier list, or indexed name");
        ++index_;
        return;
      }
      append_expression(statement, token_slice(line, equal + 1, count), line.source.number);
      statements.push_back(store(std::move(statement)));
      ++index_;
      return;
    }

    if (first == Kind::unsupported_keyword || first == Kind::keyword_catch ||
        first == Kind::keyword_arguments || first == Kind::keyword_function ||
        first == Kind::keyword_if || first == Kind::keyword_while || first == Kind::keyword_for ||
        first == Kind::keyword_switch) {
      frontend::unsupported(diagnostics_, line.source.number,
                            "unsupported Matlab statement in the current language subset: " +
                                (count == 0 ? std::string{} : line.tokens[0].text));
      ++index_;
      return;
    }
    Statement statement;
    statement.kind = StatementKind::expression;
    statement.line = line.source.number;
    append_expression(statement, line.source.text, line.source.number);
    statements.push_back(store(std::move(statement)));
    ++index_;
  }

  std::vector<AstNodeId> parse_block() {
    std::vector<AstNodeId> statements;
    while (index_ < lines_.size()) {
      const auto& line = lines_[index_];
      if (is_terminator(line)) break;
      const auto first = token_count(line) == 0 ? Kind::end : line.tokens[0].kind;
      switch (first) {
        case Kind::keyword_function: statements.push_back(store(parse_function())); break;
        case Kind::keyword_if: statements.push_back(store(parse_if())); break;
        case Kind::keyword_try: statements.push_back(store(parse_try())); break;
        case Kind::keyword_while: statements.push_back(store(parse_while())); break;
        case Kind::keyword_for: statements.push_back(store(parse_for())); break;
        case Kind::keyword_switch: statements.push_back(store(parse_switch())); break;
        default: parse_simple_statement(statements); break;
      }
    }
    return statements;
  }

  std::vector<MatlabStatementLine> lines_;
  std::vector<Diagnostic> diagnostics_;
  [[maybe_unused]] LanguageVersion version_;
  std::size_t index_{0};
  std::vector<std::vector<std::string>> function_returns_;
  FrontendAstBuilder<matlab::ast::LanguageTag> builder_;
};

}  // namespace

matlab::ast::ParseResult parse_matlab_statements(std::vector<MatlabStatementLine> lines,
                                                 std::vector<Diagnostic> diagnostics,
                                                 const LanguageVersion version,
                                                 std::pmr::memory_resource* resource) {
  return Parser{std::move(lines), std::move(diagnostics), version, resource}.parse();
}

}  // namespace mpf::detail
