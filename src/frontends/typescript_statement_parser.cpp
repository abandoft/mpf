#include "typescript_statement_parser.hpp"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "common.hpp"
#include "frontend_ast_builder.hpp"

namespace mpf::detail {
namespace {

using Kind = TypeScriptStatementTokenKind;
using Statement = typescript::ast::Statement;

struct TypeInfo {
  ValueType type{ValueType::unknown};
  ValueType element_type{ValueType::unknown};
  bool valid{true};
};

bool opening(const Kind kind) noexcept {
  return kind == Kind::left_parenthesis || kind == Kind::left_bracket || kind == Kind::left_brace;
}

bool closing(const Kind kind) noexcept {
  return kind == Kind::right_parenthesis || kind == Kind::right_bracket ||
         kind == Kind::right_brace;
}

bool matches(const Kind left, const Kind right) noexcept {
  return (left == Kind::left_parenthesis && right == Kind::right_parenthesis) ||
         (left == Kind::left_bracket && right == Kind::right_bracket) ||
         (left == Kind::left_brace && right == Kind::right_brace);
}

bool word_like(const Kind kind) noexcept {
  return kind == Kind::identifier || kind == Kind::number || kind == Kind::string_literal ||
         kind == Kind::unsupported_keyword;
}

class Parser final {
 public:
  Parser(TypeScriptStatementLexResult lexed, const LanguageVersion version,
         std::pmr::memory_resource* resource)
      : tokens_(std::move(lexed.tokens)),
        diagnostics_(std::move(lexed.diagnostics)),
        builder_(SourceLanguage::typescript, resource) {
    (void)version;
    scopes_.emplace_back();
    const_scopes_.emplace_back();
    builder_.reserve(tokens_.size() / 3U + 1U, tokens_.size());
  }

  typescript::ast::ParseResult parse() {
    auto roots = parse_block(false);
    builder_.set_roots(std::move(roots));
    return {std::move(builder_).finish(), std::move(diagnostics_)};
  }

 private:
  const TypeScriptStatementToken& current() const noexcept { return tokens_[index_]; }

  const TypeScriptStatementToken& token(const std::size_t index) const noexcept {
    return tokens_[std::min(index, tokens_.size() - 1U)];
  }

  bool accept(const Kind kind) {
    if (current().kind != kind) return false;
    if (index_ + 1U < tokens_.size()) ++index_;
    return true;
  }

  bool expect(const Kind kind, const char* message) {
    if (accept(kind)) return true;
    diagnose(current(), message);
    return false;
  }

  void diagnose(const TypeScriptStatementToken& where, std::string message,
                std::string code = "MPF1200") {
    diagnostics_.push_back(
        {DiagnosticSeverity::error, std::move(code), std::move(message), where.location});
  }

  void skip_separators() {
    while (current().kind == Kind::newline || current().kind == Kind::semicolon) ++index_;
  }

  std::size_t matching_token(const std::size_t opening_index) const noexcept {
    if (!opening(token(opening_index).kind)) return tokens_.size() - 1U;
    std::vector<Kind> stack;
    for (std::size_t cursor = opening_index; cursor < tokens_.size(); ++cursor) {
      const auto kind = token(cursor).kind;
      if (opening(kind)) {
        stack.push_back(kind);
      } else if (closing(kind)) {
        if (stack.empty() || !matches(stack.back(), kind)) return tokens_.size() - 1U;
        stack.pop_back();
        if (stack.empty()) return cursor;
      }
    }
    return tokens_.size() - 1U;
  }

  std::vector<std::size_t> top_level_tokens(const Kind wanted, const std::size_t first,
                                            const std::size_t last) const {
    std::vector<std::size_t> result;
    std::vector<Kind> stack;
    for (std::size_t cursor = first; cursor < last; ++cursor) {
      const auto kind = token(cursor).kind;
      if (opening(kind)) {
        stack.push_back(kind);
      } else if (closing(kind)) {
        if (!stack.empty() && matches(stack.back(), kind)) stack.pop_back();
      } else if (kind == wanted && stack.empty()) {
        result.push_back(cursor);
      }
    }
    return result;
  }

  std::size_t statement_end(const std::size_t first) const noexcept {
    std::vector<Kind> stack;
    for (std::size_t cursor = first; cursor < tokens_.size(); ++cursor) {
      const auto kind = token(cursor).kind;
      if (opening(kind)) {
        if (kind == Kind::left_brace && stack.empty()) return cursor;
        stack.push_back(kind);
      } else if (closing(kind)) {
        if (kind == Kind::right_brace && stack.empty()) return cursor;
        if (!stack.empty() && matches(stack.back(), kind)) stack.pop_back();
      } else if (stack.empty() &&
                 (kind == Kind::semicolon || kind == Kind::newline || kind == Kind::end)) {
        return cursor;
      }
    }
    return tokens_.size() - 1U;
  }

  std::string expression_text(const std::size_t first, const std::size_t last) const {
    std::string result;
    for (std::size_t cursor = first; cursor < last; ++cursor) {
      if (token(cursor).kind == Kind::newline) continue;
      if (!result.empty() &&
          (word_like(token(cursor - 1U).kind) || word_like(token(cursor).kind))) {
        result.push_back(' ');
      }
      result += token(cursor).text;
    }
    return result;
  }

  AstNodeId parse_expression(const std::size_t first, const std::size_t last) {
    if (first >= last) return {};
    for (std::size_t cursor = first; cursor < last; ++cursor) {
      if (token(cursor).kind == Kind::question || token(cursor).kind == Kind::arrow) {
        diagnose(token(cursor),
                 "TypeScript conditional and arrow expressions are not supported by the current "
                 "grammar slice");
      }
      if (token(cursor).kind == Kind::other &&
          (token(cursor).text == "==" || token(cursor).text == "!=")) {
        diagnose(token(cursor),
                 "TypeScript loose equality is rejected; use === or !== for portable semantics");
      }
      if (token(cursor).kind == Kind::unsupported_keyword) {
        diagnose(token(cursor), "unsupported TypeScript expression keyword: " + token(cursor).text);
      }
    }
    return builder_.parse_expression(expression_text(first, last), SourceLanguage::typescript,
                                     token(first).location.line, diagnostics_);
  }

  TypeInfo parse_type(const std::size_t first, const std::size_t last) {
    TypeInfo result;
    if (first >= last || token(first).kind != Kind::identifier) {
      result.valid = false;
      return result;
    }
    const auto& spelling = token(first).text;
    if (spelling == "number")
      result.type = ValueType::real;
    else if (spelling == "boolean")
      result.type = ValueType::boolean;
    else if (spelling == "string")
      result.type = ValueType::string;
    else if (spelling == "null")
      result.type = ValueType::null_value;
    else if (spelling != "unknown" && spelling != "any")
      result.valid = false;
    if (last == first + 3U && token(first + 1U).kind == Kind::left_bracket &&
        token(first + 2U).kind == Kind::right_bracket) {
      result.element_type = result.type;
      result.type = ValueType::list;
    } else if (last != first + 1U) {
      result.valid = false;
    }
    return result;
  }

  bool declared(const std::string& name) const {
    for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
      if (scope->count(name) != 0U) return true;
    }
    return false;
  }

  bool constant(const std::string& name) const {
    for (std::size_t offset = 0; offset < scopes_.size(); ++offset) {
      const auto index = scopes_.size() - offset - 1U;
      if (scopes_[index].count(name) != 0U) {
        return const_scopes_[index].count(name) != 0U;
      }
    }
    return false;
  }

  void declare_name(const TypeScriptStatementToken& name, const bool is_const) {
    if (!scopes_.back().insert(name.text).second) {
      diagnose(name, "duplicate TypeScript declaration in the same lexical scope: " + name.text);
    }
    if (is_const) const_scopes_.back().insert(name.text);
  }

  void consume_statement_end(const std::size_t end) {
    index_ = end;
    if (current().kind == Kind::semicolon) ++index_;
    while (current().kind == Kind::newline) ++index_;
  }

  AstNodeId store(Statement&& statement) { return builder_.add_statement(std::move(statement)); }

  AstNodeId parse_declaration() {
    const auto keyword = current();
    const bool is_const = keyword.kind == Kind::keyword_const;
    ++index_;
    if (current().kind != Kind::identifier) {
      diagnose(current(), "TypeScript declaration requires an identifier");
      recover_statement();
      return {};
    }
    const auto name_token = current();
    declare_name(name_token, is_const);
    ++index_;
    const auto end = statement_end(index_);
    const auto commas = top_level_tokens(Kind::comma, index_, end);
    if (!commas.empty()) {
      diagnose(token(commas.front()),
               "multiple TypeScript declarators are not supported; use one declaration per "
               "statement");
    }

    std::size_t initializer = end;
    const auto equals = top_level_tokens(Kind::equal, index_, end);
    if (equals.size() > 1U) {
      diagnose(token(equals[1]), "chained TypeScript assignment is not supported");
    } else if (!equals.empty()) {
      initializer = equals.front();
    }

    TypeInfo type;
    if (index_ < initializer && current().kind == Kind::colon) {
      type = parse_type(index_ + 1U, initializer);
      if (!type.valid) {
        diagnose(current(),
                 "unsupported TypeScript type annotation; scalar number/boolean/string, any, "
                 "unknown, and one-dimensional array forms are accepted");
      }
    } else if (index_ < initializer) {
      diagnose(current(), "unexpected token after TypeScript declaration name");
    }
    if (is_const && initializer == end) {
      diagnose(keyword, "TypeScript const declaration requires an initializer");
    }

    Statement statement;
    statement.kind = StatementKind::declaration;
    statement.line = keyword.location.line;
    statement.name = name_token.text;
    statement.declared_type = type.type;
    statement.element_type = type.element_type;
    if (initializer < end) {
      statement.expression = parse_expression(initializer + 1U, end);
      statement.has_expression = statement.expression.valid();
      if (!statement.has_expression) diagnose(token(initializer), "initializer requires a value");
    }
    consume_statement_end(end);
    return store(std::move(statement));
  }

  bool parse_parameter(Statement& function, const std::size_t first, const std::size_t last,
                       bool& saw_default) {
    if (first >= last || token(first).kind != Kind::identifier) return false;
    const auto name = token(first);
    std::size_t cursor = first + 1U;
    const bool optional = cursor < last && token(cursor).kind == Kind::question;
    if (optional) ++cursor;
    std::size_t equals = last;
    const auto defaults = top_level_tokens(Kind::equal, cursor, last);
    if (defaults.size() > 1U) return false;
    if (!defaults.empty()) equals = defaults.front();
    TypeInfo type;
    if (cursor < equals) {
      if (token(cursor).kind != Kind::colon) return false;
      type = parse_type(cursor + 1U, equals);
      if (!type.valid) return false;
    }
    const bool has_default = equals < last;
    if (optional && !has_default) {
      diagnose(name,
               "optional TypeScript parameters without a default require undefined semantics and "
               "are not supported yet");
    }
    if (saw_default && !has_default) {
      diagnose(name, "required TypeScript parameter cannot follow a defaulted parameter");
    }
    saw_default = saw_default || has_default;
    function.parameters.push_back(name.text);
    function.parameter_kinds.push_back(ParameterKind::positional_or_keyword);
    function.parameter_types.push_back(type.type);
    function.parameter_element_types.push_back(type.element_type);
    function.parameter_shapes.emplace_back();
    function.parameter_intents.push_back(ParameterIntent::none);
    function.parameter_optional.push_back(has_default || optional);
    AstNodeId default_value;
    if (has_default) default_value = parse_expression(equals + 1U, last);
    function.parameter_defaults.push_back(default_value);
    return true;
  }

  std::vector<AstNodeId> parse_braced_body(const char* construct) {
    if (!expect(Kind::left_brace, "TypeScript control-flow body requires '{'")) return {};
    scopes_.emplace_back();
    const_scopes_.emplace_back();
    ++control_depth_;
    auto result = parse_block(true);
    --control_depth_;
    const_scopes_.pop_back();
    scopes_.pop_back();
    (void)construct;
    return result;
  }

  AstNodeId parse_function(const bool exported = false) {
    const auto function_token = current();
    ++index_;
    if (control_depth_ != 0U) {
      diagnose(function_token,
               "block-local TypeScript function declarations are not supported by the current "
               "scope model");
    }
    if (function_depth_ != 0U) {
      diagnose(function_token,
               "nested TypeScript function declarations are not supported by the current "
               "function ownership model");
    }
    if (current().kind != Kind::identifier) {
      diagnose(current(), "TypeScript function declaration requires a name");
      recover_statement();
      return {};
    }
    const auto name = current();
    declare_name(name, false);
    ++index_;
    if (current().kind != Kind::left_parenthesis) {
      diagnose(current(), "TypeScript function declaration requires a parameter list");
      recover_statement();
      return {};
    }
    const auto opening = index_;
    const auto closing = matching_token(opening);
    if (closing >= tokens_.size() - 1U || token(closing).kind != Kind::right_parenthesis) {
      diagnose(current(), "unterminated TypeScript function parameter list");
      recover_statement();
      return {};
    }

    Statement statement;
    statement.kind = StatementKind::function;
    statement.line = function_token.location.line;
    statement.name = name.text;
    statement.exported = exported;
    const auto commas = top_level_tokens(Kind::comma, opening + 1U, closing);
    std::size_t begin = opening + 1U;
    bool saw_default = false;
    for (std::size_t part = 0; part <= commas.size(); ++part) {
      const auto end = part < commas.size() ? commas[part] : closing;
      if (begin != end && !parse_parameter(statement, begin, end, saw_default)) {
        diagnose(token(begin), "malformed or unsupported TypeScript function parameter");
      } else if (begin == end && !(begin == closing && commas.empty())) {
        diagnose(token(begin), "empty TypeScript function parameter");
      }
      begin = end + 1U;
    }
    index_ = closing + 1U;
    if (accept(Kind::colon)) {
      const auto type_begin = index_;
      while (current().kind != Kind::left_brace && current().kind != Kind::end &&
             current().kind != Kind::newline) {
        ++index_;
      }
      const auto type = parse_type(type_begin, index_);
      if (!type.valid) diagnose(token(type_begin), "unsupported TypeScript return type annotation");
    }
    while (current().kind == Kind::newline) ++index_;
    if (!expect(Kind::left_brace, "TypeScript function body requires '{'")) {
      recover_statement();
      return store(std::move(statement));
    }
    scopes_.emplace_back();
    const_scopes_.emplace_back();
    for (const auto& parameter : statement.parameters) scopes_.back().insert(parameter);
    ++function_depth_;
    statement.body = parse_block(true);
    --function_depth_;
    const_scopes_.pop_back();
    scopes_.pop_back();
    return store(std::move(statement));
  }

  AstNodeId parse_if() {
    const auto keyword = current();
    ++index_;
    if (!expect(Kind::left_parenthesis, "TypeScript if statement requires '('")) {
      recover_statement();
      return {};
    }
    const auto opening = index_ - 1U;
    const auto closing = matching_token(opening);
    if (closing >= tokens_.size() - 1U || token(closing).kind != Kind::right_parenthesis) {
      diagnose(token(opening), "unterminated TypeScript if condition");
      recover_statement();
      return {};
    }
    Statement statement;
    statement.kind = StatementKind::if_statement;
    statement.line = keyword.location.line;
    statement.expression = parse_expression(opening + 1U, closing);
    statement.has_expression = statement.expression.valid();
    index_ = closing + 1U;
    while (current().kind == Kind::newline) ++index_;
    statement.body = parse_braced_body("if");
    while (current().kind == Kind::newline || current().kind == Kind::semicolon) ++index_;
    if (accept(Kind::keyword_else)) {
      while (current().kind == Kind::newline) ++index_;
      if (current().kind == Kind::keyword_if) {
        statement.alternative.push_back(parse_if());
      } else {
        statement.alternative = parse_braced_body("else");
      }
    }
    return store(std::move(statement));
  }

  AstNodeId parse_while() {
    const auto keyword = current();
    ++index_;
    if (!expect(Kind::left_parenthesis, "TypeScript while statement requires '('")) {
      recover_statement();
      return {};
    }
    const auto opening = index_ - 1U;
    const auto closing = matching_token(opening);
    if (closing >= tokens_.size() - 1U || token(closing).kind != Kind::right_parenthesis) {
      diagnose(token(opening), "unterminated TypeScript while condition");
      recover_statement();
      return {};
    }
    Statement statement;
    statement.kind = StatementKind::while_loop;
    statement.line = keyword.location.line;
    statement.expression = parse_expression(opening + 1U, closing);
    statement.has_expression = statement.expression.valid();
    index_ = closing + 1U;
    while (current().kind == Kind::newline) ++index_;
    statement.body = parse_braced_body("while");
    return store(std::move(statement));
  }

  AstNodeId parse_for() {
    const auto keyword = current();
    ++index_;
    if (!expect(Kind::left_parenthesis, "TypeScript for statement requires '('")) {
      recover_statement();
      return {};
    }
    const auto opening = index_ - 1U;
    const auto closing = matching_token(opening);
    if (closing >= tokens_.size() - 1U || token(closing).kind != Kind::right_parenthesis) {
      diagnose(token(opening), "unterminated TypeScript for header");
      recover_statement();
      return {};
    }
    const auto separators = top_level_tokens(Kind::semicolon, opening + 1U, closing);
    if (separators.size() != 2U) {
      diagnose(token(opening),
               "TypeScript for header requires initializer; condition; update clauses");
      index_ = closing + 1U;
      recover_statement();
      return {};
    }

    Statement statement;
    statement.kind = StatementKind::for_loop;
    statement.line = keyword.location.line;
    statement.retain_last_loop_value = false;
    const auto initializer_begin = opening + 1U;
    const auto initializer_end = separators[0];
    std::size_t cursor = initializer_begin;
    if (cursor >= initializer_end ||
        (token(cursor).kind != Kind::keyword_let && token(cursor).kind != Kind::keyword_const)) {
      diagnose(token(cursor), "TypeScript for initializer requires a lexical let declaration");
    }
    if (cursor < initializer_end && token(cursor).kind == Kind::keyword_const) {
      diagnose(token(cursor), "TypeScript for induction binding must use let, not const");
    }
    if (cursor < initializer_end) ++cursor;
    if (cursor >= initializer_end || token(cursor).kind != Kind::identifier) {
      diagnose(token(cursor), "TypeScript for initializer requires an induction identifier");
    }
    const auto name = token(cursor);
    statement.name = name.text;
    if (cursor < initializer_end) ++cursor;
    const auto equals = top_level_tokens(Kind::equal, cursor, initializer_end);
    if (equals.size() != 1U || equals.front() + 1U >= initializer_end) {
      diagnose(token(cursor), "TypeScript for initializer requires exactly one initial value");
    }
    const auto equal = equals.empty() ? initializer_end : equals.front();
    if (cursor < equal) {
      if (token(cursor).kind != Kind::colon) {
        diagnose(token(cursor), "unexpected token in TypeScript for initializer");
      } else {
        const auto type = parse_type(cursor + 1U, equal);
        if (!type.valid || (type.type != ValueType::real && type.type != ValueType::unknown)) {
          diagnose(token(cursor), "TypeScript for induction binding requires the number type");
        }
        statement.declared_type = type.type;
      }
    }
    if (equal < initializer_end) {
      statement.expression = parse_expression(equal + 1U, initializer_end);
      statement.has_expression = statement.expression.valid();
    }

    scopes_.emplace_back();
    const_scopes_.emplace_back();
    declare_name(name, false);
    const auto condition_begin = separators[0] + 1U;
    const auto condition_end = separators[1];
    statement.secondary_expression = parse_expression(condition_begin, condition_end);
    statement.has_secondary_expression = statement.secondary_expression.valid();
    if (!statement.has_secondary_expression) {
      diagnose(token(condition_begin), "TypeScript for condition requires a boolean expression");
    }

    const auto update_begin = separators[1] + 1U;
    const auto update_end = closing;
    std::string update;
    if (update_begin >= update_end || token(update_begin).kind != Kind::identifier ||
        token(update_begin).text != statement.name) {
      diagnose(token(update_begin),
               "TypeScript for update must assign the induction binding: " + statement.name);
    } else if (update_begin + 2U == update_end && token(update_begin + 1U).kind == Kind::other &&
               (token(update_begin + 1U).text == "++" || token(update_begin + 1U).text == "--")) {
      update = statement.name + (token(update_begin + 1U).text == "++" ? " + 1" : " - 1");
    } else if (update_begin + 2U < update_end && token(update_begin + 1U).kind == Kind::other &&
               (token(update_begin + 1U).text == "+=" || token(update_begin + 1U).text == "-=")) {
      update = statement.name + (token(update_begin + 1U).text == "+=" ? " + (" : " - (") +
               expression_text(update_begin + 2U, update_end) + ')';
    } else if (update_begin + 2U < update_end && token(update_begin + 1U).kind == Kind::equal) {
      update = expression_text(update_begin + 2U, update_end);
    } else {
      diagnose(token(update_begin),
               "unsupported TypeScript for update; use ++, --, +=, -=, or direct assignment");
    }
    if (!update.empty()) {
      statement.tertiary_expression = builder_.parse_expression(
          update, SourceLanguage::typescript, token(update_begin).location.line, diagnostics_);
      statement.has_tertiary_expression = statement.tertiary_expression.valid();
    }

    index_ = closing + 1U;
    while (current().kind == Kind::newline) ++index_;
    statement.body = parse_braced_body("for");
    const_scopes_.pop_back();
    scopes_.pop_back();
    return store(std::move(statement));
  }

  bool console_log(const std::size_t first, const std::size_t last, std::size_t& opening,
                   std::size_t& closing_index) const noexcept {
    if (last < first + 5U || token(first).kind != Kind::identifier ||
        token(first).text != "console" || token(first + 1U).kind != Kind::dot ||
        token(first + 2U).kind != Kind::identifier || token(first + 2U).text != "log" ||
        token(first + 3U).kind != Kind::left_parenthesis) {
      return false;
    }
    opening = first + 3U;
    closing_index = matching_token(opening);
    return closing_index == last - 1U;
  }

  const typescript::ast::Expression* indexed_base(const AstNodeId id) const noexcept {
    const auto* expression = builder_.expression(id);
    while (expression != nullptr && expression->kind == ExpressionKind::index &&
           !expression->children.empty()) {
      expression = builder_.expression(expression->children.front());
    }
    return expression;
  }

  AstNodeId parse_simple_statement() {
    const auto first = index_;
    const auto end = statement_end(first);
    if (end == first) {
      diagnose(current(), "empty or malformed TypeScript statement");
      consume_statement_end(end);
      return {};
    }

    if (current().kind == Kind::keyword_return) {
      Statement statement;
      statement.kind = StatementKind::return_statement;
      statement.line = current().location.line;
      if (function_depth_ == 0U)
        diagnose(current(), "TypeScript return is only valid in a function");
      if (first + 1U < end) {
        statement.expression = parse_expression(first + 1U, end);
        statement.has_expression = statement.expression.valid();
      }
      consume_statement_end(end);
      return store(std::move(statement));
    }
    if (current().kind == Kind::keyword_break || current().kind == Kind::keyword_continue) {
      const auto kind = current().kind;
      if (first + 1U != end) diagnose(current(), "break/continue cannot have an operand");
      Statement statement;
      statement.kind = kind == Kind::keyword_break ? StatementKind::break_statement
                                                   : StatementKind::continue_statement;
      statement.line = current().location.line;
      consume_statement_end(end);
      return store(std::move(statement));
    }

    std::size_t call_opening = end;
    std::size_t call_closing = end;
    if (console_log(first, end, call_opening, call_closing)) {
      Statement statement;
      statement.kind = StatementKind::print;
      statement.line = current().location.line;
      if (call_opening + 1U < call_closing) {
        statement.expression = parse_expression(call_opening + 1U, call_closing);
        statement.has_expression = statement.expression.valid();
      }
      consume_statement_end(end);
      return store(std::move(statement));
    }

    const auto equals = top_level_tokens(Kind::equal, first, end);
    if (!equals.empty()) {
      if (equals.size() != 1U || equals.front() == first || equals.front() + 1U >= end) {
        diagnose(token(equals.front()),
                 "chained or valueless TypeScript assignment is unsupported");
        consume_statement_end(end);
        return {};
      }
      const auto equal = equals.front();
      Statement statement;
      statement.line = current().location.line;
      if (equal == first + 1U && token(first).kind == Kind::identifier) {
        statement.kind = StatementKind::assignment;
        statement.name = token(first).text;
        if (!declared(statement.name)) {
          diagnose(token(first), "assignment to undeclared TypeScript name: " + statement.name);
        } else if (constant(statement.name)) {
          diagnose(token(first), "assignment to TypeScript const binding: " + statement.name);
        }
      } else {
        statement.kind = StatementKind::indexed_assignment;
        statement.target_expression = parse_expression(first, equal);
        statement.has_target_expression = statement.target_expression.valid();
        const auto* base = indexed_base(statement.target_expression);
        const auto* target = builder_.expression(statement.target_expression);
        if (base == nullptr || target == nullptr || base->kind != ExpressionKind::identifier ||
            target->kind != ExpressionKind::index) {
          diagnose(token(first), "TypeScript assignment target must be a declared name or index");
        } else {
          statement.name = base->value;
          if (!declared(statement.name)) {
            diagnose(token(first),
                     "indexed assignment to undeclared TypeScript name: " + statement.name);
          }
        }
      }
      statement.expression = parse_expression(equal + 1U, end);
      statement.has_expression = statement.expression.valid();
      consume_statement_end(end);
      return store(std::move(statement));
    }

    for (std::size_t cursor = first; cursor < end; ++cursor) {
      if (token(cursor).kind == Kind::other &&
          (token(cursor).text.find('=') != std::string::npos || token(cursor).text == "++" ||
           token(cursor).text == "--")) {
        diagnose(token(cursor),
                 "compound assignment and update expressions are not supported by the current "
                 "TypeScript grammar slice");
      }
    }
    Statement statement;
    statement.kind = StatementKind::expression;
    statement.line = current().location.line;
    statement.expression = parse_expression(first, end);
    statement.has_expression = statement.expression.valid();
    consume_statement_end(end);
    return store(std::move(statement));
  }

  AstNodeId parse_statement() {
    bool exported = false;
    if (accept(Kind::keyword_export)) {
      exported = true;
      while (current().kind == Kind::newline) ++index_;
    }
    if (exported && (function_depth_ != 0U || control_depth_ != 0U)) {
      diagnose(current(), "TypeScript export declarations are only valid at module scope");
    }
    if (exported && current().kind != Kind::keyword_function) {
      diagnose(current(),
               "only TypeScript function exports are supported by the current module lowering");
    }
    switch (current().kind) {
      case Kind::keyword_function:
        if (function_depth_ != 0U || control_depth_ != 0U) {
          diagnose(current(), "nested TypeScript function declarations are not yet supported");
        }
        return parse_function(exported);
      case Kind::keyword_let:
      case Kind::keyword_const: return parse_declaration();
      case Kind::keyword_if: return parse_if();
      case Kind::keyword_while: return parse_while();
      case Kind::keyword_for: return parse_for();
      case Kind::unsupported_keyword:
        diagnose(current(), "unsupported TypeScript statement keyword: " + current().text);
        recover_statement();
        return {};
      case Kind::keyword_else:
        diagnose(current(), "orphan TypeScript else clause");
        recover_statement();
        return {};
      default: return parse_simple_statement();
    }
  }

  void recover_statement() {
    std::vector<Kind> stack;
    while (current().kind != Kind::end) {
      const auto kind = current().kind;
      if (stack.empty() && (kind == Kind::semicolon || kind == Kind::newline)) {
        ++index_;
        return;
      }
      if (stack.empty() && kind == Kind::right_brace) return;
      if (opening(kind))
        stack.push_back(kind);
      else if (closing(kind) && !stack.empty() && matches(stack.back(), kind))
        stack.pop_back();
      ++index_;
    }
  }

  std::vector<AstNodeId> parse_block(const bool braced) {
    std::vector<AstNodeId> statements;
    skip_separators();
    while (current().kind != Kind::end) {
      if (current().kind == Kind::right_brace) {
        if (!braced) {
          diagnose(current(), "orphan TypeScript closing brace");
          ++index_;
          continue;
        }
        ++index_;
        return statements;
      }
      const auto before = index_;
      const auto statement = parse_statement();
      if (statement.valid()) statements.push_back(statement);
      if (index_ == before) ++index_;
      skip_separators();
    }
    if (braced) diagnose(current(), "unterminated TypeScript block");
    return statements;
  }

  std::vector<TypeScriptStatementToken> tokens_;
  std::vector<Diagnostic> diagnostics_;
  std::size_t index_{0};
  std::size_t function_depth_{0};
  std::size_t control_depth_{0};
  std::vector<std::unordered_set<std::string>> scopes_;
  std::vector<std::unordered_set<std::string>> const_scopes_;
  FrontendAstBuilder<typescript::ast::LanguageTag> builder_;
};

}  // namespace

typescript::ast::ParseResult parse_typescript_statements(TypeScriptStatementLexResult lexed,
                                                         const LanguageVersion version,
                                                         std::pmr::memory_resource* resource) {
  return Parser{std::move(lexed), version, resource}.parse();
}

}  // namespace mpf::detail
