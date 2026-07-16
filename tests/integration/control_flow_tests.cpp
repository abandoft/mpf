#include <string>

#include "mpf/transpiler.hpp"
#include "test_framework.hpp"

namespace {

mpf::TranspileResult transpile_flow(const std::string& source, const mpf::SourceLanguage language,
                                    const mpf::TargetLanguage target) {
  mpf::TranspileOptions options;
  options.language = language;
  options.target = target;
  options.emit_source_banner = false;
  return mpf::Transpiler{}.transpile(source, options);
}

}  // namespace

TEST_CASE("Python range and while lower through both backends") {
  const std::string source =
      "total = 0\n"
      "for index in range(5, 0, -2):\n"
      "    total = total + index\n"
      "while total < 12:\n"
      "    total = total + 1\n"
      "print(total, index)\n";
  const auto javascript =
      transpile_flow(source, mpf::SourceLanguage::python, mpf::TargetLanguage::javascript);
  const auto cpp = transpile_flow(source, mpf::SourceLanguage::python, mpf::TargetLanguage::cpp);
  REQUIRE(javascript.success());
  REQUIRE(cpp.success());
  REQUIRE(javascript.code.find("for (let mpf_internal_cursor") != std::string::npos);
  REQUIRE(javascript.code.find("while (__mpf_truthy(total < 12))") != std::string::npos);
  REQUIRE(cpp.code.find("auto mpf_internal_cursor") != std::string::npos);
  REQUIRE(cpp.code.find("while (mpf_runtime::range_next(") != std::string::npos);
  REQUIRE(cpp.code.find("while (mpf_runtime::truthy(mpf_runtime::py_compare(") !=
          std::string::npos);
  REQUIRE(cpp.code.find("std::less<>{}") != std::string::npos);
}

TEST_CASE("Python truthiness and operand-returning short-circuit logic lower independently") {
  const std::string source =
      "value = 0 and 2\n"
      "other = 3 or 4\n"
      "if not []:\n"
      "    print(value, other)\n";
  const auto javascript =
      transpile_flow(source, mpf::SourceLanguage::python, mpf::TargetLanguage::javascript);
  const auto cpp = transpile_flow(source, mpf::SourceLanguage::python, mpf::TargetLanguage::cpp);
  REQUIRE(javascript.success());
  REQUIRE(cpp.success());
  REQUIRE(javascript.code.find("__mpf_py_and(() => (0), () => (2))") != std::string::npos);
  REQUIRE(javascript.code.find("__mpf_py_not([])") != std::string::npos);
  REQUIRE(cpp.code.find("mpf_runtime::py_and([&]() { return (0); }") != std::string::npos);
  REQUIRE(cpp.code.find("std::int64_t value") != std::string::npos);
}

TEST_CASE("Matlab colon range is inclusive and preserves its last value") {
  const std::string source =
      "total = 0;\nfor index = 5:-2:1\n  total = total + index;\nend\ndisp(total)\n";
  const auto result =
      transpile_flow(source, mpf::SourceLanguage::matlab, mpf::TargetLanguage::javascript);
  REQUIRE(result.success());
  REQUIRE(result.code.find("mpf_internal_cursor") != std::string::npos);
  REQUIRE(result.code.find(" >= mpf_internal_stop") != std::string::npos);
}

TEST_CASE("Fortran counted DO exposes the terminal induction value") {
  const std::string source =
      "program loops\ninteger :: total = 0, index\ndo index = 5, 1, -2\n"
      "total = total + index\nend do\nprint *, total, index\nend program loops\n";
  const auto result =
      transpile_flow(source, mpf::SourceLanguage::fortran, mpf::TargetLanguage::cpp);
  REQUIRE(result.success());
  REQUIRE(result.code.find("index = mpf_internal_start") != std::string::npos);
  REQUIRE(result.code.find("while (mpf_runtime::range_next(index") != std::string::npos);
}

TEST_CASE("Fortran SELECT CASE lowers scalar values ranges and default through both backends") {
  const std::string source =
      "program selection\n"
      "integer :: value = 4, result\n"
      "character(len=2) :: grade = 'b '\n"
      "logical :: enabled = .true.\n"
      "select case (value)\n"
      "case (:0)\n"
      "result = 1\n"
      "case (1, 3:5, 9:)\n"
      "result = 40\n"
      "case default\n"
      "result = 2\n"
      "end select\n"
      "select case (grade)\n"
      "case ('a')\n"
      "result = result + 1\n"
      "case ('b':'c')\n"
      "result = result + 2\n"
      "case default\n"
      "result = 0\n"
      "endselect\n"
      "select case (enabled)\n"
      "case (.true.)\n"
      "select case (result)\n"
      "case (42)\n"
      "result = result + 0\n"
      "case default\n"
      "result = 0\n"
      "end select\n"
      "case default\n"
      "result = 0\n"
      "end select\n"
      "print *, result\n"
      "end program selection\n";
  const auto javascript =
      transpile_flow(source, mpf::SourceLanguage::fortran, mpf::TargetLanguage::javascript);
  const auto cpp = transpile_flow(source, mpf::SourceLanguage::fortran, mpf::TargetLanguage::cpp);
  REQUIRE(javascript.success());
  REQUIRE(cpp.success());
  REQUIRE(javascript.code.find("const mpf_internal_select") != std::string::npos);
  REQUIRE(javascript.code.find("__mpf_fortran_compare(mpf_internal_select") != std::string::npos);
  REQUIRE(javascript.code.find(" === true") != std::string::npos);
  REQUIRE(cpp.code.find("const auto mpf_internal_select") != std::string::npos);
  REQUIRE(cpp.code.find("mpf_runtime::fortran_compare(mpf_internal_select") != std::string::npos);
  REQUIRE(cpp.code.find(" == true") != std::string::npos);
}

TEST_CASE("Fortran SELECT CASE enforces constants types overlap and definite assignment") {
  const auto no_default = transpile_flow(
      "program bad\ninteger :: key = 1, value\nselect case (key)\ncase (1)\n"
      "value = 42\nend select\nprint *, value\nend program bad\n",
      mpf::SourceLanguage::fortran, mpf::TargetLanguage::javascript);
  const auto overlap = transpile_flow(
      "program bad\ninteger :: key = 2\nselect case (key)\ncase (1:3)\nprint *, 1\n"
      "case (3:5)\nprint *, 2\nend select\nend program bad\n",
      mpf::SourceLanguage::fortran, mpf::TargetLanguage::javascript);
  const auto dynamic_bound = transpile_flow(
      "program bad\ninteger :: key = 2\nselect case (key)\ncase (key)\nprint *, 1\n"
      "end select\nend program bad\n",
      mpf::SourceLanguage::fortran, mpf::TargetLanguage::javascript);
  const auto wrong_type = transpile_flow(
      "program bad\ninteger :: key = 2\nselect case (key)\ncase ('two')\nprint *, 2\n"
      "end select\nend program bad\n",
      mpf::SourceLanguage::fortran, mpf::TargetLanguage::javascript);
  const auto character_overlap = transpile_flow(
      "program bad\ncharacter(len=2) :: key = 'c '\nselect case (key)\n"
      "case ('a':'c')\nprint *, 1\ncase ('c ':'d')\nprint *, 2\n"
      "end select\nend program bad\n",
      mpf::SourceLanguage::fortran, mpf::TargetLanguage::javascript);
  const auto logical_overlap = transpile_flow(
      "program bad\nlogical :: key = .true.\nselect case (key)\n"
      "case (.true.)\nprint *, 1\ncase (.true.)\nprint *, 2\n"
      "end select\nend program bad\n",
      mpf::SourceLanguage::fortran, mpf::TargetLanguage::javascript);
  const auto malformed = transpile_flow(
      "program bad\ninteger :: key = 2\nselect case (key)\ncase default\nprint *, 1\n"
      "case default\nprint *, 2\nend program bad\n",
      mpf::SourceLanguage::fortran, mpf::TargetLanguage::javascript);
  REQUIRE(!no_default.success());
  REQUIRE(!overlap.success());
  REQUIRE(!dynamic_bound.success());
  REQUIRE(!wrong_type.success());
  REQUIRE(!character_overlap.success());
  REQUIRE(!logical_overlap.success());
  REQUIRE(!malformed.success());
  REQUIRE(no_default.diagnostics.front().code == "MPF2003");
  REQUIRE(overlap.diagnostics.front().code == "MPF2043");
  REQUIRE(dynamic_bound.diagnostics.front().code == "MPF2043");
  REQUIRE(wrong_type.diagnostics.front().code == "MPF2043");
  REQUIRE(character_overlap.diagnostics.front().code == "MPF2043");
  REQUIRE(logical_overlap.diagnostics.front().code == "MPF2043");
  REQUIRE(malformed.diagnostics.front().code == "MPF1200");
}

TEST_CASE("Python elif, continue, break and loop else form one structured control-flow graph") {
  const std::string source =
      "total = 0\n"
      "for index in range(1, 10):\n"
      "    if index == 2:\n"
      "        continue\n"
      "    elif index == 5:\n"
      "        break\n"
      "    else:\n"
      "        total = total + index\n"
      "else:\n"
      "    total = 999\n"
      "print(total, index)\n";
  const auto javascript =
      transpile_flow(source, mpf::SourceLanguage::python, mpf::TargetLanguage::javascript);
  const auto cpp = transpile_flow(source, mpf::SourceLanguage::python, mpf::TargetLanguage::cpp);
  REQUIRE(javascript.success());
  REQUIRE(cpp.success());
  REQUIRE(javascript.code.find("loop_completed") != std::string::npos);
  REQUIRE(javascript.code.find("continue;") != std::string::npos);
  REQUIRE(cpp.code.find("loop_completed") != std::string::npos);
  REQUIRE(cpp.code.find("break;") != std::string::npos);
}
