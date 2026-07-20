#include <algorithm>
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
  REQUIRE(cpp.code.find("while (mpf_runtime::truthy(([&]() {") != std::string::npos);
  REQUIRE(cpp.code.find("return mpf_runtime::py_compare(") != std::string::npos);
  REQUIRE(cpp.code.find("std::less<>{}") != std::string::npos);
}

TEST_CASE("Python truthiness and operand-returning short-circuit logic lower independently") {
  const std::string source =
      "value = 0 and 2\n"
      "other = 3 or 4\n"
      "items = [1] and [2]\n"
      "if not []:\n"
      "    print(value, other, sum(items))\n";
  const auto javascript =
      transpile_flow(source, mpf::SourceLanguage::python, mpf::TargetLanguage::javascript);
  const auto cpp = transpile_flow(source, mpf::SourceLanguage::python, mpf::TargetLanguage::cpp);
  REQUIRE(javascript.success());
  REQUIRE(cpp.success());
  REQUIRE(javascript.code.find("__mpf_py_and(() => (0), () => (2))") != std::string::npos);
  REQUIRE(javascript.code.find("__mpf_py_not([])") != std::string::npos);
  REQUIRE(cpp.code.find("mpf_runtime::py_and([&]() { return (0); }") != std::string::npos);
  REQUIRE(cpp.code.find("std::int64_t value") != std::string::npos);
  REQUIRE(cpp.code.find("std::vector<std::int64_t> items") != std::string::npos);
  REQUIRE(cpp.code.find("std::vector<bool> items") == std::string::npos);
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

TEST_CASE("Matlab switch evaluates once and lowers exact scalar cases through both backends") {
  const std::string source =
      "choice = 'beta';\n"
      "result = 0;\n"
      "switch choice\n"
      "case 'alpha'\n"
      "  result = 1;\n"
      "case 'beta'\n"
      "  result = 40;\n"
      "otherwise\n"
      "  result = -1;\n"
      "end\n"
      "switch 2\n"
      "case 1\n"
      "  result = 0;\n"
      "case 2.0\n"
      "  result = result + 2;\n"
      "otherwise\n"
      "  result = 0;\n"
      "end\n"
      "disp(result)\n";
  const auto javascript =
      transpile_flow(source, mpf::SourceLanguage::matlab, mpf::TargetLanguage::javascript);
  const auto cpp = transpile_flow(source, mpf::SourceLanguage::matlab, mpf::TargetLanguage::cpp);
  REQUIRE(javascript.success());
  REQUIRE(cpp.success());
  REQUIRE(javascript.code.find("const mpf_internal_select") != std::string::npos);
  REQUIRE(javascript.code.find(" === \"beta\"") != std::string::npos);
  REQUIRE(javascript.code.find("__mpf_fortran_compare") == std::string::npos);
  REQUIRE(cpp.code.find("const auto mpf_internal_select") != std::string::npos);
  REQUIRE(cpp.code.find(" == std::string{\"beta\"}") != std::string::npos);
  REQUIRE(cpp.code.find("fortran_compare(mpf_internal_select") == std::string::npos);
}

TEST_CASE("Matlab switch rejects malformed clauses and unsupported selector shapes") {
  const auto duplicate_default =
      transpile_flow("switch 1\notherwise\ndisp(1)\notherwise\ndisp(2)\nend\n",
                     mpf::SourceLanguage::matlab, mpf::TargetLanguage::javascript);
  const auto case_after_default =
      transpile_flow("switch 1\notherwise\ndisp(1)\ncase 1\ndisp(2)\nend\n",
                     mpf::SourceLanguage::matlab, mpf::TargetLanguage::javascript);
  const auto wrong_type =
      transpile_flow("switch 1\ncase 'one'\ndisp(1)\notherwise\ndisp(2)\nend\n",
                     mpf::SourceLanguage::matlab, mpf::TargetLanguage::javascript);
  const auto missing_end = transpile_flow(
      "switch 1\ncase 1\ndisp(1)\n", mpf::SourceLanguage::matlab, mpf::TargetLanguage::javascript);
  REQUIRE(!duplicate_default.success());
  REQUIRE(!case_after_default.success());
  REQUIRE(!wrong_type.success());
  REQUIRE(!missing_end.success());
  REQUIRE(duplicate_default.diagnostics.front().code == "MPF1200");
  REQUIRE(case_after_default.diagnostics.front().code == "MPF1200");
  REQUIRE(wrong_type.diagnostics.front().code == "MPF2043");
  REQUIRE(missing_end.diagnostics.front().code == "MPF1200");
}

TEST_CASE("Matlab try catch lowers explicit exception state through both backends") {
  const std::string source =
      "value = 0;\n"
      "try\n"
      "  error('MPF:Expected', 'boom')\n"
      "catch ME\n"
      "  disp(ME.identifier)\n"
      "  value = 42;\n"
      "end\n"
      "try\n"
      "  value = value + 0;\n"
      "catch\n"
      "end\n";
  const auto javascript =
      transpile_flow(source, mpf::SourceLanguage::matlab, mpf::TargetLanguage::javascript);
  const auto cpp = transpile_flow(source, mpf::SourceLanguage::matlab, mpf::TargetLanguage::cpp);
  REQUIRE(javascript.success());
  REQUIRE(cpp.success());
  REQUIRE(javascript.code.find("function __mpf_capture_exception") != std::string::npos);
  REQUIRE(javascript.code.find("function __mpf_matlab_error") != std::string::npos);
  REQUIRE(javascript.code.find("catch (__mpf_caught)") != std::string::npos);
  REQUIRE(javascript.code.find("ME = __mpf_capture_exception(__mpf_caught)") != std::string::npos);
  REQUIRE(cpp.code.find("class matlab_exception final") != std::string::npos);
  REQUIRE(cpp.code.find("catch (...)") != std::string::npos);
  REQUIRE(cpp.code.find("ME = mpf_runtime::capture_exception(std::current_exception())") !=
          std::string::npos);
  for (const auto* result : {&javascript, &cpp}) {
    REQUIRE(std::any_of(result->source_map.segments.begin(), result->source_map.segments.end(),
                        [](const auto& segment) { return segment.original_line == 2U; }));
    REQUIRE(std::any_of(result->source_map.segments.begin(), result->source_map.segments.end(),
                        [](const auto& segment) { return segment.original_line == 4U; }));
  }
}

TEST_CASE("Matlab nested catch can rethrow the original exception") {
  const std::string source =
      "try\n"
      "  try\n"
      "    error('MPF:Nested', 'nested')\n"
      "  catch inner\n"
      "    rethrow(inner)\n"
      "  end\n"
      "catch outer\n"
      "  disp(outer.message)\n"
      "end\n";
  for (const auto target : {mpf::TargetLanguage::javascript, mpf::TargetLanguage::cpp}) {
    const auto result = transpile_flow(source, mpf::SourceLanguage::matlab, target);
    REQUIRE(result.success());
    REQUIRE(result.code.find(target == mpf::TargetLanguage::javascript
                                 ? "__mpf_matlab_rethrow(inner)"
                                 : "mpf_runtime::matlab_rethrow(inner)") != std::string::npos);
  }
}

TEST_CASE("Matlab try catch rejects malformed clauses and unsafe exception uses") {
  const auto missing_catch = transpile_flow("try\n  disp(1)\nend\n", mpf::SourceLanguage::matlab,
                                            mpf::TargetLanguage::javascript);
  const auto malformed_binding =
      transpile_flow("try\n  disp(1)\ncatch first second\nend\n", mpf::SourceLanguage::matlab,
                     mpf::TargetLanguage::javascript);
  const auto missing_end =
      transpile_flow("try\ncatch\n", mpf::SourceLanguage::matlab, mpf::TargetLanguage::javascript);
  const auto repeated_catch =
      transpile_flow("try\n  disp(1)\ncatch\ncatch\nend\n", mpf::SourceLanguage::matlab,
                     mpf::TargetLanguage::javascript);
  const auto invalid_error =
      transpile_flow("error(1)\n", mpf::SourceLanguage::matlab, mpf::TargetLanguage::javascript);
  const auto invalid_rethrow = transpile_flow("rethrow('bad')\n", mpf::SourceLanguage::matlab,
                                              mpf::TargetLanguage::javascript);
  const auto unsupported_property =
      transpile_flow("try\n  error('boom')\ncatch ME\n  disp(ME.cause)\nend\n",
                     mpf::SourceLanguage::matlab, mpf::TargetLanguage::javascript);
  const auto escaped_binding =
      transpile_flow("try\n  disp(1)\ncatch ME\n  disp(ME.message)\nend\ndisp(ME.message)\n",
                     mpf::SourceLanguage::matlab, mpf::TargetLanguage::javascript);
  const std::string rebound_source =
      "ME = 1;\ntry\n  error('boom')\ncatch ME\n  disp(ME.message)\nend\n";
  const auto dynamic_rebind =
      transpile_flow(rebound_source, mpf::SourceLanguage::matlab, mpf::TargetLanguage::javascript);
  const auto static_rebind =
      transpile_flow(rebound_source, mpf::SourceLanguage::matlab, mpf::TargetLanguage::cpp);
  REQUIRE(!missing_catch.success());
  REQUIRE(!malformed_binding.success());
  REQUIRE(!missing_end.success());
  REQUIRE(!repeated_catch.success());
  REQUIRE(!invalid_error.success());
  REQUIRE(!invalid_rethrow.success());
  REQUIRE(!unsupported_property.success());
  REQUIRE(!escaped_binding.success());
  REQUIRE(dynamic_rebind.success());
  REQUIRE(!static_rebind.success());
  REQUIRE(missing_catch.diagnostics.front().code == "MPF1200");
  REQUIRE(malformed_binding.diagnostics.front().code == "MPF1200");
  REQUIRE(missing_end.diagnostics.front().code == "MPF1200");
  REQUIRE(repeated_catch.diagnostics.front().code == "MPF1200");
  REQUIRE(invalid_error.diagnostics.front().code == "MPF2057");
  REQUIRE(invalid_rethrow.diagnostics.front().code == "MPF2057");
  REQUIRE(unsupported_property.diagnostics.front().code == "MPF2056");
  REQUIRE(escaped_binding.diagnostics.front().code == "MPF2003");
  REQUIRE(static_rebind.diagnostics.front().code == "MPF2007");
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
