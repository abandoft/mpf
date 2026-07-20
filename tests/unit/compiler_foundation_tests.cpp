#include <algorithm>
#include <string>
#include <tuple>

#include "compiler/expression.hpp"
#include "compiler/function_graph_generic.hpp"
#include "compiler/numeric_contract.hpp"
#include "frontends/common/registry.hpp"
#include "frontends/fortran/expression_lexer.hpp"
#include "frontends/fortran/source_form.hpp"
#include "frontends/fortran/statement_lexer.hpp"
#include "frontends/matlab/expression_lexer.hpp"
#include "frontends/matlab/logical_source.hpp"
#include "frontends/matlab/statement_lexer.hpp"
#include "frontends/python/expression_lexer.hpp"
#include "frontends/python/logical_source.hpp"
#include "frontends/python/statement_lexer.hpp"
#include "frontends/typescript/expression_lexer.hpp"
#include "frontends/typescript/statement_lexer.hpp"
#include "ir/hir.hpp"
#include "lexer/lexer.hpp"
#include "source/source_manager.hpp"
#include "source/source_text.hpp"
#include "test_framework.hpp"

namespace {

mpf::detail::ExpressionParseResult parse_expression(const std::string_view source,
                                                    const mpf::SourceLanguage language,
                                                    const std::size_t line,
                                                    const std::size_t column = 1U) {
  mpf::detail::ExpressionLexer lexer = nullptr;
  switch (language) {
    case mpf::SourceLanguage::python: lexer = &mpf::detail::lex_python_expression; break;
    case mpf::SourceLanguage::matlab: lexer = &mpf::detail::lex_matlab_expression; break;
    case mpf::SourceLanguage::fortran: lexer = &mpf::detail::lex_fortran_expression; break;
    case mpf::SourceLanguage::typescript: lexer = &mpf::detail::lex_typescript_expression; break;
    case mpf::SourceLanguage::automatic: return {};
  }
  return mpf::detail::parse_expression(lexer(source, line, column), language);
}

const mpf::detail::python::ast::Statement& python_statement(
    const mpf::detail::python::ast::Program& program, const mpf::detail::AstNodeId id) {
  REQUIRE(id.valid());
  REQUIRE(id.value() < program.records.size());
  const auto& record = program.records[id.value()];
  REQUIRE(record.kind == mpf::detail::AstNodeKind::statement);
  REQUIRE(record.index < program.statements.size());
  return program.statements[record.index];
}

const mpf::detail::python::ast::Expression& python_expression(
    const mpf::detail::python::ast::Program& program, const mpf::detail::AstNodeId id) {
  REQUIRE(id.valid());
  REQUIRE(id.value() < program.records.size());
  const auto& record = program.records[id.value()];
  REQUIRE(record.kind == mpf::detail::AstNodeKind::expression);
  REQUIRE(record.index < program.expressions.size());
  return program.expressions[record.index];
}

}  // namespace

TEST_CASE("numeric side-table contracts distinguish dynamic scalars from nonnumeric values") {
  using mpf::detail::ValueType;
  REQUIRE(
      mpf::detail::numeric_contract_matches(ValueType::real, mpf::detail::unknown_numeric_type));
  REQUIRE(
      mpf::detail::numeric_contract_matches(ValueType::real, mpf::detail::complex_numeric_type));
  REQUIRE(
      !mpf::detail::numeric_contract_matches(ValueType::string, mpf::detail::unknown_numeric_type));
  REQUIRE(mpf::detail::numeric_contract_matches(ValueType::string, mpf::detail::no_numeric_type));
  REQUIRE(!mpf::detail::element_numeric_contract_matches(ValueType::tuple, ValueType::unknown,
                                                         mpf::detail::unknown_numeric_type));
  REQUIRE(mpf::detail::element_numeric_contract_matches(ValueType::tuple, ValueType::unknown,
                                                        mpf::detail::no_numeric_type));
}

TEST_CASE("SourceText maps CRLF and UTF-8 byte offsets to logical locations") {
  const mpf::detail::SourceText source(u8"α\r\nx\n", "utf8.py");
  REQUIRE(source.line_count() == 2);
  REQUIRE(source.line_text(1) == u8"α");
  REQUIRE(source.line_text(2) == "x");
  REQUIRE(source.location(2).line == 1);
  REQUIRE(source.location(2).column == 2);
  REQUIRE(source.location(4).line == 2);
  REQUIRE(source.location(4).column == 1);
}

TEST_CASE("SourceManager owns a stable multi-file table and resolves filenames") {
  mpf::detail::SourceManager sources;
  const auto python = sources.add("print(42)\n", "main.py");
  const auto fortran = sources.add("program main\nend program main\n", "main.f90");
  const auto first_address = &sources.source(python);
  for (std::size_t index = 0; index < 64; ++index) {
    static_cast<void>(sources.add("value = 1\n", "generated-" + std::to_string(index) + ".m"));
  }
  REQUIRE(sources.size() == 66);
  REQUIRE(&sources.source(python) == first_address);
  REQUIRE(sources.source(fortran).line_count() == 2);
  REQUIRE(sources.find("main.py").has_value());
  REQUIRE(*sources.find("main.py") == python);
  REQUIRE(!sources.find("missing.py").has_value());
}

TEST_CASE("free-form Fortran normalization joins continuations and splits semicolons") {
  const mpf::detail::SourceText source(
      "program forms\n"
      "integer :: value = 40 + & ! trailing comment\n"
      "! comment between continuation records\n"
      "&2; print *, value\n"
      "end program forms\n",
      "forms.f90");
  const auto normalized =
      mpf::detail::normalize_fortran_source(source, mpf::FortranSourceForm::automatic);
  REQUIRE(normalized.diagnostics.empty());
  REQUIRE(normalized.source_form == mpf::FortranSourceForm::free);
  REQUIRE(normalized.lines.size() == 4);
  REQUIRE(normalized.lines[1].text == "integer :: value = 40 +2");
  REQUIRE(normalized.lines[1].number == 2);
  REQUIRE(normalized.lines[2].text == "print *, value");
  REQUIRE(normalized.lines[2].number == 2);
}

TEST_CASE("fixed-form Fortran normalization applies columns and continuation records") {
  const mpf::detail::SourceText source(
      "C comment\n"
      "      INTEGER VALUE\n"
      "      VALUE = 40 +\n"
      "     &        2\n"
      "      PRINT *, VALUE\n",
      "forms.f");
  const auto normalized =
      mpf::detail::normalize_fortran_source(source, mpf::FortranSourceForm::automatic);
  REQUIRE(normalized.diagnostics.empty());
  REQUIRE(normalized.source_form == mpf::FortranSourceForm::fixed);
  REQUIRE(normalized.lines.size() == 3);
  REQUIRE(normalized.lines[1].text == "VALUE = 40 +        2");
  REQUIRE(normalized.lines[1].number == 3);
}

TEST_CASE("Fortran source-form normalization rejects malformed continuation boundaries") {
  const mpf::detail::SourceText orphan("&value = 1\n", "bad.f90");
  const mpf::detail::SourceText unfinished("value = 1 + &\n", "bad.f90");
  const mpf::detail::SourceText character("value = 'left&\nright'\n", "bad.f90");
  const auto orphan_result =
      mpf::detail::normalize_fortran_source(orphan, mpf::FortranSourceForm::free);
  const auto unfinished_result =
      mpf::detail::normalize_fortran_source(unfinished, mpf::FortranSourceForm::free);
  const auto character_result =
      mpf::detail::normalize_fortran_source(character, mpf::FortranSourceForm::free);
  REQUIRE(!orphan_result.diagnostics.empty());
  REQUIRE(!unfinished_result.diagnostics.empty());
  REQUIRE(!character_result.diagnostics.empty());
  REQUIRE(orphan_result.diagnostics.front().code == "MPF1301");
  REQUIRE(unfinished_result.diagnostics.front().code == "MPF1302");
  REQUIRE(character_result.diagnostics.front().code == "MPF1304");
  REQUIRE(character_result.diagnostics.front().location.line == 2);
}

TEST_CASE("Python logical source joins brackets backslashes comments and simple statements") {
  const mpf::detail::SourceText source(
      "# leading comment\n"
      "values = [\n"
      "  1, # first\n"
      "  2,\n"
      "]\n"
      "joined = 4 + \\\n"
      "  5\n"
      "left = 1; right = ';#' # trailing comment\n"
      "if True:\n"
      "\tprint(joined)\n",
      "logical.py");
  const auto normalized = mpf::detail::normalize_python_source(source);
  REQUIRE(normalized.diagnostics.empty());
  REQUIRE(normalized.lines.size() == 6);
  REQUIRE(normalized.lines[0].text == "values = [  1,  2, ]");
  REQUIRE(normalized.lines[0].number == 2);
  REQUIRE(normalized.lines[1].text == "joined = 4 +   5");
  REQUIRE(normalized.lines[1].number == 6);
  REQUIRE(normalized.lines[2].text == "left = 1");
  REQUIRE(normalized.lines[3].text == "right = ';#'");
  REQUIRE(normalized.lines[2].number == 8);
  REQUIRE(normalized.lines[3].number == 8);
  REQUIRE(normalized.lines[5].indent == 8);
}

TEST_CASE("Python logical source reports malformed continuation boundaries") {
  const mpf::detail::SourceText unclosed("value = (1 +\n  2\n", "bad.py");
  const mpf::detail::SourceText unfinished("value = 1 + \\\n", "bad.py");
  const mpf::detail::SourceText bad_string("value = 'unterminated\n", "bad.py");
  const auto unclosed_result = mpf::detail::normalize_python_source(unclosed);
  const auto unfinished_result = mpf::detail::normalize_python_source(unfinished);
  const auto string_result = mpf::detail::normalize_python_source(bad_string);
  REQUIRE(!unclosed_result.diagnostics.empty());
  REQUIRE(!unfinished_result.diagnostics.empty());
  REQUIRE(!string_result.diagnostics.empty());
  REQUIRE(unclosed_result.diagnostics.front().code == "MPF1402");
  REQUIRE(unfinished_result.diagnostics.front().code == "MPF1403");
  REQUIRE(string_result.diagnostics.front().code == "MPF1404");
}

TEST_CASE("Matlab logical source joins ellipses and splits top-level separators") {
  const mpf::detail::SourceText source(
      "%{\n"
      "ignored block comment\n"
      "%}\n"
      "values = [1 2 ... % continuation\n"
      "          3 4]; total = 10 % comment\n"
      "matrix = [\n"
      "          1 2\n"
      "          3 4\n"
      "]\n"
      "text = '100%'; if total == 10, disp(total), end\n",
      "logical.m");
  const auto normalized = mpf::detail::normalize_matlab_source(source);
  REQUIRE(normalized.diagnostics.empty());
  REQUIRE(normalized.lines.size() == 7);
  REQUIRE(normalized.lines[0].text.rfind("values = [1 2", 0) == 0);
  REQUIRE(normalized.lines[0].text.find("3 4]") != std::string::npos);
  REQUIRE(normalized.lines[0].number == 4);
  REQUIRE(normalized.lines[1].text == "total = 10");
  REQUIRE(normalized.lines[2].text.rfind("matrix = [", 0) == 0);
  REQUIRE(normalized.lines[2].text.find("1 2;") != std::string::npos);
  REQUIRE(normalized.lines[2].text.find("3 4") != std::string::npos);
  REQUIRE(normalized.lines[2].text.back() == ']');
  REQUIRE(normalized.lines[3].text == "text = '100%'");
  REQUIRE(normalized.lines[4].text == "if total == 10");
  REQUIRE(normalized.lines[5].text == "disp(total)");
  REQUIRE(normalized.lines[6].text == "end");
}

TEST_CASE("Matlab logical source diagnoses incomplete delimiters comments and ellipses") {
  const mpf::detail::SourceText delimiter("value = (1 +\n2)\n", "bad.m");
  const mpf::detail::SourceText comment("%{\nnever closed\n", "bad.m");
  const mpf::detail::SourceText ellipsis("value = 1 + ...\n", "bad.m");
  const auto delimiter_result = mpf::detail::normalize_matlab_source(delimiter);
  const auto comment_result = mpf::detail::normalize_matlab_source(comment);
  const auto ellipsis_result = mpf::detail::normalize_matlab_source(ellipsis);
  REQUIRE(!delimiter_result.diagnostics.empty());
  REQUIRE(!comment_result.diagnostics.empty());
  REQUIRE(!ellipsis_result.diagnostics.empty());
  REQUIRE(delimiter_result.diagnostics.front().code == "MPF1502");
  REQUIRE(comment_result.diagnostics.front().code == "MPF1504");
  REQUIRE(ellipsis_result.diagnostics.front().code == "MPF1505");
}

TEST_CASE("Python lexer normalizes logical comparison and floor division tokens") {
  const auto result =
      mpf::detail::lex_python_expression("not True and 7 // 2 is not None or 2 in [1, 2]", 4, 3);
  REQUIRE(result.diagnostics.empty());
  REQUIRE(result.tokens.size() == 18);
  REQUIRE(result.tokens[0].kind == mpf::detail::TokenKind::logical_not);
  REQUIRE(result.tokens[1].kind == mpf::detail::TokenKind::true_keyword);
  REQUIRE(result.tokens[2].kind == mpf::detail::TokenKind::logical_and);
  REQUIRE(result.tokens[4].kind == mpf::detail::TokenKind::floor_slash);
  REQUIRE(result.tokens[6].kind == mpf::detail::TokenKind::identity_is);
  REQUIRE(result.tokens[7].kind == mpf::detail::TokenKind::logical_not);
  REQUIRE(result.tokens[11].kind == mpf::detail::TokenKind::membership_in);
  REQUIRE(result.tokens[0].location.line == 4);
  REQUIRE(result.tokens[0].location.column == 3);
}

TEST_CASE("expression scanner profiles isolate language-specific spellings") {
  const auto python = mpf::detail::lex_python_expression("left ~= right", 1, 1);
  const auto matlab = mpf::detail::lex_matlab_expression("left // right", 1, 1);
  const auto matlab_imaginary = mpf::detail::lex_matlab_expression("2i + .5j + 1e-3i", 2, 4);
  const auto python_imaginary = mpf::detail::lex_python_expression("2i", 1, 1);
  const auto fortran = mpf::detail::lex_fortran_expression("left != right", 1, 1);
  const auto typescript = mpf::detail::lex_typescript_expression("left === right", 1, 1);

  REQUIRE(!python.diagnostics.empty());
  REQUIRE(!fortran.diagnostics.empty());
  REQUIRE(std::none_of(matlab.tokens.begin(), matlab.tokens.end(), [](const auto& token) {
    return token.kind == mpf::detail::TokenKind::floor_slash;
  }));
  REQUIRE(typescript.diagnostics.empty());
  REQUIRE(typescript.tokens.size() == 4);
  REQUIRE(typescript.tokens[1].kind == mpf::detail::TokenKind::equal_equal);
  REQUIRE(typescript.tokens[1].text == "===");
  REQUIRE(matlab_imaginary.diagnostics.empty());
  REQUIRE(matlab_imaginary.tokens.size() == 6);
  REQUIRE(matlab_imaginary.tokens[0].kind == mpf::detail::TokenKind::number);
  REQUIRE(matlab_imaginary.tokens[0].text == "2i");
  REQUIRE(matlab_imaginary.tokens[2].text == ".5j");
  REQUIRE(matlab_imaginary.tokens[4].text == "1e-3i");
  REQUIRE(matlab_imaginary.tokens[0].location.line == 2);
  REQUIRE(matlab_imaginary.tokens[0].location.column == 4);
  REQUIRE(python_imaginary.diagnostics.empty());
  REQUIRE(python_imaginary.tokens.size() == 3);
  REQUIRE(python_imaginary.tokens[0].text == "2");
  REQUIRE(python_imaginary.tokens[1].kind == mpf::detail::TokenKind::identifier);
  REQUIRE(python_imaginary.tokens[1].text == "i");
}

TEST_CASE(
    "Python expression lexer and parser preserve conditional and comparison-chain structure") {
  const auto lexed = mpf::detail::lex_python_expression("1 if ready else 0 < value <= 2", 3, 5);
  REQUIRE(lexed.diagnostics.empty());
  REQUIRE(lexed.tokens[1].kind == mpf::detail::TokenKind::conditional_if);
  REQUIRE(lexed.tokens[3].kind == mpf::detail::TokenKind::conditional_else);

  const auto chain = parse_expression("0 < probe(1) <= 2 != 3", mpf::SourceLanguage::python, 4);
  REQUIRE(chain.diagnostics.empty());
  REQUIRE(chain.expression.kind == mpf::detail::ExpressionKind::comparison_chain);
  REQUIRE(chain.expression.children.size() == 4);
  REQUIRE(chain.expression.comparisons.size() == 3);
  REQUIRE(chain.expression.comparisons[0] == mpf::detail::ComparisonOperator::less);
  REQUIRE(chain.expression.comparisons[1] == mpf::detail::ComparisonOperator::less_equal);
  REQUIRE(chain.expression.comparisons[2] == mpf::detail::ComparisonOperator::not_equal);

  const auto compound =
      parse_expression("needle not in values is not None", mpf::SourceLanguage::python, 5);
  REQUIRE(compound.diagnostics.empty());
  REQUIRE(compound.expression.kind == mpf::detail::ExpressionKind::comparison_chain);
  REQUIRE(compound.expression.comparisons.size() == 2);
  REQUIRE(compound.expression.comparisons[0] == mpf::detail::ComparisonOperator::not_contains);
  REQUIRE(compound.expression.comparisons[1] == mpf::detail::ComparisonOperator::not_identity);

  const auto precedence = parse_expression("not 1 in values", mpf::SourceLanguage::python, 5);
  REQUIRE(precedence.diagnostics.empty());
  REQUIRE(precedence.expression.kind == mpf::detail::ExpressionKind::unary);
  REQUIRE(precedence.expression.children.front().comparison ==
          mpf::detail::ComparisonOperator::contains);

  const auto conditional =
      parse_expression("1 if first else 2 if second else 3", mpf::SourceLanguage::python, 5);
  REQUIRE(conditional.diagnostics.empty());
  REQUIRE(conditional.expression.kind == mpf::detail::ExpressionKind::conditional);
  REQUIRE(conditional.expression.children.size() == 3);
  REQUIRE(conditional.expression.children[2].kind == mpf::detail::ExpressionKind::conditional);
}

TEST_CASE("Python statement lexer preserves keyword operator and source spans") {
  std::vector<mpf::detail::SourceLine> lines;
  lines.push_back({4, 20, 8, "def compute(value, other) -> float:"});
  lines.push_back({5, 60, 8, "result = 'a=b:c'"});
  lines.push_back({6, 80, 8, "result += 1"});
  const auto result = mpf::detail::lex_python_statements(std::move(lines));
  REQUIRE(result.diagnostics.empty());
  REQUIRE(result.lines.size() == 3);
  REQUIRE(result.lines[0].tokens[0].kind == mpf::detail::PythonStatementTokenKind::keyword_def);
  REQUIRE(result.lines[0].tokens[1].kind == mpf::detail::PythonStatementTokenKind::identifier);
  REQUIRE(result.lines[0].tokens[7].kind == mpf::detail::PythonStatementTokenKind::arrow);
  REQUIRE(result.lines[0].tokens.back().kind == mpf::detail::PythonStatementTokenKind::end);
  REQUIRE(result.lines[0].tokens[0].location.line == 4);
  REQUIRE(result.lines[0].tokens[0].location.column == 9);
  REQUIRE(result.lines[1].tokens[1].kind == mpf::detail::PythonStatementTokenKind::equal);
  REQUIRE(result.lines[1].tokens[2].kind == mpf::detail::PythonStatementTokenKind::string_literal);
  REQUIRE(result.lines[1].tokens.size() == 4);
  REQUIRE(result.lines[2].tokens[1].kind == mpf::detail::PythonStatementTokenKind::other);
  REQUIRE(result.lines[2].tokens[1].text == "+=");
}

TEST_CASE("Python statement lexer diagnoses invalid token boundaries") {
  std::vector<mpf::detail::SourceLine> lines;
  lines.push_back({1, 0, 0, "value = 'unterminated"});
  lines.push_back({2, 24, 0, std::string("value = 1\0", 10)});
  const auto result = mpf::detail::lex_python_statements(std::move(lines));
  REQUIRE(result.diagnostics.size() == 2);
  REQUIRE(result.diagnostics[0].code == "MPF1601");
  REQUIRE(result.diagnostics[0].location.line == 1);
  REQUIRE(result.diagnostics[1].code == "MPF1602");
  REQUIRE(result.diagnostics[1].location.line == 2);
}

TEST_CASE("Matlab statement lexer preserves function string and transpose boundaries") {
  std::vector<mpf::detail::SourceLine> lines;
  lines.push_back({3, 0, 0, "function [left right] = pair(value)"});
  lines.push_back({4, 40, 0, "text = 'a=b:c'"});
  lines.push_back({5, 60, 0, "copy = matrix.'"});
  lines.push_back({6, 80, 0, "other = matrix'"});
  const auto result = mpf::detail::lex_matlab_statements(std::move(lines));
  REQUIRE(result.diagnostics.empty());
  REQUIRE(result.lines.size() == 4);
  REQUIRE(result.lines[0].tokens[0].kind ==
          mpf::detail::MatlabStatementTokenKind::keyword_function);
  REQUIRE(result.lines[0].tokens[1].kind == mpf::detail::MatlabStatementTokenKind::left_bracket);
  REQUIRE(result.lines[0].tokens[5].kind == mpf::detail::MatlabStatementTokenKind::equal);
  REQUIRE(result.lines[1].tokens[1].kind == mpf::detail::MatlabStatementTokenKind::equal);
  REQUIRE(result.lines[1].tokens[2].kind == mpf::detail::MatlabStatementTokenKind::string_literal);
  REQUIRE(result.lines[2].tokens[3].kind == mpf::detail::MatlabStatementTokenKind::transpose);
  REQUIRE(result.lines[2].tokens[3].text == ".'");
  REQUIRE(result.lines[3].tokens[3].kind == mpf::detail::MatlabStatementTokenKind::transpose);
  REQUIRE(result.lines[0].tokens[0].location.line == 3);
  REQUIRE(result.lines[0].tokens[0].location.column == 1);
}

TEST_CASE("Matlab statement lexer diagnoses invalid token boundaries") {
  std::vector<mpf::detail::SourceLine> lines;
  lines.push_back({1, 0, 0, "value = 'unterminated"});
  lines.push_back({2, 24, 0, std::string("value = 1\0", 10)});
  const auto result = mpf::detail::lex_matlab_statements(std::move(lines));
  REQUIRE(result.diagnostics.size() == 2);
  REQUIRE(result.diagnostics[0].code == "MPF1701");
  REQUIRE(result.diagnostics[1].code == "MPF1702");
}

TEST_CASE("Matlab statement lexer classifies switch case and otherwise") {
  std::vector<mpf::detail::SourceLine> lines;
  lines.push_back({1, 0, 0, "switch choice"});
  lines.push_back({2, 14, 0, "case 'alpha'"});
  lines.push_back({3, 27, 0, "otherwise"});
  lines.push_back({4, 37, 0, "end"});
  const auto result = mpf::detail::lex_matlab_statements(std::move(lines));
  REQUIRE(result.diagnostics.empty());
  REQUIRE(result.lines[0].tokens[0].kind == mpf::detail::MatlabStatementTokenKind::keyword_switch);
  REQUIRE(result.lines[1].tokens[0].kind == mpf::detail::MatlabStatementTokenKind::keyword_case);
  REQUIRE(result.lines[1].tokens[1].kind == mpf::detail::MatlabStatementTokenKind::string_literal);
  REQUIRE(result.lines[2].tokens[0].kind ==
          mpf::detail::MatlabStatementTokenKind::keyword_otherwise);
  REQUIRE(result.lines[3].tokens[0].kind == mpf::detail::MatlabStatementTokenKind::keyword_end);
}

TEST_CASE("Matlab statement lexer classifies return and structured block keywords") {
  std::vector<mpf::detail::SourceLine> lines;
  lines.push_back({1, 0, 0, "return"});
  lines.push_back({2, 7, 0, "try"});
  lines.push_back({3, 11, 0, "catch failure"});
  lines.push_back({4, 25, 0, "arguments"});
  lines.push_back({5, 35, 0, "display 'hello world'"});
  const auto result = mpf::detail::lex_matlab_statements(std::move(lines));
  REQUIRE(result.diagnostics.empty());
  REQUIRE(result.lines[0].tokens[0].kind == mpf::detail::MatlabStatementTokenKind::keyword_return);
  REQUIRE(result.lines[1].tokens[0].kind == mpf::detail::MatlabStatementTokenKind::keyword_try);
  REQUIRE(result.lines[2].tokens[0].kind == mpf::detail::MatlabStatementTokenKind::keyword_catch);
  REQUIRE(result.lines[3].tokens[0].kind ==
          mpf::detail::MatlabStatementTokenKind::keyword_arguments);
  REQUIRE(result.lines[4].tokens[1].kind == mpf::detail::MatlabStatementTokenKind::string_literal);
}

TEST_CASE("Matlab command scanner preserves quoting and operator spacing") {
  std::vector<mpf::detail::SourceLine> lines;
  lines.push_back({1, 0, 0, "invoke +token"});
  lines.push_back({2, 14, 0, "invoke + token"});
  lines.push_back({3, 29, 0, "invoke+token"});
  lines.push_back({4, 42, 0, "invoke ./path"});
  lines.push_back({5, 56, 0, "invoke ./ path"});
  lines.push_back({6, 71, 0, "invoke 'two words' tail"});
  lines.push_back({7, 95, 0, "invoke \"two words\""});
  lines.push_back({8, 114, 0, "invoke terminal;"});
  lines.push_back({9, 131, 0, "if value'"});
  const auto result = mpf::detail::lex_matlab_statements(std::move(lines));
  REQUIRE(result.diagnostics.empty());
  REQUIRE(result.lines[0].command.has_value());
  REQUIRE(result.lines[0].command->arguments[0].value == "+token");
  REQUIRE(!result.lines[1].command.has_value());
  REQUIRE(!result.lines[2].command.has_value());
  REQUIRE(result.lines[3].command.has_value());
  REQUIRE(result.lines[3].command->arguments[0].value == "./path");
  REQUIRE(!result.lines[4].command.has_value());
  REQUIRE(result.lines[5].command.has_value());
  REQUIRE(result.lines[5].command->arguments.size() == 2U);
  REQUIRE(result.lines[5].command->arguments[0].value == "two words");
  REQUIRE(result.lines[5].command->arguments[0].form ==
          mpf::detail::MatlabCommandArgumentForm::single_quoted);
  REQUIRE(result.lines[5].command->arguments[1].value == "tail");
  REQUIRE(result.lines[6].command.has_value());
  REQUIRE(result.lines[6].command->arguments.size() == 2U);
  REQUIRE(result.lines[6].command->arguments[0].value == "\"two");
  REQUIRE(result.lines[6].command->arguments[1].value == "words\"");
  REQUIRE(result.lines[7].command.has_value());
  REQUIRE(result.lines[7].command->arguments[0].value == "terminal");
  REQUIRE(!result.lines[8].command.has_value());
  REQUIRE(result.lines[8].tokens[2].kind == mpf::detail::MatlabStatementTokenKind::transpose);
}

TEST_CASE("Matlab command scanner diagnoses unterminated quoted arguments") {
  std::vector<mpf::detail::SourceLine> lines;
  lines.push_back({1, 0, 0, "invoke 'unterminated"});
  const auto result = mpf::detail::lex_matlab_statements(std::move(lines));
  REQUIRE(result.lines.size() == 1U);
  REQUIRE(result.lines.front().command.has_value());
  REQUIRE(!result.lines.front().command->terminated);
  REQUIRE(result.diagnostics.size() == 1U);
  REQUIRE(result.diagnostics.front().code == "MPF1701");
}

TEST_CASE("Fortran statement lexer preserves declarations delimiters and contextual names") {
  std::vector<mpf::detail::SourceLine> lines;
  lines.push_back({2, 0, 0, "INTEGER :: BLOCK(2, 2) = RESHAPE([1,2,3,4], [2,2])"});
  lines.push_back({3, 60, 0, "ELSE IF (VALUE .GE. 2) THEN"});
  lines.push_back({4, 100, 0, "PRINT *, 'MiXeD'"});
  lines.push_back({5, 120, 0, "values = (/1, 2/)"});
  const auto result = mpf::detail::lex_fortran_statements(std::move(lines));
  REQUIRE(result.diagnostics.empty());
  REQUIRE(result.lines.size() == 4);
  REQUIRE(result.lines[0].tokens[0].kind ==
          mpf::detail::FortranStatementTokenKind::keyword_integer);
  REQUIRE(result.lines[0].tokens[1].kind == mpf::detail::FortranStatementTokenKind::double_colon);
  REQUIRE(result.lines[0].tokens[2].kind == mpf::detail::FortranStatementTokenKind::identifier);
  REQUIRE(result.lines[0].tokens[2].text == "block");
  REQUIRE(result.lines[1].tokens[0].kind == mpf::detail::FortranStatementTokenKind::keyword_else);
  REQUIRE(result.lines[1].tokens[1].kind == mpf::detail::FortranStatementTokenKind::keyword_if);
  REQUIRE(result.lines[2].tokens[3].kind == mpf::detail::FortranStatementTokenKind::string_literal);
  REQUIRE(result.lines[2].tokens[3].text == "'MiXeD'");
  REQUIRE(result.lines[3].tokens[2].kind == mpf::detail::FortranStatementTokenKind::left_bracket);
  REQUIRE(result.lines[3].tokens[6].kind == mpf::detail::FortranStatementTokenKind::right_bracket);
}

TEST_CASE("Fortran statement lexer diagnoses invalid token boundaries") {
  std::vector<mpf::detail::SourceLine> lines;
  lines.push_back({1, 0, 0, "value = 'unterminated"});
  lines.push_back({2, 24, 0, std::string("value = 1\0", 10)});
  const auto result = mpf::detail::lex_fortran_statements(std::move(lines));
  REQUIRE(result.diagnostics.size() == 2);
  REQUIRE(result.diagnostics[0].code == "MPF1801");
  REQUIRE(result.diagnostics[1].code == "MPF1802");
}

TEST_CASE("Fortran statement lexer classifies procedure structure contextually") {
  std::vector<mpf::detail::SourceLine> lines;
  lines.push_back({1, 0, 0, "recursive integer function factorial(n) result(value)"});
  lines.push_back({2, 56, 0, "subroutine announce(value)"});
  lines.push_back({3, 83, 0, "return"});
  lines.push_back({4, 90, 0, "end function factorial"});
  const auto result = mpf::detail::lex_fortran_statements(std::move(lines));
  REQUIRE(result.diagnostics.empty());
  REQUIRE(result.lines[0].tokens[0].kind ==
          mpf::detail::FortranStatementTokenKind::keyword_recursive);
  REQUIRE(result.lines[0].tokens[2].kind ==
          mpf::detail::FortranStatementTokenKind::keyword_function);
  REQUIRE(result.lines[0].tokens[7].kind == mpf::detail::FortranStatementTokenKind::keyword_result);
  REQUIRE(result.lines[1].tokens[0].kind ==
          mpf::detail::FortranStatementTokenKind::keyword_subroutine);
  REQUIRE(result.lines[2].tokens[0].kind == mpf::detail::FortranStatementTokenKind::keyword_return);
  REQUIRE(result.lines[3].tokens[1].kind ==
          mpf::detail::FortranStatementTokenKind::keyword_function);
}

TEST_CASE("Fortran statement lexer classifies SELECT CASE structure and ranges") {
  std::vector<mpf::detail::SourceLine> lines;
  lines.push_back({1, 0, 0, "select case (value)"});
  lines.push_back({2, 20, 0, "case (:0, 2, 4:6, 9:)"});
  lines.push_back({3, 48, 0, "case default"});
  lines.push_back({4, 61, 0, "endselect"});
  const auto result = mpf::detail::lex_fortran_statements(std::move(lines));
  REQUIRE(result.diagnostics.empty());
  REQUIRE(result.lines[0].tokens[0].kind == mpf::detail::FortranStatementTokenKind::keyword_select);
  REQUIRE(result.lines[0].tokens[1].kind == mpf::detail::FortranStatementTokenKind::keyword_case);
  REQUIRE(result.lines[1].tokens[0].kind == mpf::detail::FortranStatementTokenKind::keyword_case);
  REQUIRE(result.lines[1].tokens[2].kind == mpf::detail::FortranStatementTokenKind::colon);
  REQUIRE(result.lines[2].tokens[1].kind ==
          mpf::detail::FortranStatementTokenKind::keyword_default);
  REQUIRE(result.lines[3].tokens[0].kind ==
          mpf::detail::FortranStatementTokenKind::keyword_endselect);
}

TEST_CASE("Fortran lexer normalizes dotted operators and kind exponents") {
  const auto result = mpf::detail::lex_fortran_expression(".TRUE. .AND. x /= 1.0D+2_8", 2, 1);
  REQUIRE(result.diagnostics.empty());
  REQUIRE(result.tokens[0].kind == mpf::detail::TokenKind::true_keyword);
  REQUIRE(result.tokens[1].kind == mpf::detail::TokenKind::logical_and);
  REQUIRE(result.tokens[3].kind == mpf::detail::TokenKind::not_equal);
  REQUIRE(result.tokens[4].text == "1.0e+2");
}

TEST_CASE("Fortran expression parser preserves keyword actual argument names") {
  const auto result =
      parse_expression("combine(right=2, left=40)", mpf::SourceLanguage::fortran, 3);
  REQUIRE(result.diagnostics.empty());
  REQUIRE(result.expression.kind == mpf::detail::ExpressionKind::call);
  REQUIRE(result.expression.children.size() == 3);
  REQUIRE(result.expression.argument_names.size() == 2);
  REQUIRE(result.expression.argument_names[0] == "right");
  REQUIRE(result.expression.argument_names[1] == "left");

  const auto invalid = parse_expression("combine(right=2, 40)", mpf::SourceLanguage::fortran, 4);
  REQUIRE(!invalid.diagnostics.empty());
  REQUIRE(invalid.diagnostics.front().code == "MPF1015");
}

TEST_CASE("Python parser preserves parameter kinds defaults and keyword actual names") {
  const mpf::detail::SourceText source(
      "def combine(left, /, right=2, *, scale=1):\n"
      "    return (left + right) * scale\n",
      "parameters.py");
  const auto parsed = mpf::detail::parse_with_frontend(mpf::detail::python_frontend(), source);
  REQUIRE(parsed.diagnostics.empty());
  const auto& program = std::get<mpf::detail::python::ast::Program>(parsed.ast);
  REQUIRE(program.roots.size() == 1);
  const auto& function = python_statement(program, program.roots.front());
  REQUIRE(function.parameters.size() == 3);
  REQUIRE(function.parameter_kinds[0] == mpf::detail::ParameterKind::positional_only);
  REQUIRE(function.parameter_kinds[1] == mpf::detail::ParameterKind::positional_or_keyword);
  REQUIRE(function.parameter_kinds[2] == mpf::detail::ParameterKind::keyword_only);
  REQUIRE(!function.parameter_defaults[0].valid());
  REQUIRE(python_expression(program, function.parameter_defaults[1]).value == "2");
  REQUIRE(python_expression(program, function.parameter_defaults[2]).value == "1");

  const auto call =
      parse_expression("combine(40, scale=2, right=1)", mpf::SourceLanguage::python, 2);
  REQUIRE(call.diagnostics.empty());
  REQUIRE(call.expression.argument_names.size() == 3);
  REQUIRE(call.expression.argument_names[0].empty());
  REQUIRE(call.expression.argument_names[1] == "scale");
  REQUIRE(call.expression.argument_names[2] == "right");
}

TEST_CASE("Python parser normalizes flat tuple and list assignment targets") {
  const mpf::detail::SourceText source(
      "first, second = pair(39)\n"
      "(first, second) = (second, first)\n"
      "[left, right] = [20, 22]\n"
      "single, = (42,)\n"
      "(left, [head, *middle, tail]), answer = payload()\n",
      "unpacking.py");
  const auto parsed = mpf::detail::parse_with_frontend(mpf::detail::python_frontend(), source);
  REQUIRE(parsed.diagnostics.empty());
  const auto& program = std::get<mpf::detail::python::ast::Program>(parsed.ast);
  REQUIRE(program.roots.size() == 5);
  for (const auto root : program.roots) {
    const auto& statement = python_statement(program, root);
    REQUIRE(statement.kind == mpf::detail::StatementKind::multi_assignment);
  }
  const auto& first = python_statement(program, program.roots[0]);
  const auto& third = python_statement(program, program.roots[2]);
  const auto& fourth = python_statement(program, program.roots[3]);
  REQUIRE(first.target_names.size() == 2);
  REQUIRE(first.target_names[0] == "first");
  REQUIRE(first.target_names[1] == "second");
  REQUIRE(third.target_names[0] == "left");
  REQUIRE(third.target_names[1] == "right");
  REQUIRE(fourth.target_names.size() == 1);
  REQUIRE(fourth.target_names[0] == "single");
  const auto& nested = python_statement(program, program.roots[4]).target_pattern;
  REQUIRE(nested.kind == mpf::detail::AssignmentPatternKind::sequence);
  REQUIRE(nested.children.size() == 2);
  REQUIRE(nested.children[0].children.size() == 2);
  REQUIRE(nested.children[0].children[1].children.size() == 3);
  REQUIRE(nested.children[0].children[1].children[1].kind ==
          mpf::detail::AssignmentPatternKind::starred_name);
  REQUIRE(nested.children[0].children[1].children[1].name == "middle");
}

TEST_CASE("Pratt parser preserves precedence and right associative power") {
  const auto result = parse_expression("-2 ** 2 + 3 * 4", mpf::SourceLanguage::python, 1);
  REQUIRE(result.diagnostics.empty());
  REQUIRE(result.expression.kind == mpf::detail::ExpressionKind::binary);
  REQUIRE(result.expression.value == "+");
  REQUIRE(result.expression.children[0].kind == mpf::detail::ExpressionKind::unary);
  REQUIRE(result.expression.children[0].children[0].value == "**");
  REQUIRE(result.expression.children[1].value == "*");
}

TEST_CASE("Matlab parser preserves matrix and element-wise operator identity") {
  using mpf::detail::BinaryOperator;
  const std::vector<std::tuple<std::string, std::string, BinaryOperator>> operators{
      {"left * right", "*", BinaryOperator::multiply},
      {"left / right", "/", BinaryOperator::divide},
      {"left \\ right", "\\", BinaryOperator::left_divide},
      {"left ^ right", "^", BinaryOperator::power},
      {"left .* right", ".*", BinaryOperator::elementwise_multiply},
      {"left ./ right", "./", BinaryOperator::elementwise_divide},
      {"left .\\ right", ".\\", BinaryOperator::elementwise_left_divide},
      {"left .^ right", ".^", BinaryOperator::elementwise_power}};
  for (const auto& [source, expected, operation] : operators) {
    const auto result = parse_expression(source, mpf::SourceLanguage::matlab, 1);
    REQUIRE(result.diagnostics.empty());
    REQUIRE(result.expression.kind == mpf::detail::ExpressionKind::binary);
    REQUIRE(result.expression.value == expected);
    REQUIRE(result.expression.operation == operation);
  }

  const auto power = parse_expression("2 .^ 3 .^ 2", mpf::SourceLanguage::matlab, 1);
  REQUIRE(power.diagnostics.empty());
  REQUIRE(power.expression.value == ".^");
  REQUIRE(power.expression.children[1].value == ".^");
}

TEST_CASE("Matlab parser preserves logical identity and documented precedence") {
  using mpf::detail::BinaryOperator;
  using mpf::detail::ExpressionKind;

  const auto expression =
      parse_expression("a < b & c < d | e && f || g", mpf::SourceLanguage::matlab, 1);
  REQUIRE(expression.diagnostics.empty());
  REQUIRE(expression.expression.kind == ExpressionKind::binary);
  REQUIRE(expression.expression.operation == BinaryOperator::logical_or);

  const auto& short_and = expression.expression.children[0];
  REQUIRE(short_and.operation == BinaryOperator::logical_and);
  const auto& elementwise_or = short_and.children[0];
  REQUIRE(elementwise_or.operation == BinaryOperator::elementwise_logical_or);
  const auto& elementwise_and = elementwise_or.children[0];
  REQUIRE(elementwise_and.operation == BinaryOperator::elementwise_logical_and);
  REQUIRE(elementwise_and.children[0].comparison == mpf::detail::ComparisonOperator::less);
  REQUIRE(elementwise_and.children[1].comparison == mpf::detail::ComparisonOperator::less);

  const auto negated = parse_expression("~a & b == c", mpf::SourceLanguage::matlab, 1);
  REQUIRE(negated.diagnostics.empty());
  REQUIRE(negated.expression.operation == BinaryOperator::elementwise_logical_and);
  REQUIRE(negated.expression.children[0].kind == ExpressionKind::unary);
  REQUIRE(negated.expression.children[0].unary_operation ==
          mpf::detail::UnaryOperator::logical_not);
  REQUIRE(negated.expression.children[1].comparison == mpf::detail::ComparisonOperator::equal);
}

TEST_CASE("Matlab parser preserves conjugating and non-conjugating transpose identity") {
  using mpf::detail::UnaryOperator;
  const auto conjugating = parse_expression("values'", mpf::SourceLanguage::matlab, 1);
  const auto non_conjugating = parse_expression("values.'", mpf::SourceLanguage::matlab, 1);
  const auto chained = parse_expression("values''", mpf::SourceLanguage::matlab, 1);

  REQUIRE(conjugating.diagnostics.empty());
  REQUIRE(non_conjugating.diagnostics.empty());
  REQUIRE(chained.diagnostics.empty());
  REQUIRE(conjugating.expression.kind == mpf::detail::ExpressionKind::unary);
  REQUIRE(non_conjugating.expression.kind == mpf::detail::ExpressionKind::unary);
  REQUIRE(conjugating.expression.unary_operation == UnaryOperator::conjugate_transpose);
  REQUIRE(non_conjugating.expression.unary_operation == UnaryOperator::transpose);
  REQUIRE(chained.expression.unary_operation == UnaryOperator::conjugate_transpose);
  REQUIRE(chained.expression.children[0].unary_operation == UnaryOperator::conjugate_transpose);
}

TEST_CASE("malformed expressions report stable parser diagnostics") {
  const auto result = parse_expression("(1 + 2", mpf::SourceLanguage::python, 8, 5);
  const auto conditional = parse_expression("1 if True", mpf::SourceLanguage::python, 9);
  const auto non_python_chain = parse_expression("1 < 2 < 3", mpf::SourceLanguage::matlab, 10);
  REQUIRE(!result.diagnostics.empty());
  REQUIRE(!conditional.diagnostics.empty());
  REQUIRE(!non_python_chain.diagnostics.empty());
  REQUIRE(result.diagnostics.back().code == "MPF1012");
  REQUIRE(result.diagnostics.back().location.line == 8);
  REQUIRE(conditional.diagnostics.front().code == "MPF1111");
  REQUIRE(non_python_chain.diagnostics.front().code == "MPF1110");
}

TEST_CASE("subscript parser normalizes Python and Matlab slice ordering") {
  const auto python = parse_expression("values[1:6:2]", mpf::SourceLanguage::python, 1);
  const auto matlab = parse_expression("matrix(:, 1:2:5)", mpf::SourceLanguage::matlab, 1);
  REQUIRE(python.diagnostics.empty());
  REQUIRE(matlab.diagnostics.empty());
  REQUIRE(python.expression.kind == mpf::detail::ExpressionKind::index);
  REQUIRE(python.expression.children[1].kind == mpf::detail::ExpressionKind::slice);
  REQUIRE(python.expression.children[1].children[0].value == "1");
  REQUIRE(python.expression.children[1].children[1].value == "6");
  REQUIRE(python.expression.children[1].children[2].value == "2");
  REQUIRE(!python.expression.children[1].slice_stop_inclusive);
  REQUIRE(matlab.expression.kind == mpf::detail::ExpressionKind::call);
  REQUIRE(matlab.expression.children[1].kind == mpf::detail::ExpressionKind::slice);
  REQUIRE(matlab.expression.children[2].children[0].value == "1");
  REQUIRE(matlab.expression.children[2].children[1].value == "5");
  REQUIRE(matlab.expression.children[2].children[2].value == "2");
  REQUIRE(matlab.expression.children[2].slice_stop_inclusive);
}

TEST_CASE("function dependency graph orders callees and detects semantic recursion") {
  const mpf::detail::SourceText source(
      "def first():\n"
      "    return second()\n"
      "def second():\n"
      "    return 42\n"
      "def recursive(value):\n"
      "    return recursive(value)\n"
      "def callback(second):\n"
      "    return second()\n",
      "graph.py");
  auto parsed = mpf::detail::parse_with_frontend(mpf::detail::python_frontend(), source);
  REQUIRE(parsed.diagnostics.empty());
  auto lowered = mpf::detail::python_frontend().lower(std::move(parsed.ast));
  REQUIRE(lowered.diagnostics.empty());
  const auto graph = mpf::detail::build_function_dependency_graph_generic<
      mpf::detail::hir::Expression, mpf::detail::hir::Statement>(lowered.program.statements);
  REQUIRE(graph.definition_order.size() == 4);
  REQUIRE(graph.definition_order[0] == 1);
  REQUIRE(graph.definition_order[1] == 0);
  REQUIRE(graph.dependencies[0].size() == 1);
  REQUIRE(graph.dependencies[0][0] == 1);
  REQUIRE(graph.dependencies[3].empty());
  REQUIRE(!graph.recursive[0]);
  REQUIRE(graph.recursive[2]);
}

TEST_CASE("TypeScript statement lexer preserves strict operators comments and source locations") {
  const mpf::detail::SourceText source(
      "// header\nconst answer: number = 40 + 2;\nconsole.log(answer !== 0);\n", "tokens.ts");
  const auto lexed = mpf::detail::lex_typescript_statements(source);
  REQUIRE(lexed.diagnostics.empty());
  REQUIRE(std::any_of(lexed.tokens.begin(), lexed.tokens.end(), [](const auto& token) {
    return token.kind == mpf::detail::TypeScriptStatementTokenKind::keyword_const;
  }));
  const auto strict = std::find_if(lexed.tokens.begin(), lexed.tokens.end(), [](const auto& token) {
    return token.kind == mpf::detail::TypeScriptStatementTokenKind::strict_not_equal;
  });
  REQUIRE(strict != lexed.tokens.end());
  REQUIRE(strict->location.line == 3);
  REQUIRE(strict->text == "!==");
}

TEST_CASE(
    "TypeScript frontend builds its own arena AST and fails closed on unsupported semantics") {
  const mpf::detail::SourceText source(
      "export function add(left: number, right: number = 2): number {\n"
      "  return left + right;\n"
      "}\n"
      "const answer: number = add(40);\n"
      "console.log(answer);\n",
      "arena.ts");
  auto parsed = mpf::detail::parse_with_frontend(mpf::detail::typescript_frontend(), source);
  REQUIRE(parsed.diagnostics.empty());
  const auto* ast = std::get_if<mpf::detail::typescript::ast::Program>(&parsed.ast);
  REQUIRE(ast != nullptr);
  REQUIRE(ast->language == mpf::SourceLanguage::typescript);
  REQUIRE(ast->roots.size() == 3);
  const auto& function_record = ast->records[ast->roots.front().value()];
  REQUIRE(ast->statements[function_record.index].exported);
  REQUIRE(ast->node_count() + 1U == ast->records.size());
  REQUIRE(mpf::detail::typescript_frontend().verify(parsed.ast).empty());
  REQUIRE(mpf::detail::dump_frontend_ast(parsed.ast).find("ast typescript") == 0U);

  const mpf::detail::SourceText rejected(
      "var value = 1;\nconst same = value == 1;\nconst text = `value=${value}`;\n", "bad.ts");
  const auto invalid =
      mpf::detail::parse_with_frontend(mpf::detail::typescript_frontend(), rejected);
  REQUIRE(!invalid.diagnostics.empty());
  REQUIRE(
      std::any_of(invalid.diagnostics.begin(), invalid.diagnostics.end(),
                  [](const mpf::Diagnostic& diagnostic) { return diagnostic.code == "MPF1200"; }));
}
