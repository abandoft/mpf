#include "expression.hpp"

#include <optional>
#include <string>
#include <utility>

#include "../lexer/lexer.hpp"

namespace mpf::detail {
namespace {

struct ComparisonToken {
  ComparisonOperator operation{ComparisonOperator::none};
  SourceLocation location{};
  std::size_t width{1};
};

std::optional<ComparisonToken> comparison_token(const Token& current, const Token& next,
                                                const SourceLanguage language) noexcept {
  ComparisonOperator operation{ComparisonOperator::none};
  switch (current.kind) {
    case TokenKind::equal_equal: operation = ComparisonOperator::equal; break;
    case TokenKind::not_equal: operation = ComparisonOperator::not_equal; break;
    case TokenKind::less: operation = ComparisonOperator::less; break;
    case TokenKind::less_equal: operation = ComparisonOperator::less_equal; break;
    case TokenKind::greater: operation = ComparisonOperator::greater; break;
    case TokenKind::greater_equal: operation = ComparisonOperator::greater_equal; break;
    case TokenKind::identity_is:
      if (language != SourceLanguage::python) return std::nullopt;
      return ComparisonToken{next.kind == TokenKind::logical_not ? ComparisonOperator::not_identity
                                                                 : ComparisonOperator::identity,
                             current.location, next.kind == TokenKind::logical_not ? 2U : 1U};
    case TokenKind::membership_in:
      if (language != SourceLanguage::python) return std::nullopt;
      operation = ComparisonOperator::contains;
      break;
    case TokenKind::logical_not:
      if (language != SourceLanguage::python || next.kind != TokenKind::membership_in) {
        return std::nullopt;
      }
      return ComparisonToken{ComparisonOperator::not_contains, current.location, 2U};
    default: return std::nullopt;
  }
  return ComparisonToken{operation, current.location, 1U};
}

int binding_power(const TokenKind kind) noexcept {
  switch (kind) {
    case TokenKind::logical_or: return 1;
    case TokenKind::logical_and: return 2;
    case TokenKind::plus:
    case TokenKind::minus: return 4;
    case TokenKind::star:
    case TokenKind::slash:
    case TokenKind::backslash:
    case TokenKind::dot_star:
    case TokenKind::dot_slash:
    case TokenKind::dot_backslash:
    case TokenKind::floor_slash:
    case TokenKind::percent: return 5;
    case TokenKind::power:
    case TokenKind::dot_power: return 7;
    default: return -1;
  }
}

std::string canonical_operator(const TokenKind kind, const SourceLanguage language) {
  switch (kind) {
    case TokenKind::plus: return "+";
    case TokenKind::minus: return "-";
    case TokenKind::star: return "*";
    case TokenKind::slash: return "/";
    case TokenKind::backslash: return "\\";
    case TokenKind::dot_star: return ".*";
    case TokenKind::dot_slash: return "./";
    case TokenKind::dot_backslash: return ".\\";
    case TokenKind::dot_power: return ".^";
    case TokenKind::floor_slash: return "//";
    case TokenKind::percent: return "%";
    case TokenKind::power: return language == SourceLanguage::matlab ? "^" : "**";
    case TokenKind::logical_and: return "&&";
    case TokenKind::logical_or: return "||";
    case TokenKind::logical_not: return "!";
    default: return {};
  }
}

BinaryOperator binary_operator(const TokenKind kind) noexcept {
  switch (kind) {
    case TokenKind::plus: return BinaryOperator::add;
    case TokenKind::minus: return BinaryOperator::subtract;
    case TokenKind::star: return BinaryOperator::multiply;
    case TokenKind::slash: return BinaryOperator::divide;
    case TokenKind::backslash: return BinaryOperator::left_divide;
    case TokenKind::floor_slash: return BinaryOperator::floor_divide;
    case TokenKind::percent: return BinaryOperator::remainder;
    case TokenKind::power: return BinaryOperator::power;
    case TokenKind::logical_and: return BinaryOperator::logical_and;
    case TokenKind::logical_or: return BinaryOperator::logical_or;
    case TokenKind::dot_star: return BinaryOperator::elementwise_multiply;
    case TokenKind::dot_slash: return BinaryOperator::elementwise_divide;
    case TokenKind::dot_backslash: return BinaryOperator::elementwise_left_divide;
    case TokenKind::dot_power: return BinaryOperator::elementwise_power;
    default: return BinaryOperator::none;
  }
}

class Parser final {
 public:
  Parser(LexerResult lexed, const SourceLanguage language)
      : tokens_(std::move(lexed.tokens)),
        diagnostics_(std::move(lexed.diagnostics)),
        language_(language) {}

  ExpressionParseResult parse() {
    ExpressionParseResult result;
    if (current().kind == TokenKind::end) {
      error(current(), "MPF1010", "expected an expression");
    } else {
      result.expression = parse_conditional();
      if (accept(TokenKind::comma)) {
        Expression tuple;
        tuple.kind = ExpressionKind::tuple;
        tuple.location = result.expression.location;
        tuple.children.push_back(std::move(result.expression));
        while (current().kind != TokenKind::end) {
          tuple.children.push_back(parse_conditional());
          if (!accept(TokenKind::comma)) break;
        }
        result.expression = std::move(tuple);
      }
      if (current().kind != TokenKind::end) {
        error(current(), "MPF1011",
              std::string("unexpected ") + to_string(current().kind) + " after expression");
      }
    }
    result.diagnostics = std::move(diagnostics_);
    return result;
  }

 private:
  const Token& current() const noexcept { return tokens_[index_]; }

  const Token& peek(const std::size_t offset = 1) const noexcept {
    const auto position = index_ + offset;
    return tokens_[position < tokens_.size() ? position : tokens_.size() - 1];
  }

  Token take() {
    auto result = current();
    if (index_ + 1 < tokens_.size()) {
      ++index_;
    }
    return result;
  }

  bool accept(const TokenKind kind) {
    if (current().kind != kind) {
      return false;
    }
    take();
    return true;
  }

  std::optional<ComparisonToken> current_comparison() const noexcept {
    return comparison_token(current(), peek(), language_);
  }

  ComparisonToken take_comparison() {
    const auto comparison = *current_comparison();
    for (std::size_t index = 0; index < comparison.width; ++index) take();
    return comparison;
  }

  void error(const Token& token, std::string code, std::string message) {
    diagnostics_.push_back(
        {DiagnosticSeverity::error, std::move(code), std::move(message), token.location});
  }

  void expect(const TokenKind kind, const char* message) {
    if (!accept(kind)) {
      error(current(), "MPF1012", message);
    }
  }

  Expression parse_conditional() {
    auto true_expression = parse_precedence(0);
    if (language_ != SourceLanguage::python || !accept(TokenKind::conditional_if)) {
      return true_expression;
    }

    const auto location = current().location;
    auto condition = parse_precedence(0);
    if (!accept(TokenKind::conditional_else)) {
      error(current(), "MPF1111", "Python conditional expression requires 'else'");
      return true_expression;
    }
    auto false_expression = parse_conditional();
    Expression expression;
    expression.kind = ExpressionKind::conditional;
    expression.location = location;
    expression.children.push_back(std::move(condition));
    expression.children.push_back(std::move(true_expression));
    expression.children.push_back(std::move(false_expression));
    return expression;
  }

  Expression parse_precedence(const int minimum_power) {
    auto left = parse_prefix();
    while (left.valid()) {
      if (current().kind == TokenKind::left_parenthesis) {
        left = parse_call(std::move(left));
        continue;
      }
      if (current().kind == TokenKind::dot) {
        const auto dot = take();
        if (current().kind != TokenKind::identifier) {
          error(current(), "MPF1013", "member access requires an identifier after '.'");
          return left;
        }
        const auto member = take();
        Expression expression;
        expression.kind = ExpressionKind::member;
        expression.location = dot.location;
        expression.value = member.text;
        expression.children.push_back(std::move(left));
        left = std::move(expression);
        continue;
      }
      if (current().kind == TokenKind::left_bracket) {
        const auto opening = take();
        Expression expression;
        expression.kind = ExpressionKind::index;
        expression.location = opening.location;
        expression.index_base = 0;
        expression.allow_negative_index = language_ == SourceLanguage::python;
        expression.children.push_back(std::move(left));
        expression.children.push_back(parse_subscript_component(TokenKind::right_bracket));
        expect(TokenKind::right_bracket, "expected ']' to close index expression");
        left = std::move(expression);
        continue;
      }

      const auto comparison = current_comparison();
      const auto power = comparison.has_value() ? 3 : binding_power(current().kind);
      if (power < minimum_power) {
        break;
      }
      if (comparison.has_value()) {
        const auto first_operator = take_comparison();
        auto right = parse_precedence(power + 1);
        if (language_ == SourceLanguage::python && current_comparison().has_value()) {
          Expression chain;
          chain.kind = ExpressionKind::comparison_chain;
          chain.location = first_operator.location;
          chain.comparisons.push_back(first_operator.operation);
          chain.children.push_back(std::move(left));
          chain.children.push_back(std::move(right));
          while (current_comparison().has_value()) {
            const auto operator_token = take_comparison();
            chain.comparisons.push_back(operator_token.operation);
            chain.children.push_back(parse_precedence(power + 1));
          }
          left = std::move(chain);
        } else {
          if (language_ != SourceLanguage::python && left.kind == ExpressionKind::binary &&
              left.comparison != ComparisonOperator::none) {
            error({TokenKind::end, {}, first_operator.location}, "MPF1110",
                  "chained comparisons require single-evaluation lowering and are not yet "
                  "supported");
          }
          Expression expression;
          expression.kind = ExpressionKind::binary;
          expression.location = first_operator.location;
          expression.comparison = first_operator.operation;
          expression.children.push_back(std::move(left));
          expression.children.push_back(std::move(right));
          left = std::move(expression);
        }
        continue;
      }
      const auto operator_token = take();
      const auto right_associative =
          operator_token.kind == TokenKind::power || operator_token.kind == TokenKind::dot_power;
      const auto right_power = right_associative ? power : power + 1;
      auto right = parse_precedence(right_power);
      Expression expression;
      expression.kind = ExpressionKind::binary;
      expression.location = operator_token.location;
      expression.value = canonical_operator(operator_token.kind, language_);
      expression.operation = binary_operator(operator_token.kind);
      expression.children.push_back(std::move(left));
      expression.children.push_back(std::move(right));
      left = std::move(expression);
    }
    return left;
  }

  Expression parse_prefix() {
    const auto token = take();
    Expression expression;
    expression.location = token.location;
    switch (token.kind) {
      case TokenKind::identifier:
        expression.kind = ExpressionKind::identifier;
        expression.value = token.text;
        return expression;
      case TokenKind::number:
        expression.kind = ExpressionKind::number_literal;
        expression.value = token.text;
        return expression;
      case TokenKind::string_literal:
        expression.kind = ExpressionKind::string_literal;
        expression.value = token.text;
        return expression;
      case TokenKind::true_keyword:
      case TokenKind::false_keyword:
        expression.kind = ExpressionKind::boolean_literal;
        expression.value = token.kind == TokenKind::true_keyword ? "true" : "false";
        return expression;
      case TokenKind::null_keyword:
        expression.kind = ExpressionKind::null_literal;
        expression.value = "null";
        return expression;
      case TokenKind::plus:
      case TokenKind::minus:
      case TokenKind::logical_not:
        expression.kind = ExpressionKind::unary;
        expression.value = canonical_operator(token.kind, language_);
        expression.children.push_back(parse_precedence(
            language_ == SourceLanguage::python && token.kind == TokenKind::logical_not ? 3 : 6));
        return expression;
      case TokenKind::left_parenthesis:
        expression = parse_conditional();
        if (accept(TokenKind::comma)) {
          Expression tuple;
          tuple.kind = ExpressionKind::tuple;
          tuple.location = token.location;
          tuple.children.push_back(std::move(expression));
          while (current().kind != TokenKind::right_parenthesis &&
                 current().kind != TokenKind::end) {
            tuple.children.push_back(parse_conditional());
            if (!accept(TokenKind::comma)) break;
          }
          expression = std::move(tuple);
        }
        expect(TokenKind::right_parenthesis, "expected ')' to close parenthesized expression");
        return expression;
      case TokenKind::left_bracket:
        expression.kind = ExpressionKind::list;
        if (!accept(TokenKind::right_bracket)) {
          Expression row;
          row.kind = ExpressionKind::list;
          row.location = token.location;
          bool matrix = false;
          while (current().kind != TokenKind::right_bracket && current().kind != TokenKind::end) {
            row.children.push_back(parse_conditional());
            if (accept(TokenKind::comma)) continue;
            if (accept(TokenKind::semicolon)) {
              matrix = true;
              expression.children.push_back(std::move(row));
              row = Expression{};
              row.kind = ExpressionKind::list;
              row.location = token.location;
              continue;
            }
            break;
          }
          if (matrix) {
            if (!row.children.empty()) expression.children.push_back(std::move(row));
          } else {
            expression.children = std::move(row.children);
          }
          expect(TokenKind::right_bracket, "expected ']' to close list expression");
        }
        return expression;
      default:
        error(token, "MPF1014",
              std::string("expected an expression, found ") + to_string(token.kind));
        return expression;
    }
  }

  Expression parse_call(Expression callee) {
    const auto opening = take();
    Expression expression;
    expression.kind = ExpressionKind::call;
    expression.location = opening.location;
    expression.children.push_back(std::move(callee));
    if (!accept(TokenKind::right_parenthesis)) {
      bool saw_keyword = false;
      do {
        std::string argument_name;
        if ((language_ == SourceLanguage::fortran || language_ == SourceLanguage::python) &&
            current().kind == TokenKind::identifier && peek().kind == TokenKind::equal) {
          argument_name = take().text;
          take();
          saw_keyword = true;
        } else if (saw_keyword) {
          error(current(), "MPF1015",
                std::string(language_ == SourceLanguage::python ? "Python" : "Fortran") +
                    " positional argument cannot follow a keyword argument");
        }
        expression.children.push_back(parse_subscript_component(TokenKind::right_parenthesis));
        expression.argument_names.push_back(std::move(argument_name));
      } while (accept(TokenKind::comma) && current().kind != TokenKind::right_parenthesis);
      expect(TokenKind::right_parenthesis, "expected ')' to close function call");
    }
    return expression;
  }

  Expression missing_slice_bound(const SourceLocation location) const {
    Expression expression;
    expression.location = location;
    return expression;
  }

  Expression parse_subscript_component(const TokenKind terminator) {
    const auto location = current().location;
    Expression first =
        current().kind == TokenKind::colon ? missing_slice_bound(location) : parse_conditional();
    if (!accept(TokenKind::colon)) return first;

    Expression middle = current().kind == TokenKind::colon || current().kind == TokenKind::comma ||
                                current().kind == terminator || current().kind == TokenKind::end
                            ? missing_slice_bound(current().location)
                            : parse_conditional();
    const bool has_second_colon = accept(TokenKind::colon);
    Expression last = has_second_colon && current().kind != TokenKind::comma &&
                              current().kind != terminator && current().kind != TokenKind::end
                          ? parse_conditional()
                          : missing_slice_bound(current().location);

    Expression slice;
    slice.kind = ExpressionKind::slice;
    slice.location = location;
    slice.index_base = language_ == SourceLanguage::python ? 0U : 1U;
    slice.allow_negative_index = language_ == SourceLanguage::python;
    slice.slice_stop_inclusive = language_ != SourceLanguage::python;
    slice.children.push_back(std::move(first));
    if (language_ == SourceLanguage::matlab && has_second_colon) {
      slice.children.push_back(std::move(last));
      slice.children.push_back(std::move(middle));
    } else {
      slice.children.push_back(std::move(middle));
      slice.children.push_back(std::move(last));
    }
    return slice;
  }

  std::vector<Token> tokens_;
  std::vector<Diagnostic> diagnostics_;
  std::size_t index_{0};
  SourceLanguage language_{SourceLanguage::automatic};
};

}  // namespace

ExpressionParseResult parse_expression(const std::string_view expression,
                                       const SourceLanguage language, const std::size_t line,
                                       const std::size_t column) {
  return Parser(lex_expression(expression, language, line, column), language).parse();
}

}  // namespace mpf::detail
