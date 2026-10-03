#include <algorithm>
#include <string>
#include <string_view>

#include "compiler/expression.hpp"
#include "frontends/matlab/expression_lexer.hpp"
#include "mpf/transpiler.hpp"
#include "test_framework.hpp"

TEST_CASE("Matlab matrix whitespace retains binary signs and tightly attached unary elements") {
  using namespace mpf::detail;
  const auto count = [](const std::string_view source) {
    const auto scanned = lex_matlab_expression(source, 4U, 7U);
    REQUIRE(scanned.diagnostics.empty());
    return std::count_if(scanned.tokens.begin(), scanned.tokens.end(),
                         [](const auto& token) { return token.kind == TokenKind::comma; });
  };
  REQUIRE(count("[x, x + 1, x - 2]") == 2);
  REQUIRE(count("[1 +2 -3]") == 2);
  REQUIRE(count("[1 + 2 3 - 1]") == 1);
  REQUIRE(count("[1+ 2 3- 1]") == 1);
  REQUIRE(count("[(1 + 2), f(3 - 1)]") == 1);
  REQUIRE(count("[1 .* 2, 8 ./ 2, 2 .^ 3]") == 2);
  REQUIRE(count("[1 .5 -.25]") == 2);
  const auto parsed = parse_expression(lex_matlab_expression("[x, x + 1, x - 2]", 1U, 1U),
                                       mpf::SourceLanguage::matlab);
  REQUIRE(parsed.diagnostics.empty());
  REQUIRE(parsed.expression.children.size() == 3U);
  REQUIRE(parsed.expression.children[1].kind == ExpressionKind::binary);
  REQUIRE(parsed.expression.children[2].kind == ExpressionKind::binary);
}

TEST_CASE("Matlab matrix whitespace changes do not insert commas in other source languages") {
  const std::string_view source = "value = [1 + 2, 5 - 1]\nprint(value[0])\n";
  mpf::TranspileOptions options;
  options.language = mpf::SourceLanguage::python;
  for (const auto target : {mpf::TargetLanguage::javascript, mpf::TargetLanguage::cpp}) {
    options.target = target;
    REQUIRE(mpf::Transpiler{}.transpile(std::string(source), options).success());
  }
}
