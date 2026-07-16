#include "python_statement_parser.hpp"

#include <cstddef>
#include <iterator>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "common.hpp"

namespace mpf::detail {
namespace {

using Kind = PythonStatementTokenKind;

std::size_t token_count(const PythonStatementLine& line) noexcept {
  return line.tokens.empty() ? 0 : line.tokens.size() - 1;
}

std::string token_slice(const PythonStatementLine& line, const std::size_t first,
                        const std::size_t last) {
  if (first >= last || last > token_count(line)) return {};
  const auto begin = line.tokens[first].begin;
  const auto end = line.tokens[last - 1].end;
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

std::size_t matching_token(const PythonStatementLine& line, const std::size_t opening) noexcept {
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

const Expression* indexed_base(const Expression& expression) noexcept {
  auto current = &expression;
  while (current->kind == ExpressionKind::index && !current->children.empty()) {
    current = &current->children.front();
  }
  return current;
}

class Parser final {
 public:
  Parser(std::vector<PythonStatementLine> lines, std::vector<Diagnostic> diagnostics)
      : lines_(std::move(lines)), diagnostics_(std::move(diagnostics)) {}

  ParseResult parse() {
    ParseResult result;
    result.program.language = SourceLanguage::python;
    result.program.statements = parse_block(0);
    while (index_ < lines_.size()) {
      const auto orphan_indent = lines_[index_].source.indent;
      frontend::unsupported(diagnostics_, lines_[index_].source.number,
                            "unexpected Python elif/else clause");
      ++index_;
      while (index_ < lines_.size() && lines_[index_].source.indent > orphan_indent) ++index_;
      auto recovered = parse_block(0);
      result.program.statements.insert(result.program.statements.end(),
                                       std::make_move_iterator(recovered.begin()),
                                       std::make_move_iterator(recovered.end()));
    }
    result.diagnostics = std::move(diagnostics_);
    return result;
  }

 private:
  bool starts_with(const PythonStatementLine& line, const Kind kind) const noexcept {
    return token_count(line) != 0 && line.tokens.front().kind == kind;
  }

  bool exact_clause(const PythonStatementLine& line, const Kind kind) const noexcept {
    return token_count(line) == 2 && line.tokens[0].kind == kind &&
           line.tokens[1].kind == Kind::colon;
  }

  std::size_t next_body_indent(const std::size_t parent_indent) const noexcept {
    return index_ < lines_.size() && lines_[index_].source.indent > parent_indent
               ? lines_[index_].source.indent
               : parent_indent;
  }

  void parse_body(Statement& statement, const std::size_t parent_indent,
                  const std::string_view label) {
    const auto body_indent = next_body_indent(parent_indent);
    if (body_indent == parent_indent) {
      frontend::unsupported(diagnostics_, statement.line,
                            "Python " + std::string(label) + " requires an indented body");
      return;
    }
    statement.body = parse_block(body_indent);
  }

  bool append_header_expression(Statement& statement, const PythonStatementLine& line,
                                const std::string_view label) {
    const auto count = token_count(line);
    if (count < 3 || line.tokens[count - 1].kind != Kind::colon) {
      frontend::unsupported(diagnostics_, line.source.number,
                            "malformed Python " + std::string(label));
      return false;
    }
    const auto expression = token_slice(line, 1, count - 1);
    if (expression.empty()) {
      frontend::unsupported(diagnostics_, line.source.number,
                            "Python " + std::string(label) + " requires a condition");
      return false;
    }
    frontend::append_expression(statement, expression, SourceLanguage::python, line.source.number,
                                diagnostics_);
    return statement.has_expression;
  }

  Statement parse_conditional(const Kind keyword, const std::size_t parent_indent,
                              const std::string_view label) {
    const auto& line = lines_[index_];
    Statement statement;
    statement.kind = StatementKind::if_statement;
    statement.line = line.source.number;
    if (!starts_with(line, keyword)) {
      frontend::unsupported(diagnostics_, line.source.number,
                            "malformed Python " + std::string(label));
    } else {
      static_cast<void>(append_header_expression(statement, line, label));
    }
    ++index_;
    parse_body(statement, parent_indent, label);
    parse_if_alternative(statement, parent_indent);
    return statement;
  }

  void parse_if_alternative(Statement& statement, const std::size_t indent) {
    if (index_ >= lines_.size() || lines_[index_].source.indent != indent) return;
    const auto& line = lines_[index_];
    if (starts_with(line, Kind::keyword_else)) {
      const auto else_line = line.source.number;
      if (!exact_clause(line, Kind::keyword_else)) {
        frontend::unsupported(diagnostics_, else_line, "malformed Python else clause");
      }
      ++index_;
      const auto body_indent = next_body_indent(indent);
      if (body_indent == indent) {
        frontend::unsupported(diagnostics_, else_line,
                              "Python else clause requires an indented body");
      } else {
        statement.alternative = parse_block(body_indent);
      }
    } else if (starts_with(line, Kind::keyword_elif)) {
      statement.alternative.push_back(parse_conditional(Kind::keyword_elif, indent, "elif clause"));
    }
  }

  void parse_loop_else(Statement& statement, const std::size_t indent) {
    if (index_ >= lines_.size() || lines_[index_].source.indent != indent ||
        !starts_with(lines_[index_], Kind::keyword_else)) {
      return;
    }
    const auto line = lines_[index_].source.number;
    if (!exact_clause(lines_[index_], Kind::keyword_else)) {
      frontend::unsupported(diagnostics_, line, "malformed Python loop else clause");
    }
    ++index_;
    const auto body_indent = next_body_indent(indent);
    if (body_indent == indent) {
      frontend::unsupported(diagnostics_, line,
                            "Python loop else clause requires an indented body");
    } else {
      statement.alternative = parse_block(body_indent);
    }
  }

  std::vector<std::size_t> top_level_tokens(const PythonStatementLine& line, const Kind wanted,
                                            const std::size_t first, const std::size_t last) const {
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

  bool parse_function_parameters(Statement& statement, const PythonStatementLine& line,
                                 const std::size_t first, const std::size_t last) {
    if (first == last) return true;
    const auto commas = top_level_tokens(line, Kind::comma, first, last);
    std::size_t begin = first;
    bool valid = true;
    bool saw_slash = false;
    bool saw_star = false;
    bool positional_default = false;
    std::size_t keyword_only_parameters = 0;
    std::unordered_set<std::string> names;
    for (std::size_t part = 0; part <= commas.size(); ++part) {
      const auto end = part < commas.size() ? commas[part] : last;
      if (begin == end) {
        if (!(part == commas.size() && begin == last)) valid = false;
        begin = end + 1;
        continue;
      }
      if (end == begin + 1 && line.tokens[begin].kind == Kind::slash) {
        if (saw_slash || saw_star || statement.parameters.empty()) {
          valid = false;
        } else {
          saw_slash = true;
          for (auto& kind : statement.parameter_kinds) {
            kind = ParameterKind::positional_only;
          }
        }
        begin = end + 1;
        continue;
      }
      if (end == begin + 1 && line.tokens[begin].kind == Kind::star) {
        if (saw_star) valid = false;
        saw_star = true;
        begin = end + 1;
        continue;
      }
      if (line.tokens[begin].kind != Kind::identifier) {
        valid = false;
        begin = end + 1;
        continue;
      }
      const auto equals = top_level_tokens(line, Kind::equal, begin, end);
      const bool has_default = equals.size() == 1;
      if (equals.size() > 1 ||
          (!equals.empty() && (equals.front() != begin + 1 || equals.front() + 1 >= end)) ||
          (equals.empty() && end != begin + 1)) {
        valid = false;
        begin = end + 1;
        continue;
      }
      const auto& name = line.tokens[begin].text;
      if (!names.insert(name).second) valid = false;
      const auto kind =
          saw_star ? ParameterKind::keyword_only : ParameterKind::positional_or_keyword;
      if (kind == ParameterKind::keyword_only) {
        ++keyword_only_parameters;
      } else if (has_default) {
        positional_default = true;
      } else if (positional_default) {
        valid = false;
      }
      statement.parameters.push_back(name);
      statement.parameter_kinds.push_back(kind);
      Expression default_value;
      if (has_default) {
        bool parsed = false;
        frontend::append_expression(default_value, parsed,
                                    token_slice(line, equals.front() + 1, end),
                                    SourceLanguage::python, line.source.number, diagnostics_);
        if (!parsed) valid = false;
      }
      statement.parameter_defaults.push_back(std::move(default_value));
      begin = end + 1;
    }
    if (saw_star && keyword_only_parameters == 0) valid = false;
    if (!valid) {
      frontend::unsupported(
          diagnostics_, line.source.number,
          "malformed Python parameter association or unsupported variadic/annotated parameter");
    }
    return valid;
  }

  Statement parse_function(const std::size_t indent) {
    const auto& line = lines_[index_];
    const auto count = token_count(line);
    Statement statement;
    statement.kind = StatementKind::function;
    statement.line = line.source.number;
    bool valid = count >= 5 && line.tokens[0].kind == Kind::keyword_def &&
                 line.tokens[1].kind == Kind::identifier &&
                 line.tokens[2].kind == Kind::left_parenthesis &&
                 line.tokens[count - 1].kind == Kind::colon;
    std::size_t closing = count;
    if (valid) {
      closing = matching_token(line, 2);
      valid = closing < count - 1;
    }
    if (!valid) {
      frontend::unsupported(diagnostics_, line.source.number,
                            "malformed Python function signature");
    } else {
      statement.name = line.tokens[1].text;
      static_cast<void>(parse_function_parameters(statement, line, 3, closing));
      const auto annotation_begin = closing + 1;
      if (annotation_begin < count - 1 && (line.tokens[annotation_begin].kind != Kind::arrow ||
                                           annotation_begin + 1 >= count - 1)) {
        frontend::unsupported(diagnostics_, line.source.number,
                              "malformed Python function return annotation");
      }
    }
    ++index_;
    parse_body(statement, indent, "function");
    return statement;
  }

  Statement parse_while(const std::size_t indent) {
    const auto& line = lines_[index_];
    Statement statement;
    statement.kind = StatementKind::while_loop;
    statement.line = line.source.number;
    static_cast<void>(append_header_expression(statement, line, "while statement"));
    ++index_;
    parse_body(statement, indent, "while statement");
    parse_loop_else(statement, indent);
    return statement;
  }

  Statement parse_range(const std::size_t indent) {
    const auto& line = lines_[index_];
    const auto count = token_count(line);
    Statement statement;
    statement.kind = StatementKind::range_loop;
    statement.line = line.source.number;
    bool valid =
        count >= 8 && line.tokens[0].kind == Kind::keyword_for &&
        line.tokens[1].kind == Kind::identifier && line.tokens[2].kind == Kind::keyword_in &&
        line.tokens[3].kind == Kind::identifier && line.tokens[3].text == "range" &&
        line.tokens[4].kind == Kind::left_parenthesis && line.tokens[count - 1].kind == Kind::colon;
    std::size_t closing = count;
    if (valid) {
      closing = matching_token(line, 4);
      valid = closing == count - 2;
    }
    if (!valid) {
      frontend::unsupported(diagnostics_, line.source.number,
                            "Python for loop currently requires 'for name in range(...):'");
    } else {
      statement.name = line.tokens[1].text;
      const auto arguments_text =
          std::string_view(line.source.text)
              .substr(line.tokens[4].end, line.tokens[closing].begin - line.tokens[4].end);
      const auto arguments = frontend::comma_list(arguments_text);
      if (arguments.empty() || arguments.size() > 3) {
        frontend::unsupported(diagnostics_, line.source.number,
                              "Python range loop requires one, two, or three scalar arguments");
      } else {
        const auto start = arguments.size() == 1 ? std::string("0") : arguments[0];
        const auto stop = arguments.size() == 1 ? arguments[0] : arguments[1];
        frontend::append_expression(statement, start, SourceLanguage::python, line.source.number,
                                    diagnostics_);
        frontend::append_expression(statement.secondary_expression,
                                    statement.has_secondary_expression, stop,
                                    SourceLanguage::python, line.source.number, diagnostics_);
        if (arguments.size() == 3) {
          frontend::append_expression(statement.tertiary_expression,
                                      statement.has_tertiary_expression, arguments[2],
                                      SourceLanguage::python, line.source.number, diagnostics_);
        }
      }
    }
    ++index_;
    parse_body(statement, indent, "for statement");
    parse_loop_else(statement, indent);
    return statement;
  }

  bool is_print_call(const PythonStatementLine& line, std::size_t& closing) const noexcept {
    const auto count = token_count(line);
    if (count < 3 || line.tokens[0].kind != Kind::identifier || line.tokens[0].text != "print" ||
        line.tokens[1].kind != Kind::left_parenthesis) {
      return false;
    }
    closing = matching_token(line, 1);
    return closing == count - 1;
  }

  std::vector<std::size_t> top_level_equals(const PythonStatementLine& line) const {
    std::vector<std::size_t> result;
    std::vector<Kind> stack;
    for (std::size_t token = 0; token < token_count(line); ++token) {
      const auto kind = line.tokens[token].kind;
      if (is_opening(kind)) {
        stack.push_back(kind);
      } else if (is_closing(kind)) {
        if (!stack.empty() && matches(stack.back(), kind)) stack.pop_back();
      } else if (kind == Kind::equal && stack.empty()) {
        result.push_back(token);
      }
    }
    return result;
  }

  bool parse_assignment_pattern(const PythonStatementLine& line, const std::size_t first,
                                const std::size_t last, AssignmentPattern& pattern) const {
    if (first >= last) return false;
    if (last == first + 1 && line.tokens[first].kind == Kind::identifier) {
      pattern.kind = AssignmentPatternKind::name;
      pattern.location = line.tokens[first].location;
      pattern.name = line.tokens[first].text;
      return true;
    }
    if (last == first + 2 && line.tokens[first].kind == Kind::star &&
        line.tokens[first + 1].kind == Kind::identifier) {
      pattern.kind = AssignmentPatternKind::starred_name;
      pattern.location = line.tokens[first].location;
      pattern.name = line.tokens[first + 1].text;
      return true;
    }

    auto begin = first;
    auto end = last;
    bool bracket_pattern = false;
    bool parenthesized = false;
    if ((line.tokens[first].kind == Kind::left_parenthesis ||
         line.tokens[first].kind == Kind::left_bracket) &&
        matching_token(line, first) == last - 1) {
      parenthesized = line.tokens[first].kind == Kind::left_parenthesis;
      bracket_pattern = line.tokens[first].kind == Kind::left_bracket;
      ++begin;
      --end;
    }
    if (begin >= end) return false;

    const auto commas = top_level_tokens(line, Kind::comma, begin, end);
    if (commas.empty()) {
      if (parenthesized) return parse_assignment_pattern(line, begin, end, pattern);
      if (!bracket_pattern) return false;
    }

    pattern.kind = AssignmentPatternKind::sequence;
    pattern.location = line.tokens[first].location;
    std::size_t part_begin = begin;
    std::size_t starred = 0;
    for (std::size_t part = 0; part <= commas.size(); ++part) {
      const auto part_end = part < commas.size() ? commas[part] : end;
      if (part_begin == part_end) {
        if (part == commas.size() && part_begin == end && !pattern.children.empty()) break;
        return false;
      }
      AssignmentPattern child;
      if (!parse_assignment_pattern(line, part_begin, part_end, child)) return false;
      if (child.kind == AssignmentPatternKind::starred_name) ++starred;
      pattern.children.push_back(std::move(child));
      part_begin = part_end + 1;
    }
    return !pattern.children.empty() && starred <= 1;
  }

  void parse_simple_statement(std::vector<Statement>& statements) {
    const auto& line = lines_[index_];
    const auto count = token_count(line);
    const auto first = count == 0 ? Kind::end : line.tokens[0].kind;
    if (first == Kind::keyword_break || first == Kind::keyword_continue) {
      if (count != 1) {
        frontend::unsupported(diagnostics_, line.source.number,
                              "Python break/continue statement cannot have operands");
      }
      Statement statement;
      statement.kind = first == Kind::keyword_break ? StatementKind::break_statement
                                                    : StatementKind::continue_statement;
      statement.line = line.source.number;
      statements.push_back(std::move(statement));
      ++index_;
      return;
    }
    if (first == Kind::keyword_return) {
      Statement statement;
      statement.kind = StatementKind::return_statement;
      statement.line = line.source.number;
      if (count > 1) {
        frontend::append_expression(statement, token_slice(line, 1, count), SourceLanguage::python,
                                    line.source.number, diagnostics_);
      }
      statements.push_back(std::move(statement));
      ++index_;
      return;
    }

    std::size_t print_closing = count;
    if (is_print_call(line, print_closing)) {
      Statement statement;
      statement.kind = StatementKind::print;
      statement.line = line.source.number;
      const auto arguments =
          std::string_view(line.source.text)
              .substr(line.tokens[1].end, line.tokens[print_closing].begin - line.tokens[1].end);
      frontend::append_expression(statement, arguments, SourceLanguage::python, line.source.number,
                                  diagnostics_);
      statements.push_back(std::move(statement));
      ++index_;
      return;
    }

    const auto equals = top_level_equals(line);
    if (!equals.empty()) {
      if (equals.size() != 1 || equals.front() == 0 || equals.front() + 1 >= count) {
        frontend::unsupported(diagnostics_, line.source.number,
                              "chained or valueless Python assignment is not supported");
        ++index_;
        return;
      }
      const auto equal = equals.front();
      Statement statement;
      statement.line = line.source.number;
      const bool unpack_candidate = !top_level_tokens(line, Kind::comma, 0, equal).empty() ||
                                    ((line.tokens[0].kind == Kind::left_parenthesis ||
                                      line.tokens[0].kind == Kind::left_bracket) &&
                                     matching_token(line, 0) == equal - 1);
      if (equal == 1 && line.tokens[0].kind == Kind::identifier) {
        statement.kind = StatementKind::assignment;
        statement.name = line.tokens[0].text;
      } else if (equal == 3 && line.tokens[0].kind == Kind::left_parenthesis &&
                 line.tokens[1].kind == Kind::identifier &&
                 line.tokens[2].kind == Kind::right_parenthesis) {
        statement.kind = StatementKind::assignment;
        statement.name = line.tokens[1].text;
      } else if (parse_assignment_pattern(line, 0, equal, statement.target_pattern) &&
                 statement.target_pattern.kind == AssignmentPatternKind::sequence) {
        statement.kind = StatementKind::multi_assignment;
        statement.has_target_pattern = true;
        collect_assignment_names(statement.target_pattern, statement.target_names);
      } else if (unpack_candidate) {
        frontend::unsupported(
            diagnostics_, line.source.number,
            "malformed Python assignment pattern or more than one starred target");
        ++index_;
        return;
      } else {
        statement.kind = StatementKind::indexed_assignment;
        frontend::append_expression(statement.target_expression, statement.has_target_expression,
                                    token_slice(line, 0, equal), SourceLanguage::python,
                                    line.source.number, diagnostics_);
        const auto* base = indexed_base(statement.target_expression);
        if (!statement.has_target_expression ||
            statement.target_expression.kind != ExpressionKind::index || base == nullptr ||
            base->kind != ExpressionKind::identifier) {
          frontend::unsupported(diagnostics_, line.source.number,
                                "Python assignment target must be a name, fixed name unpacking "
                                "pattern, or indexed name");
        } else {
          statement.name = base->value;
        }
      }
      frontend::append_expression(statement, token_slice(line, equal + 1, count),
                                  SourceLanguage::python, line.source.number, diagnostics_);
      statements.push_back(std::move(statement));
      ++index_;
      return;
    }

    if (first == Kind::unsupported_keyword || first == Kind::keyword_for ||
        first == Kind::keyword_while || first == Kind::keyword_def || first == Kind::keyword_if ||
        first == Kind::keyword_return ||
        (!line.source.text.empty() && line.source.text.front() == '@')) {
      const auto keyword = count == 0 ? std::string{} : line.tokens[0].text;
      frontend::unsupported(
          diagnostics_, line.source.number,
          "unsupported Python statement in the current language subset: " + keyword);
      ++index_;
      return;
    }

    Statement statement;
    statement.kind = StatementKind::expression;
    statement.line = line.source.number;
    frontend::append_expression(statement, line.source.text, SourceLanguage::python,
                                line.source.number, diagnostics_);
    statements.push_back(std::move(statement));
    ++index_;
  }

  std::vector<Statement> parse_block(const std::size_t indent) {
    std::vector<Statement> statements;
    while (index_ < lines_.size()) {
      const auto& line = lines_[index_];
      if (line.source.indent < indent) break;
      if (line.source.indent > indent) {
        frontend::unsupported(diagnostics_, line.source.number, "unexpected Python indentation");
        ++index_;
        continue;
      }
      if (starts_with(line, Kind::keyword_else) || starts_with(line, Kind::keyword_elif)) break;

      const auto first = token_count(line) == 0 ? Kind::end : line.tokens.front().kind;
      switch (first) {
        case Kind::keyword_def: statements.push_back(parse_function(indent)); break;
        case Kind::keyword_if:
          statements.push_back(parse_conditional(Kind::keyword_if, indent, "if statement"));
          break;
        case Kind::keyword_while: statements.push_back(parse_while(indent)); break;
        case Kind::keyword_for: statements.push_back(parse_range(indent)); break;
        default: parse_simple_statement(statements); break;
      }
    }
    return statements;
  }

  std::vector<PythonStatementLine> lines_;
  std::vector<Diagnostic> diagnostics_;
  std::size_t index_{0};
};

}  // namespace

ParseResult parse_python_statements(std::vector<PythonStatementLine> lines,
                                    std::vector<Diagnostic> diagnostics) {
  return Parser{std::move(lines), std::move(diagnostics)}.parse();
}

}  // namespace mpf::detail
