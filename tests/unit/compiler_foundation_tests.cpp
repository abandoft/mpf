#include <string>

#include "compiler/expression.hpp"
#include "compiler/frontend.hpp"
#include "compiler/function_graph.hpp"
#include "frontends/fortran_source_form.hpp"
#include "frontends/logical_source.hpp"
#include "lexer/fortran_statement_lexer.hpp"
#include "lexer/lexer.hpp"
#include "lexer/matlab_statement_lexer.hpp"
#include "lexer/python_statement_lexer.hpp"
#include "source/source_manager.hpp"
#include "source/source_text.hpp"
#include "test_framework.hpp"

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

TEST_CASE("Python lexer normalizes logical and floor division tokens") {
  const auto result = mpf::detail::lex_python_expression("not True and 7 // 2", 4, 3);
  REQUIRE(result.diagnostics.empty());
  REQUIRE(result.tokens.size() == 7);
  REQUIRE(result.tokens[0].kind == mpf::detail::TokenKind::logical_not);
  REQUIRE(result.tokens[1].kind == mpf::detail::TokenKind::true_keyword);
  REQUIRE(result.tokens[2].kind == mpf::detail::TokenKind::logical_and);
  REQUIRE(result.tokens[4].kind == mpf::detail::TokenKind::floor_slash);
  REQUIRE(result.tokens[0].location.line == 4);
  REQUIRE(result.tokens[0].location.column == 3);
}

TEST_CASE(
    "Python expression lexer and parser preserve conditional and comparison-chain structure") {
  const auto lexed = mpf::detail::lex_python_expression("1 if ready else 0 < value <= 2", 3, 5);
  REQUIRE(lexed.diagnostics.empty());
  REQUIRE(lexed.tokens[1].kind == mpf::detail::TokenKind::conditional_if);
  REQUIRE(lexed.tokens[3].kind == mpf::detail::TokenKind::conditional_else);

  const auto chain =
      mpf::detail::parse_expression("0 < probe(1) <= 2 != 3", mpf::SourceLanguage::python, 4);
  REQUIRE(chain.diagnostics.empty());
  REQUIRE(chain.expression.kind == mpf::detail::ExpressionKind::comparison_chain);
  REQUIRE(chain.expression.children.size() == 4);
  REQUIRE(chain.expression.operators.size() == 3);
  REQUIRE(chain.expression.operators[0] == "<");
  REQUIRE(chain.expression.operators[1] == "<=");
  REQUIRE(chain.expression.operators[2] == "!==");

  const auto conditional = mpf::detail::parse_expression("1 if first else 2 if second else 3",
                                                         mpf::SourceLanguage::python, 5);
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
      mpf::detail::parse_expression("combine(right=2, left=40)", mpf::SourceLanguage::fortran, 3);
  REQUIRE(result.diagnostics.empty());
  REQUIRE(result.expression.kind == mpf::detail::ExpressionKind::call);
  REQUIRE(result.expression.children.size() == 3);
  REQUIRE(result.expression.argument_names.size() == 2);
  REQUIRE(result.expression.argument_names[0] == "right");
  REQUIRE(result.expression.argument_names[1] == "left");

  const auto invalid =
      mpf::detail::parse_expression("combine(right=2, 40)", mpf::SourceLanguage::fortran, 4);
  REQUIRE(!invalid.diagnostics.empty());
  REQUIRE(invalid.diagnostics.front().code == "MPF1015");
}

TEST_CASE("Python parser preserves parameter kinds defaults and keyword actual names") {
  const mpf::detail::SourceText source(
      "def combine(left, /, right=2, *, scale=1):\n"
      "    return (left + right) * scale\n",
      "parameters.py");
  const auto parsed = mpf::detail::parse_python(source);
  REQUIRE(parsed.diagnostics.empty());
  REQUIRE(parsed.program.statements.size() == 1);
  const auto& function = parsed.program.statements.front();
  REQUIRE(function.parameters.size() == 3);
  REQUIRE(function.parameter_kinds[0] == mpf::detail::ParameterKind::positional_only);
  REQUIRE(function.parameter_kinds[1] == mpf::detail::ParameterKind::positional_or_keyword);
  REQUIRE(function.parameter_kinds[2] == mpf::detail::ParameterKind::keyword_only);
  REQUIRE(!function.parameter_defaults[0].valid());
  REQUIRE(function.parameter_defaults[1].value == "2");
  REQUIRE(function.parameter_defaults[2].value == "1");

  const auto call = mpf::detail::parse_expression("combine(40, scale=2, right=1)",
                                                  mpf::SourceLanguage::python, 2);
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
  const auto parsed = mpf::detail::parse_python(source);
  REQUIRE(parsed.diagnostics.empty());
  REQUIRE(parsed.program.statements.size() == 5);
  for (const auto& statement : parsed.program.statements) {
    REQUIRE(statement.kind == mpf::detail::StatementKind::multi_assignment);
  }
  REQUIRE(parsed.program.statements[0].target_names.size() == 2);
  REQUIRE(parsed.program.statements[0].target_names[0] == "first");
  REQUIRE(parsed.program.statements[0].target_names[1] == "second");
  REQUIRE(parsed.program.statements[2].target_names[0] == "left");
  REQUIRE(parsed.program.statements[2].target_names[1] == "right");
  REQUIRE(parsed.program.statements[3].target_names.size() == 1);
  REQUIRE(parsed.program.statements[3].target_names[0] == "single");
  const auto& nested = parsed.program.statements[4].target_pattern;
  REQUIRE(nested.kind == mpf::detail::AssignmentPatternKind::sequence);
  REQUIRE(nested.children.size() == 2);
  REQUIRE(nested.children[0].children.size() == 2);
  REQUIRE(nested.children[0].children[1].children.size() == 3);
  REQUIRE(nested.children[0].children[1].children[1].kind ==
          mpf::detail::AssignmentPatternKind::starred_name);
  REQUIRE(nested.children[0].children[1].children[1].name == "middle");
}

TEST_CASE("Pratt parser preserves precedence and right associative power") {
  const auto result =
      mpf::detail::parse_expression("-2 ** 2 + 3 * 4", mpf::SourceLanguage::python, 1);
  REQUIRE(result.diagnostics.empty());
  REQUIRE(result.expression.kind == mpf::detail::ExpressionKind::binary);
  REQUIRE(result.expression.value == "+");
  REQUIRE(result.expression.children[0].kind == mpf::detail::ExpressionKind::unary);
  REQUIRE(result.expression.children[0].children[0].value == "**");
  REQUIRE(result.expression.children[1].value == "*");
}

TEST_CASE("malformed expressions report stable parser diagnostics") {
  const auto result = mpf::detail::parse_expression("(1 + 2", mpf::SourceLanguage::python, 8, 5);
  const auto conditional =
      mpf::detail::parse_expression("1 if True", mpf::SourceLanguage::python, 9);
  const auto non_python_chain =
      mpf::detail::parse_expression("1 < 2 < 3", mpf::SourceLanguage::matlab, 10);
  REQUIRE(!result.diagnostics.empty());
  REQUIRE(!conditional.diagnostics.empty());
  REQUIRE(!non_python_chain.diagnostics.empty());
  REQUIRE(result.diagnostics.back().code == "MPF1012");
  REQUIRE(result.diagnostics.back().location.line == 8);
  REQUIRE(conditional.diagnostics.front().code == "MPF1111");
  REQUIRE(non_python_chain.diagnostics.front().code == "MPF1110");
}

TEST_CASE("subscript parser normalizes Python and Matlab slice ordering") {
  const auto python =
      mpf::detail::parse_expression("values[1:6:2]", mpf::SourceLanguage::python, 1);
  const auto matlab =
      mpf::detail::parse_expression("matrix(:, 1:2:5)", mpf::SourceLanguage::matlab, 1);
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
  const auto parsed = mpf::detail::parse_python(source);
  REQUIRE(parsed.diagnostics.empty());
  const auto graph = mpf::detail::build_function_dependency_graph(parsed.program.statements);
  REQUIRE(graph.definition_order.size() == 4);
  REQUIRE(graph.definition_order[0] == 1);
  REQUIRE(graph.definition_order[1] == 0);
  REQUIRE(graph.dependencies[0].size() == 1);
  REQUIRE(graph.dependencies[0][0] == 1);
  REQUIRE(graph.dependencies[3].empty());
  REQUIRE(!graph.recursive[0]);
  REQUIRE(graph.recursive[2]);
}
