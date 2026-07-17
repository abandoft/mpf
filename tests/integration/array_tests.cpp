#include <string>

#include "mpf/transpiler.hpp"
#include "test_framework.hpp"

namespace {

mpf::TranspileResult transpile_array(const std::string& source, const mpf::SourceLanguage language,
                                     const mpf::TargetLanguage target) {
  mpf::TranspileOptions options;
  options.language = language;
  options.target = target;
  options.emit_source_banner = false;
  return mpf::Transpiler{}.transpile(source, options);
}

}  // namespace

TEST_CASE("TypeScript typed arrays preserve zero-based reads and const-container mutation") {
  const std::string source =
      "const values: number[] = [1, 2, 3];\n"
      "values[1] = 40;\n"
      "console.log(values[0] + values[1] + values[2] - 2);\n";
  const auto javascript =
      transpile_array(source, mpf::SourceLanguage::typescript, mpf::TargetLanguage::javascript);
  const auto cpp =
      transpile_array(source, mpf::SourceLanguage::typescript, mpf::TargetLanguage::cpp);
  REQUIRE(javascript.success());
  REQUIRE(cpp.success());
  REQUIRE(javascript.code.find("__mpf_set(values, [1]") != std::string::npos);
  REQUIRE(cpp.code.find("std::vector<double>{static_cast<double>(1)") != std::string::npos);
  REQUIRE(cpp.code.find("mpf_runtime::index(values, static_cast<std::int64_t>(1), 0, false)") !=
          std::string::npos);
}

TEST_CASE("Python list read, write, negative index, len and sum lower to both backends") {
  const std::string source =
      "values = [1, 2, 3]\n"
      "values[1] = values[0] + 4\n"
      "values[-1] = values[-1] + 7\n"
      "print(len(values), sum(values), values[1], values[-1])\n";
  const auto javascript =
      transpile_array(source, mpf::SourceLanguage::python, mpf::TargetLanguage::javascript);
  const auto cpp = transpile_array(source, mpf::SourceLanguage::python, mpf::TargetLanguage::cpp);
  REQUIRE(javascript.success());
  REQUIRE(cpp.success());
  REQUIRE(javascript.code.find("__mpf_get") != std::string::npos);
  REQUIRE(javascript.code.find("__mpf_set") != std::string::npos);
  REQUIRE(cpp.code.find("mpf_runtime::index") != std::string::npos);
  REQUIRE(cpp.code.find("mpf_runtime::sum") != std::string::npos);
}

TEST_CASE("Matlab space-separated row vectors use one-based indexing") {
  const std::string source = "values = [1 2 3];\nvalues(2) = values(1) + 4;\ndisp(values(2))\n";
  const auto result =
      transpile_array(source, mpf::SourceLanguage::matlab, mpf::TargetLanguage::javascript);
  REQUIRE(result.success());
  REQUIRE(result.code.find("__mpf_get(values, [1], 1, false, true)") != std::string::npos);
  REQUIRE(result.code.find("__mpf_set(values, [2]") != std::string::npos);
}

TEST_CASE("Matlab whitespace can separate signed vector elements") {
  const auto result = transpile_array("values = [1 -2 3];\ndisp(sum(values))\n",
                                      mpf::SourceLanguage::matlab, mpf::TargetLanguage::javascript);
  REQUIRE(result.success());
  REQUIRE(result.code.find("[1, -2, 3]") != std::string::npos);
}

TEST_CASE("Fortran constant-shape arrays retain element type and one-based indexing") {
  const std::string source =
      "program arrays\nimplicit none\ninteger :: values(3) = [1, 2, 3]\n"
      "values(2) = values(1) + 4\nprint *, size(values), sum(values)\nend program arrays\n";
  const auto result =
      transpile_array(source, mpf::SourceLanguage::fortran, mpf::TargetLanguage::cpp);
  REQUIRE(result.success());
  REQUIRE(result.code.find("std::vector<std::int64_t>") != std::string::npos);
  REQUIRE(result.code.find(", 1, false)") != std::string::npos);
}

TEST_CASE("shape analysis rejects static out-of-bounds indexes") {
  const auto python = transpile_array("values = [1, 2]\nprint(values[2])\n",
                                      mpf::SourceLanguage::python, mpf::TargetLanguage::javascript);
  const auto matlab = transpile_array("values = [1 2];\ndisp(values(0))\n",
                                      mpf::SourceLanguage::matlab, mpf::TargetLanguage::javascript);
  const auto integer_limit =
      transpile_array("values = [1, 2]\nprint(values[9223372036854775808])\n",
                      mpf::SourceLanguage::python, mpf::TargetLanguage::javascript);
  REQUIRE(!python.success());
  REQUIRE(!matlab.success());
  REQUIRE(!integer_limit.success());
  REQUIRE(python.diagnostics.front().code == "MPF2021");
  REQUIRE(matlab.diagnostics.front().code == "MPF2021");
  REQUIRE(integer_limit.diagnostics.front().code == "MPF2021");
}

TEST_CASE("heterogeneous Python lists remain valid in JavaScript and fail closed in C++17") {
  const std::string source = "values = [1, 'two']\nprint(len(values))\n";
  const auto javascript =
      transpile_array(source, mpf::SourceLanguage::python, mpf::TargetLanguage::javascript);
  const auto cpp = transpile_array(source, mpf::SourceLanguage::python, mpf::TargetLanguage::cpp);
  REQUIRE(javascript.success());
  REQUIRE(!cpp.success());
  REQUIRE(cpp.diagnostics.front().code == "MPF2020");
}

TEST_CASE("Fortran declared extent must match its array initializer") {
  const auto result =
      transpile_array("program bad\ninteger :: values(3) = [1, 2]\nend program bad\n",
                      mpf::SourceLanguage::fortran, mpf::TargetLanguage::javascript);
  REQUIRE(!result.success());
  REQUIRE(result.diagnostics.front().code == "MPF2024");
}

TEST_CASE("C++17 array inference widens integer elements before emitting all assignments") {
  const auto result = transpile_array("values = [1, 2]\nvalues = [1.5, 2.5]\nprint(sum(values))\n",
                                      mpf::SourceLanguage::python, mpf::TargetLanguage::cpp);
  REQUIRE(result.success());
  REQUIRE(result.code.find("std::vector<double>") != std::string::npos);
  REQUIRE(result.code.find("std::vector<std::int64_t>") == std::string::npos);
}

TEST_CASE("legacy Fortran slash constructors lower through the shared list AST") {
  const auto result = transpile_array(
      "program legacy\ninteger :: values(3) = (/1, 2, 3/)\nprint *, sum(values)\nend program "
      "legacy\n",
      mpf::SourceLanguage::fortran, mpf::TargetLanguage::javascript);
  REQUIRE(result.success());
  REQUIRE(result.code.find("[1, 2, 3]") != std::string::npos);
}

TEST_CASE("uninitialized fixed-size Fortran arrays allocate typed storage in both targets") {
  const std::string source =
      "program storage\ninteger :: values(2)\nvalues(1) = 3\nvalues(2) = 4\n"
      "print *, sum(values)\nend program storage\n";
  const auto javascript =
      transpile_array(source, mpf::SourceLanguage::fortran, mpf::TargetLanguage::javascript);
  const auto cpp = transpile_array(source, mpf::SourceLanguage::fortran, mpf::TargetLanguage::cpp);
  REQUIRE(javascript.success());
  REQUIRE(cpp.success());
  REQUIRE(javascript.code.find("new Array(2).fill(0)") != std::string::npos);
  REQUIRE(cpp.code.find("std::vector<std::int64_t> values(2)") != std::string::npos);
}

TEST_CASE("Python rectangular nested lists preserve shape and chained indexing") {
  const std::string source =
      "matrix = [[1, 2], [3, 4]]\n"
      "matrix[1][0] = 7\n"
      "print(len(matrix), sum(matrix[0]), matrix[1][0])\n";
  const auto javascript =
      transpile_array(source, mpf::SourceLanguage::python, mpf::TargetLanguage::javascript);
  const auto cpp = transpile_array(source, mpf::SourceLanguage::python, mpf::TargetLanguage::cpp);
  REQUIRE(javascript.success());
  REQUIRE(cpp.success());
  REQUIRE(javascript.code.find("__mpf_set(__mpf_get(matrix") != std::string::npos);
  REQUIRE(cpp.code.find("std::vector<std::vector<std::int64_t>>") != std::string::npos);
  REQUIRE(cpp.code.find("mpf_runtime::index(mpf_runtime::index(matrix") != std::string::npos);
}

TEST_CASE("Matlab matrices use row-column and column-major linear indexing") {
  const std::string source =
      "matrix = [1 2; 3 4];\n"
      "matrix(2, 1) = 7;\n"
      "disp(matrix(2, 1) + matrix(3) + length(matrix) + numel(matrix))\n";
  const auto javascript =
      transpile_array(source, mpf::SourceLanguage::matlab, mpf::TargetLanguage::javascript);
  const auto cpp = transpile_array(source, mpf::SourceLanguage::matlab, mpf::TargetLanguage::cpp);
  REQUIRE(javascript.success());
  REQUIRE(cpp.success());
  REQUIRE(javascript.code.find("__mpf_get(matrix, [3], 1, false, true)") != std::string::npos);
  REQUIRE(cpp.code.find("mpf_runtime::matrix_linear_index(matrix") != std::string::npos);
  REQUIRE(cpp.code.find("mpf_runtime::length(matrix)") != std::string::npos);
}

TEST_CASE("Fortran RESHAPE constructs typed rank-two column-major arrays") {
  const std::string source =
      "program matrices\n"
      "integer :: matrix(2, 2) = reshape([1, 2, 3, 4], [2, 2])\n"
      "matrix(2, 1) = 7\n"
      "print *, size(matrix), sum(matrix), matrix(2, 1)\n"
      "end program matrices\n";
  const auto javascript =
      transpile_array(source, mpf::SourceLanguage::fortran, mpf::TargetLanguage::javascript);
  const auto cpp = transpile_array(source, mpf::SourceLanguage::fortran, mpf::TargetLanguage::cpp);
  REQUIRE(javascript.success());
  REQUIRE(cpp.success());
  REQUIRE(javascript.code.find("__mpf_reshape([1, 2, 3, 4], [2, 2])") != std::string::npos);
  REQUIRE(cpp.code.find("mpf_runtime::reshape_column_major") != std::string::npos);
  REQUIRE(cpp.code.find("mpf_runtime::numel(matrix)") != std::string::npos);
}

TEST_CASE("rank-two shape analysis rejects ragged and out-of-bounds matrices") {
  const auto ragged = transpile_array("matrix = [[1, 2], [3]]\nprint(matrix[0][0])\n",
                                      mpf::SourceLanguage::python, mpf::TargetLanguage::cpp);
  const auto matlab_bounds =
      transpile_array("matrix = [1 2; 3 4];\ndisp(matrix(1, 3))\n", mpf::SourceLanguage::matlab,
                      mpf::TargetLanguage::javascript);
  const auto fortran_rank =
      transpile_array("program bad\ninteger :: matrix(2,2)\nprint *, matrix(1)\nend program bad\n",
                      mpf::SourceLanguage::fortran, mpf::TargetLanguage::javascript);
  REQUIRE(!ragged.success());
  REQUIRE(!matlab_bounds.success());
  REQUIRE(!fortran_rank.success());
  REQUIRE(ragged.diagnostics.front().code == "MPF2020");
  REQUIRE(matlab_bounds.diagnostics.front().code == "MPF2021");
  REQUIRE(fortran_rank.diagnostics.front().code == "MPF2025");
}

TEST_CASE("rank-two fixed storage and RESHAPE sizes are validated") {
  const auto storage = transpile_array(
      "program storage\ninteger :: matrix(2,3)\nmatrix(2,3) = 9\nend program storage\n",
      mpf::SourceLanguage::fortran, mpf::TargetLanguage::cpp);
  const auto mismatch = transpile_array(
      "program bad\ninteger :: matrix(2,2) = reshape([1,2,3], [2,2])\nend program bad\n",
      mpf::SourceLanguage::fortran, mpf::TargetLanguage::javascript);
  REQUIRE(storage.success());
  REQUIRE(storage.code.find("std::vector<std::vector<std::int64_t>> matrix(2, ") !=
          std::string::npos);
  REQUIRE(!mismatch.success());
  REQUIRE(mismatch.diagnostics.front().code == "MPF2024");
}

TEST_CASE("Matlab reshape accepts flat and multidimensional sources") {
  const auto reshape = transpile_array("matrix = reshape([1 3 2 4], 2, 2);\ndisp(matrix(1, 2))\n",
                                       mpf::SourceLanguage::matlab, mpf::TargetLanguage::cpp);
  const auto python_sum =
      transpile_array("matrix = [[1, 2], [3, 4]]\nprint(sum(matrix))\n",
                      mpf::SourceLanguage::python, mpf::TargetLanguage::javascript);
  const auto nested_reshape =
      transpile_array("matrix = [1 2; 3 4];\nother = reshape(matrix, 4, 1);\n",
                      mpf::SourceLanguage::matlab, mpf::TargetLanguage::javascript);
  REQUIRE(reshape.success());
  REQUIRE(reshape.code.find("mpf_runtime::reshape_column_major") != std::string::npos);
  REQUIRE(!python_sum.success());
  REQUIRE(nested_reshape.success());
  REQUIRE(nested_reshape.code.find("__mpf_reshape(matrix, [4, 1])") != std::string::npos);
  REQUIRE(python_sum.diagnostics.front().code == "MPF2028");
}

TEST_CASE("general N-dimensional reshape indexing and sections lower to both backends") {
  const std::string source =
      "program tensors\n"
      "integer :: cube(2,2,2) = reshape([1,2,3,4,5,6,7,8], [2,2,2])\n"
      "cube(:,1,2) = [40,2]\n"
      "print *, size(cube), sum(cube), cube(1,1,2)\n"
      "end program tensors\n";
  const auto javascript =
      transpile_array(source, mpf::SourceLanguage::fortran, mpf::TargetLanguage::javascript);
  const auto cpp = transpile_array(source, mpf::SourceLanguage::fortran, mpf::TargetLanguage::cpp);
  REQUIRE(javascript.success());
  REQUIRE(cpp.success());
  REQUIRE(javascript.code.find("__mpf_reshape([1, 2, 3, 4, 5, 6, 7, 8], [2, 2, 2])") !=
          std::string::npos);
  REQUIRE(javascript.code.find("__mpf_set_section(cube") != std::string::npos);
  REQUIRE(cpp.code.find("reshape_column_major_nd") != std::string::npos);
  REQUIRE(cpp.code.find("assign_section_nd(cube") != std::string::npos);
  REQUIRE(cpp.code.find("std::vector<std::vector<std::vector<std::int64_t>>>") !=
          std::string::npos);
}

TEST_CASE("Python slices preserve exclusive stops, defaults and negative steps") {
  const std::string source =
      "values = [0, 1, 2, 3, 4, 5]\n"
      "forward = values[1:100:2]\n"
      "reverse = values[::-2]\n"
      "print(len(forward), sum(forward), len(reverse), sum(reverse))\n";
  const auto javascript =
      transpile_array(source, mpf::SourceLanguage::python, mpf::TargetLanguage::javascript);
  const auto cpp = transpile_array(source, mpf::SourceLanguage::python, mpf::TargetLanguage::cpp);
  REQUIRE(javascript.success());
  REQUIRE(cpp.success());
  REQUIRE(javascript.code.find("__mpf_section(values") != std::string::npos);
  REQUIRE(javascript.code.find("inclusive: false") != std::string::npos);
  REQUIRE(cpp.code.find("mpf_runtime::slice(values") != std::string::npos);
  REQUIRE(cpp.code.find("std::optional<std::int64_t>") != std::string::npos);
}

TEST_CASE("Matlab colon selection lowers rows, columns and rectangular blocks") {
  const std::string source =
      "matrix = [1 2 3; 4 5 6];\n"
      "row = matrix(2, :);\n"
      "column = matrix(:, 2);\n"
      "block = matrix(:, 1:2);\n"
      "linear = matrix(:);\n"
      "odd = matrix(1, 1:2:3);\n"
      "disp(sum(row) + sum(column) + numel(block) + linear(2) + sum(odd))\n";
  const auto javascript =
      transpile_array(source, mpf::SourceLanguage::matlab, mpf::TargetLanguage::javascript);
  const auto cpp = transpile_array(source, mpf::SourceLanguage::matlab, mpf::TargetLanguage::cpp);
  REQUIRE(javascript.success());
  REQUIRE(cpp.success());
  REQUIRE(javascript.code.find("inclusive: true") != std::string::npos);
  REQUIRE(cpp.code.find("mpf_runtime::column(") != std::string::npos);
  REQUIRE(cpp.code.find("mpf_runtime::columns(") != std::string::npos);
  REQUIRE(cpp.code.find("mpf_runtime::flatten_column_major(") != std::string::npos);
}

TEST_CASE("Fortran array sections retain rank and declared shape") {
  const std::string source =
      "program sections\n"
      "integer :: matrix(2,3) = reshape([1,4,2,5,3,6], [2,3])\n"
      "integer :: row(3)\n"
      "integer :: column(2)\n"
      "integer :: block(2,2)\n"
      "integer :: reverse(3)\n"
      "row = matrix(2,:)\n"
      "column = matrix(:,2)\n"
      "block = matrix(:,1:2)\n"
      "reverse = matrix(2,3:1:-1)\n"
      "print *, sum(row) + sum(column) + size(block) + reverse(1)\n"
      "end program sections\n";
  const auto javascript =
      transpile_array(source, mpf::SourceLanguage::fortran, mpf::TargetLanguage::javascript);
  const auto cpp = transpile_array(source, mpf::SourceLanguage::fortran, mpf::TargetLanguage::cpp);
  REQUIRE(javascript.success());
  REQUIRE(cpp.success());
  REQUIRE(javascript.code.find("__mpf_section(matrix") != std::string::npos);
  REQUIRE(cpp.code.find("mpf_runtime::columns(") != std::string::npos);
}

TEST_CASE("slice analysis rejects zero steps, scalar Python replacement and invalid bounds") {
  const auto zero_step =
      transpile_array("values = [1, 2, 3]\nprint(values[::0])\n", mpf::SourceLanguage::python,
                      mpf::TargetLanguage::javascript);
  const auto extreme_step =
      transpile_array("values = [1, 2, 3]\nprint(values[::-9223372036854775808])\n",
                      mpf::SourceLanguage::python, mpf::TargetLanguage::javascript);
  const auto section_write =
      transpile_array("values = [1, 2, 3]\nvalues[1:] = 0\n", mpf::SourceLanguage::python,
                      mpf::TargetLanguage::javascript);
  const auto matlab_bounds =
      transpile_array("values = [1 2 3];\ndisp(values(2:5))\n", mpf::SourceLanguage::matlab,
                      mpf::TargetLanguage::javascript);
  const auto empty_bounds =
      transpile_array("values = []\nprint(values[0])\n", mpf::SourceLanguage::python,
                      mpf::TargetLanguage::javascript);
  const auto fortran_shape = transpile_array(
      "program bad\ninteger :: matrix(2,3)\ninteger :: row(2)\n"
      "row = matrix(2,:)\nend program bad\n",
      mpf::SourceLanguage::fortran, mpf::TargetLanguage::javascript);
  REQUIRE(!zero_step.success());
  REQUIRE(!extreme_step.success());
  REQUIRE(!section_write.success());
  REQUIRE(!matlab_bounds.success());
  REQUIRE(!empty_bounds.success());
  REQUIRE(!fortran_shape.success());
  REQUIRE(zero_step.diagnostics.front().code == "MPF2030");
  REQUIRE(extreme_step.diagnostics.front().code == "MPF2027");
  REQUIRE(section_write.diagnostics.front().code == "MPF2031");
  REQUIRE(matlab_bounds.diagnostics.front().code == "MPF2021");
  REQUIRE(empty_bounds.diagnostics.front().code == "MPF2021");
  REQUIRE(fortran_shape.diagnostics.front().code == "MPF2024");
}

TEST_CASE("Python slice assignment supports resize and equal-length extended replacement") {
  const std::string source =
      "values = [1, 2, 3, 4]\n"
      "values[1:3] = [8, 9, 10]\n"
      "values[::2] = [5, 6, 7]\n"
      "print(len(values), sum(values))\n";
  const auto javascript =
      transpile_array(source, mpf::SourceLanguage::python, mpf::TargetLanguage::javascript);
  const auto cpp = transpile_array(source, mpf::SourceLanguage::python, mpf::TargetLanguage::cpp);
  REQUIRE(javascript.success());
  REQUIRE(cpp.success());
  REQUIRE(javascript.code.find("__mpf_set_section(values") != std::string::npos);
  REQUIRE(cpp.code.find("mpf_runtime::assign_slice(values") != std::string::npos);
}

TEST_CASE("Matlab and Fortran section assignment support block vector and scalar replacement") {
  const std::string matlab_source =
      "matrix = [1 2 3; 4 5 6];\n"
      "matrix(:) = [1 4 2 5 3 6];\n"
      "matrix(:, 1:2) = [1 2; 3 4];\n"
      "matrix(:, 2) = [8; 9];\n"
      "matrix(:, 3) = 7;\n"
      "matrix(1, :) = [4 5 6];\n"
      "disp(matrix(1,1) + matrix(2,2) + matrix(1,3))\n";
  const std::string fortran_source =
      "program assignments\n"
      "integer :: matrix(2,3) = reshape([1,4,2,5,3,6], [2,3])\n"
      "matrix(:,1:2) = reshape([1,3,2,4], [2,2])\n"
      "matrix(:,2) = [8,9]\n"
      "matrix(:,3) = 7\n"
      "matrix(1,:) = [4,5,6]\n"
      "print *, matrix(1,1) + matrix(2,2) + matrix(1,3)\n"
      "end program assignments\n";
  for (const auto language : {mpf::SourceLanguage::matlab, mpf::SourceLanguage::fortran}) {
    const auto& source = language == mpf::SourceLanguage::matlab ? matlab_source : fortran_source;
    const auto javascript = transpile_array(source, language, mpf::TargetLanguage::javascript);
    const auto cpp = transpile_array(source, language, mpf::TargetLanguage::cpp);
    REQUIRE(javascript.success());
    REQUIRE(cpp.success());
    REQUIRE(javascript.code.find("__mpf_set_section") != std::string::npos);
    REQUIRE(cpp.code.find("mpf_runtime::assign_block(matrix") != std::string::npos);
    REQUIRE(cpp.code.find("mpf_runtime::assign_column(matrix") != std::string::npos);
    if (language == mpf::SourceLanguage::matlab) {
      REQUIRE(cpp.code.find("mpf_runtime::assign_linear_column_major(matrix") != std::string::npos);
    }
  }
}

TEST_CASE("section assignment rejects nonconforming source-language shapes") {
  const auto python_extended =
      transpile_array("values = [1, 2, 3]\nvalues[::2] = [8]\n", mpf::SourceLanguage::python,
                      mpf::TargetLanguage::javascript);
  const auto matlab =
      transpile_array("matrix = [1 2 3; 4 5 6];\nmatrix(:, 1:2) = [1 2 3; 4 5 6];\n",
                      mpf::SourceLanguage::matlab, mpf::TargetLanguage::javascript);
  const auto fortran = transpile_array(
      "program bad\ninteger :: matrix(2,3)\n"
      "matrix(:,1:2) = reshape([1,2,3,4,5,6], [2,3])\nend program bad\n",
      mpf::SourceLanguage::fortran, mpf::TargetLanguage::javascript);
  REQUIRE(!python_extended.success());
  REQUIRE(!matlab.success());
  REQUIRE(!fortran.success());
  REQUIRE(python_extended.diagnostics.front().code == "MPF2031");
  REQUIRE(matlab.diagnostics.front().code == "MPF2031");
  REQUIRE(fortran.diagnostics.front().code == "MPF2031");
}

TEST_CASE("section assignment through a temporary and C++ rank changes fail closed") {
  const auto temporary =
      transpile_array("matrix = [[1,2],[3,4]]\nmatrix[:][0] = [5,6]\n", mpf::SourceLanguage::python,
                      mpf::TargetLanguage::javascript);
  const auto javascript =
      transpile_array("matrix = [[1,2],[3,4]]\nmatrix[:] = [1,2]\n", mpf::SourceLanguage::python,
                      mpf::TargetLanguage::javascript);
  const auto cpp = transpile_array("matrix = [[1,2],[3,4]]\nmatrix[:] = [1,2]\n",
                                   mpf::SourceLanguage::python, mpf::TargetLanguage::cpp);
  REQUIRE(!temporary.success());
  REQUIRE(temporary.diagnostics.front().code == "MPF2029");
  REQUIRE(javascript.success());
  REQUIRE(!cpp.success());
  REQUIRE(cpp.diagnostics.front().code == "MPF2020");
}

TEST_CASE("Python section element type changes remain JavaScript-only") {
  const std::string source =
      "values = [1, 2]\n"
      "values[:] = [1.5, 2.5]\n"
      "print(sum(values))\n";
  const auto javascript =
      transpile_array(source, mpf::SourceLanguage::python, mpf::TargetLanguage::javascript);
  const auto cpp = transpile_array(source, mpf::SourceLanguage::python, mpf::TargetLanguage::cpp);
  REQUIRE(javascript.success());
  REQUIRE(!cpp.success());
  REQUIRE(cpp.diagnostics.front().code == "MPF2020");
}

TEST_CASE("dynamic Python slice bounds retain runtime shape without false mismatch diagnostics") {
  const std::string source =
      "values = [1, 2, 3, 4]\n"
      "stop = 3\n"
      "part = values[:stop]\n"
      "print(len(part), sum(part))\n";
  const auto javascript =
      transpile_array(source, mpf::SourceLanguage::python, mpf::TargetLanguage::javascript);
  const auto cpp = transpile_array(source, mpf::SourceLanguage::python, mpf::TargetLanguage::cpp);
  REQUIRE(javascript.success());
  REQUIRE(cpp.success());
  REQUIRE(javascript.code.find("stop: stop") != std::string::npos);
  REQUIRE(cpp.code.find("static_cast<std::int64_t>(stop)") != std::string::npos);
}

TEST_CASE("Python rectangular tensor shape supports retained and consumed dimensions") {
  const std::string source =
      "cube = [[[1, 2], [3, 4]], [[5, 6], [7, 8]]]\n"
      "plane = cube[1]\n"
      "row = cube[0][1]\n"
      "cube[1][0][1] = 9\n"
      "print(len(cube), len(plane), sum(row), cube[1][0][1])\n";
  const auto javascript =
      transpile_array(source, mpf::SourceLanguage::python, mpf::TargetLanguage::javascript);
  const auto cpp = transpile_array(source, mpf::SourceLanguage::python, mpf::TargetLanguage::cpp);
  REQUIRE(javascript.success());
  REQUIRE(cpp.success());
  REQUIRE(cpp.code.find("std::vector<std::vector<std::vector<std::int64_t>>>") !=
          std::string::npos);
}

TEST_CASE("Fortran assumed-shape arrays preserve reference mutation in both backends") {
  const std::string source =
      "program reference\n"
      "integer :: values(3) = [1,2,3]\n"
      "integer :: output(2)\n"
      "integer :: matrix(2,2) = reshape([1,2,3,4], [2,2])\n"
      "call scale(values)\n"
      "call fill(output)\n"
      "call corner(matrix)\n"
      "contains\n"
      "subroutine scale(items)\n"
      "integer, intent(inout) :: items(:)\n"
      "items(2) = 8\n"
      "end subroutine scale\n"
      "subroutine fill(items)\n"
      "integer, intent(out) :: items(:)\n"
      "items(1) = 20\n"
      "items(2) = 22\n"
      "end subroutine fill\n"
      "subroutine corner(items)\n"
      "integer, intent(inout) :: items(:,:)\n"
      "items(2,2) = 42\n"
      "end subroutine corner\n"
      "end program reference\n";
  const auto javascript =
      transpile_array(source, mpf::SourceLanguage::fortran, mpf::TargetLanguage::javascript);
  const auto cpp = transpile_array(source, mpf::SourceLanguage::fortran, mpf::TargetLanguage::cpp);
  REQUIRE(javascript.success());
  REQUIRE(cpp.success());
  REQUIRE(javascript.code.find("items.value") != std::string::npos);
  REQUIRE(javascript.code.find("{ value: output }") != std::string::npos);
  REQUIRE(javascript.code.find("new Array(184467") == std::string::npos);
  REQUIRE(cpp.code.find("auto scale(T0& items)") != std::string::npos);
  REQUIRE(cpp.code.find("auto fill(T0& items)") != std::string::npos);
}

TEST_CASE("Fortran dummy array rank and extent mismatches fail closed") {
  const auto rank = transpile_array(
      "program bad\ninteger :: matrix(2,2)\ncall consume(matrix)\ncontains\n"
      "subroutine consume(values)\ninteger, intent(in) :: values(:)\n"
      "end subroutine consume\nend program bad\n",
      mpf::SourceLanguage::fortran, mpf::TargetLanguage::javascript);
  const auto extent = transpile_array(
      "program bad\ninteger :: values(2)\ncall consume(values)\ncontains\n"
      "subroutine consume(items)\ninteger, intent(in) :: items(3)\n"
      "end subroutine consume\nend program bad\n",
      mpf::SourceLanguage::fortran, mpf::TargetLanguage::javascript);
  const auto section = transpile_array(
      "program sections\ninteger :: values(3) = [1,2,3]\ncall mutate(values(1:2))\ncontains\n"
      "subroutine mutate(items)\ninteger, intent(inout) :: items(:)\n"
      "items(1) = 9\nend subroutine mutate\nend program sections\n",
      mpf::SourceLanguage::fortran, mpf::TargetLanguage::javascript);
  const auto non_dummy =
      transpile_array("program bad\ninteger :: values(:)\nend program bad\n",
                      mpf::SourceLanguage::fortran, mpf::TargetLanguage::javascript);
  REQUIRE(!rank.success());
  REQUIRE(!extent.success());
  REQUIRE(section.success());
  REQUIRE(!non_dummy.success());
  REQUIRE(rank.diagnostics.front().code == "MPF2039");
  REQUIRE(extent.diagnostics.front().code == "MPF2039");
  REQUIRE(section.code.find("__mpf_set_section") != std::string::npos);
  REQUIRE(non_dummy.diagnostics.front().code == "MPF2039");
}

TEST_CASE("Fortran writable element and section actuals lower independently per backend") {
  const std::string source =
      "program references\n"
      "integer :: values(5) = [1,2,3,4,5]\n"
      "integer :: result\n"
      "call bump(values(2))\n"
      "result = adjust(values(1:5:2))\n"
      "contains\n"
      "subroutine bump(value)\n"
      "integer, intent(inout) :: value\n"
      "value = value + 1\n"
      "end subroutine bump\n"
      "integer function adjust(items) result(total)\n"
      "integer, intent(inout) :: items(:)\n"
      "items(1) = items(1) + 1\n"
      "total = sum(items)\n"
      "end function adjust\n"
      "end program references\n";
  const auto javascript =
      transpile_array(source, mpf::SourceLanguage::fortran, mpf::TargetLanguage::javascript);
  const auto cpp = transpile_array(source, mpf::SourceLanguage::fortran, mpf::TargetLanguage::cpp);
  REQUIRE(javascript.success());
  REQUIRE(cpp.success());
  REQUIRE(javascript.code.find("__mpf_set(values, [2]") != std::string::npos);
  REQUIRE(javascript.code.find("__mpf_set_section(values") != std::string::npos);
  REQUIRE(cpp.code.find("mpf_internal_section_reference_") != std::string::npos);
  REQUIRE(cpp.code.find("mpf_runtime::assign_slice(values") != std::string::npos);
  REQUIRE(cpp.code.find("mpf_internal_call_result_") != std::string::npos);
}
