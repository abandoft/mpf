#include <algorithm>
#include <string>
#include <vector>

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

TEST_CASE("Matlab complex scalars arrays and conjugate transpose lower independently per target") {
  const std::string source =
      "z = 2i;\n"
      "w = (1 + z) * (3 - 4j);\n"
      "negated = -w;\n"
      "positive = +w;\n"
      "values = [1+2i 3-4i; 5i 6];\n"
      "plain = values.';\n"
      "hermitian = values';\n"
      "mixed = values + [1 2; 3 4];\n"
      "mutated = [1 2];\n"
      "mutated(2) = 3i;\n"
      "shaped = reshape(mutated, 2, 1);\n"
      "disp(real(w), imag(w), real(plain(2,1)), imag(plain(2,1)), "
      "real(hermitian(2,1)), imag(hermitian(2,1)), real(mixed(1,2)), imag(mixed(1,2)))\n";
  const auto javascript =
      transpile_array(source, mpf::SourceLanguage::matlab, mpf::TargetLanguage::javascript);
  const auto cpp = transpile_array(source, mpf::SourceLanguage::matlab, mpf::TargetLanguage::cpp);
  REQUIRE(javascript.success());
  REQUIRE(cpp.success());
  REQUIRE(javascript.code.find("function __mpf_complex_multiply") != std::string::npos);
  REQUIRE(javascript.code.find("__mpf_complex_negate(w)") != std::string::npos);
  REQUIRE(javascript.code.find("__mpf_as_complex(w)") != std::string::npos);
  REQUIRE(javascript.code.find("__mpf_matlab_complex_add(values") != std::string::npos);
  REQUIRE(javascript.code.find("__mpf_matlab_ctranspose(values)") != std::string::npos);
  REQUIRE(javascript.code.find("std::complex") == std::string::npos);
  REQUIRE(cpp.code.find("#include <complex>") != std::string::npos);
  REQUIRE(cpp.code.find("std::complex<double>") != std::string::npos);
  REQUIRE(cpp.code.find("mpf_runtime::complex_multiply") != std::string::npos);
  REQUIRE(cpp.code.find("mpf_runtime::matlab_complex_add") != std::string::npos);
  REQUIRE(cpp.code.find("mpf_runtime::matlab_ctranspose(values") != std::string::npos);
  REQUIRE(cpp.code.find("std::vector<std::complex<double>> mutated") != std::string::npos);
  REQUIRE(cpp.code.find("__mpf_complex") == std::string::npos);
  for (const auto* result : {&javascript, &cpp}) {
    REQUIRE(std::any_of(result->source_map.segments.begin(), result->source_map.segments.end(),
                        [](const auto& segment) { return segment.original_line == 2U; }));
    REQUIRE(std::any_of(result->source_map.segments.begin(), result->source_map.segments.end(),
                        [](const auto& segment) { return segment.original_line == 7U; }));
    REQUIRE(std::any_of(result->source_map.segments.begin(), result->source_map.segments.end(),
                        [](const auto& segment) { return segment.original_line == 12U; }));
  }

  const auto real_only =
      transpile_array("value = abs(-2);\ndisp(value)\n", mpf::SourceLanguage::matlab,
                      mpf::TargetLanguage::javascript);
  REQUIRE(real_only.success());
  REQUIRE(real_only.code.find("Math.abs") != std::string::npos);
  REQUIRE(real_only.code.find("__mpf_complex_tag") == std::string::npos);
}

TEST_CASE("Matlab complex square matrix kernels preserve target contracts and source maps") {
  const std::string source =
      "hermitian = [4 2i; -2i 5];\n"
      "product = hermitian * hermitian;\n"
      "hermitian_solution = hermitian \\ [6+8i; 12-7i];\n"
      "dense = [1i 0; 0 -1i];\n"
      "dense_solution = dense \\ [-1+1i; -1-2i];\n"
      "right_solution = [2 8-3i] / hermitian;\n"
      "powered = hermitian ^ 2;\n"
      "inverse = hermitian ^ -1;\n"
      "disp(real(product(1,1)), imag(hermitian_solution(1)), "
      "real(dense_solution(1)), imag(right_solution(2)), real(powered(2,2)), "
      "real(inverse(1,1)))\n";
  const auto javascript =
      transpile_array(source, mpf::SourceLanguage::matlab, mpf::TargetLanguage::javascript);
  const auto cpp = transpile_array(source, mpf::SourceLanguage::matlab, mpf::TargetLanguage::cpp);
  REQUIRE(javascript.success());
  REQUIRE(cpp.success());
  REQUIRE(javascript.code.find("function __mpf_matlab_complex_mtimes") != std::string::npos);
  REQUIRE(javascript.code.find("function __mpf_matlab_complex_cholesky_factor") !=
          std::string::npos);
  REQUIRE(javascript.code.find("function __mpf_matlab_complex_lu_factor") != std::string::npos);
  REQUIRE(javascript.code.find("__mpf_matlab_mrdivide_structured_complex_square") !=
          std::string::npos);
  REQUIRE(javascript.code.find("__mpf_matlab_complex_mpower") != std::string::npos);
  REQUIRE(cpp.code.find("mpf_runtime::matlab_complex_mtimes") != std::string::npos);
  REQUIRE(cpp.code.find("matlab_complex_cholesky_factor") != std::string::npos);
  REQUIRE(cpp.code.find("matlab_complex_lu_factor") != std::string::npos);
  REQUIRE(cpp.code.find("mpf_runtime::matlab_mrdivide_structured_complex_square") !=
          std::string::npos);
  REQUIRE(cpp.code.find("mpf_runtime::matlab_complex_mpower") != std::string::npos);
  for (const auto* result : {&javascript, &cpp}) {
    for (const auto line : {2U, 3U, 5U, 6U, 7U, 8U}) {
      REQUIRE(std::any_of(result->source_map.segments.begin(), result->source_map.segments.end(),
                          [line](const auto& segment) { return segment.original_line == line; }));
    }
  }

  const std::string real_source =
      "left = [1 2; 3 4];\nright = left * left;\nsolution = left \\ [1; 2];\n"
      "disp(right(1,1), solution(1))\n";
  const auto real_javascript =
      transpile_array(real_source, mpf::SourceLanguage::matlab, mpf::TargetLanguage::javascript);
  const auto real_cpp =
      transpile_array(real_source, mpf::SourceLanguage::matlab, mpf::TargetLanguage::cpp);
  REQUIRE(real_javascript.success());
  REQUIRE(real_cpp.success());
  REQUIRE(real_javascript.code.find("__mpf_matlab_complex_mtimes") == std::string::npos);
  REQUIRE(real_javascript.code.find("__mpf_complex_tag") == std::string::npos);
  REQUIRE(real_cpp.code.find("matlab_complex_mtimes") == std::string::npos);
  REQUIRE(real_cpp.code.find("#include <complex>") == std::string::npos);
}

TEST_CASE("Matlab function boundaries preserve dynamic scalar and array numeric complexity") {
  const std::string source =
      "scaled = scale_complex(1+2i);\n"
      "values = scale_complex_elements([1+2i 3]);\n"
      "disp(real(scaled), imag(scaled), imag(values(1)), real(values(2)))\n"
      "function result = scale_complex(input)\n"
      "result = input * 2;\n"
      "end\n"
      "function result = scale_complex_elements(input)\n"
      "result = input .* 2;\n"
      "end\n";
  const auto javascript =
      transpile_array(source, mpf::SourceLanguage::matlab, mpf::TargetLanguage::javascript);
  const auto cpp = transpile_array(source, mpf::SourceLanguage::matlab, mpf::TargetLanguage::cpp);
  REQUIRE(javascript.success());
  REQUIRE(cpp.success());
  REQUIRE(javascript.code.find("__mpf_numeric_multiply(input, 2)") != std::string::npos);
  REQUIRE(javascript.code.find("__mpf_matlab_numeric_multiply_runtime(input, 2)") !=
          std::string::npos);
  REQUIRE(cpp.code.find("mpf_runtime::numeric_multiply(input, 2)") != std::string::npos);
  REQUIRE(cpp.code.find("mpf_runtime::matlab_numeric_multiply_runtime(input, 2)") !=
          std::string::npos);
  REQUIRE(cpp.code.find("std::decay_t<decltype(mpf_runtime::numeric_multiply(input, 2))>") !=
          std::string::npos);
  for (const auto* result : {&javascript, &cpp}) {
    REQUIRE(std::any_of(result->source_map.segments.begin(), result->source_map.segments.end(),
                        [](const auto& segment) { return segment.original_line == 5U; }));
    REQUIRE(std::any_of(result->source_map.segments.begin(), result->source_map.segments.end(),
                        [](const auto& segment) { return segment.original_line == 8U; }));
  }
}

TEST_CASE("Matlab compatible sizes lower through explicit N-dimensional broadcast plans") {
  const std::string source =
      "matrix = [10 20 30; 40 50 60];\n"
      "row = [1 2 3];\n"
      "column = [100; 200];\n"
      "matrix_result = matrix + row;\n"
      "outer_result = column + row;\n"
      "cube = reshape([1 2 3 4 5 6], 2, 1, 3);\n"
      "pages = reshape([10 20 30], 1, 1, 3);\n"
      "cube_result = cube + pages;\n"
      "explicit_row = reshape([1 2 3], 1, 3);\n"
      "row_equivalent = row + explicit_row;\n"
      "matrix_pages = reshape([1 2 3 4 5 6], 2, 3);\n"
      "singleton_cube = reshape([10 11 12 13 14 15], 2, 3, 1);\n"
      "rank_equivalent = matrix_pages + singleton_cube;\n"
      "disp(matrix_result(2, 3) + outer_result(2, 3) + cube_result(2, 1, 3) + "
      "row_equivalent(1, 3) + rank_equivalent(2, 3, 1))\n";
  const auto javascript =
      transpile_array(source, mpf::SourceLanguage::matlab, mpf::TargetLanguage::javascript);
  const auto cpp = transpile_array(source, mpf::SourceLanguage::matlab, mpf::TargetLanguage::cpp);
  REQUIRE(javascript.success());
  REQUIRE(cpp.success());
  REQUIRE(javascript.code.find("__mpf_matlab_add(matrix, row, [2, 3], [1, 3], [2, 3])") !=
          std::string::npos);
  REQUIRE(javascript.code.find("__mpf_matlab_add(column, row, [2, 1], [1, 3], [2, 3])") !=
          std::string::npos);
  REQUIRE(javascript.code.find("__mpf_matlab_add(row, explicit_row, [1, 3], [1, 3], [1, 3])") !=
          std::string::npos);
  REQUIRE(cpp.code.find("mpf_runtime::matlab_add_broadcast") != std::string::npos);
  REQUIRE(cpp.code.find("std::array<std::size_t, 3>{2, 1, 3}") != std::string::npos);
}

TEST_CASE("Matlab runtime-sized compatible operations derive typed operand shapes") {
  const std::string source =
      "column = [1; 2];\n"
      "row = [10 20 30];\n"
      "[expanded, mask, scaled] = expand_dynamic(column, row);\n"
      "disp(expanded(2, 3) + sum(mask) + scaled(1, 2))\n"
      "[scalar_sum, scalar_mask, scalar_scaled] = expand_dynamic(20, 22);\n"
      "disp(scalar_sum + scalar_mask + scalar_scaled)\n"
      "logical_total = sum([true true false]);\n"
      "disp(logical_total)\n"
      "function [expanded, mask, scaled] = expand_dynamic(left, right)\n"
      "expanded = left + right;\n"
      "mask = left < right;\n"
      "scaled = expanded .* 2;\n"
      "end\n";
  const auto javascript =
      transpile_array(source, mpf::SourceLanguage::matlab, mpf::TargetLanguage::javascript);
  const auto cpp = transpile_array(source, mpf::SourceLanguage::matlab, mpf::TargetLanguage::cpp);
  REQUIRE(javascript.success());
  REQUIRE(cpp.success());
  REQUIRE(javascript.code.find("__mpf_matlab_numeric_add_runtime(left, right)") !=
          std::string::npos);
  REQUIRE(javascript.code.find("__mpf_matlab_less_runtime(left, right)") != std::string::npos);
  REQUIRE(javascript.code.find("__mpf_matlab_numeric_multiply_runtime(expanded, 2)") !=
          std::string::npos);
  REQUIRE(cpp.code.find("mpf_runtime::matlab_numeric_add_runtime(left, right)") !=
          std::string::npos);
  REQUIRE(cpp.code.find("mpf_runtime::matlab_less_runtime(left, right)") != std::string::npos);
  REQUIRE(cpp.code.find("mpf_runtime::matlab_numeric_multiply_runtime(expanded, 2)") !=
          std::string::npos);
  REQUIRE(cpp.code.find("using sum_result_t") != std::string::npos);
  REQUIRE(cpp.code.find("std::int64_t logical_total") != std::string::npos);
  REQUIRE(javascript.code.find("MPF Matlab broadcast sizes are incompatible") != std::string::npos);
  REQUIRE(cpp.code.find("MPF Matlab broadcast operand must be rectangular") != std::string::npos);
  for (const auto* result : {&javascript, &cpp}) {
    REQUIRE(std::any_of(result->source_map.segments.begin(), result->source_map.segments.end(),
                        [](const auto& segment) { return segment.original_line == 11U; }));
    REQUIRE(std::any_of(result->source_map.segments.begin(), result->source_map.segments.end(),
                        [](const auto& segment) { return segment.original_line == 12U; }));
  }

  const std::string minimal =
      "function result = expand_dynamic(left, right)\n"
      "result = left + right;\n"
      "end\n";
  const auto minimal_javascript =
      transpile_array(minimal, mpf::SourceLanguage::matlab, mpf::TargetLanguage::javascript);
  const auto minimal_cpp =
      transpile_array(minimal, mpf::SourceLanguage::matlab, mpf::TargetLanguage::cpp);
  REQUIRE(minimal_javascript.success());
  REQUIRE(minimal_cpp.success());
  REQUIRE(minimal_javascript.code.find("function __mpf_matlab_add_runtime") != std::string::npos);
  REQUIRE(minimal_cpp.code.find("auto matlab_add_runtime") != std::string::npos);
}

TEST_CASE("Matlab transpose identities preserve vector and matrix shape semantics") {
  const std::string source =
      "matrix = [1 2 3; 4 5 6];\n"
      "plain = matrix.';\n"
      "conjugating = matrix';\n"
      "row = [7 8];\n"
      "column = row';\n"
      "disp(plain(3, 2) + conjugating(2, 1) + column(2, 1))\n";
  const auto javascript =
      transpile_array(source, mpf::SourceLanguage::matlab, mpf::TargetLanguage::javascript);
  const auto cpp = transpile_array(source, mpf::SourceLanguage::matlab, mpf::TargetLanguage::cpp);
  REQUIRE(javascript.success());
  REQUIRE(cpp.success());
  REQUIRE(javascript.code.find("__mpf_matlab_transpose(matrix)") != std::string::npos);
  REQUIRE(javascript.code.find("__mpf_matlab_transpose(row)") != std::string::npos);
  REQUIRE(cpp.code.find("mpf_runtime::matlab_transpose(matrix,") != std::string::npos);

  const auto nd = transpile_array("cube = reshape([1 2 3 4], 2, 1, 2);\nbad = cube';\n",
                                  mpf::SourceLanguage::matlab, mpf::TargetLanguage::javascript);
  REQUIRE(!nd.success());
  REQUIRE(nd.diagnostics.front().code == "MPF2047");
}

TEST_CASE("Matlab empty arrays preserve zero extents across shape-changing operations") {
  const std::string source =
      "empty = [];\n"
      "grown = [];\n"
      "grown(3) = 7;\n"
      "matrix = reshape([], 0, 5);\n"
      "transposed = matrix.';\n"
      "scaled = matrix + 2;\n"
      "selected = matrix([], :);\n"
      "expanded = reshape([], 0, 4);\n"
      "expanded(1, 2) = 9;\n"
      "disp(length(empty), length(grown), grown(3), length(matrix), numel(matrix), "
      "length(transposed), length(scaled), length(selected), length(expanded), expanded(1, 2))\n";
  const auto javascript =
      transpile_array(source, mpf::SourceLanguage::matlab, mpf::TargetLanguage::javascript);
  const auto cpp = transpile_array(source, mpf::SourceLanguage::matlab, mpf::TargetLanguage::cpp);
  REQUIRE(javascript.success());
  REQUIRE(cpp.success());
  REQUIRE(javascript.code.find("__mpf_empty_array([0, 0])") != std::string::npos);
  REQUIRE(javascript.code.find("__mpf_reshape(__mpf_empty_array([0, 0]), [0, 5])") !=
          std::string::npos);
  REQUIRE(javascript.code.find("__mpf_record_shape") != std::string::npos);
  REQUIRE(javascript.code.find(", 0, [1, 3]);") != std::string::npos);
  REQUIRE(javascript.code.find("MPF column-major coordinates require positive safe extents") !=
          std::string::npos);
  REQUIRE(
      javascript.code.find("const coordinates = __mpf_column_major_coordinates(linear, shape);") !=
      std::string::npos);
  REQUIRE(cpp.code.find("std::vector<std::vector<double>>{}") != std::string::npos);
  REQUIRE(cpp.code.find("std::array<std::size_t, 2>{0, 5}") != std::string::npos);
  REQUIRE(cpp.code.find("std::array<std::size_t, 2>{5, 0}") != std::string::npos);
  REQUIRE(cpp.code.find("MPF column-major coordinates require nonzero extents") !=
          std::string::npos);
  REQUIRE(cpp.code.find("linear % shape[axis]") == std::string::npos);
  REQUIRE(javascript.source_map.segments.size() >= 9U);
  REQUIRE(cpp.source_map.segments.size() >= 9U);
}

TEST_CASE("Matlab sparse CSC square solves preserve storage and target isolation") {
  const std::string source =
      "tridiagonal = sparse([2 1 0; 0 3 1; 0 0 4]);\n"
      "dense_solution = tridiagonal \\ [4; 9; 12];\n"
      "pivoted = sparse([0 2 0; 1 3 1; 0 1 4]);\n"
      "sparse_solution = pivoted \\ sparse([4; 10; 14]);\n"
      "coefficient = sparse([1 2 0; 0 3 1; 2 0 4]);\n"
      "quotient = sparse([7 8 14; 16 23 29]) / coefficient;\n"
      "dense_sparse_solution = full(sparse_solution);\n"
      "dense_quotient = full(quotient);\n"
      "disp(issparse(dense_solution), issparse(sparse_solution), nnz(pivoted), "
      "dense_sparse_solution(3), dense_quotient(2, 3))\n";
  const auto javascript =
      transpile_array(source, mpf::SourceLanguage::matlab, mpf::TargetLanguage::javascript);
  const auto cpp = transpile_array(source, mpf::SourceLanguage::matlab, mpf::TargetLanguage::cpp);
  REQUIRE(javascript.success());
  REQUIRE(cpp.success());
  REQUIRE(javascript.code.find("const __mpf_sparse_csc_tag") != std::string::npos);
  REQUIRE(javascript.code.find("function __mpf_sparse_tridiagonal_factor") != std::string::npos);
  REQUIRE(javascript.code.find("function __mpf_sparse_row_lu_factor") != std::string::npos);
  REQUIRE(javascript.code.find("__mpf_matlab_mldivide_sparse_real_square") != std::string::npos);
  REQUIRE(javascript.code.find("__mpf_matlab_mrdivide_sparse_real_square") != std::string::npos);
  REQUIRE(javascript.code.find("std::vector") == std::string::npos);
  REQUIRE(cpp.code.find("struct sparse_matrix") != std::string::npos);
  REQUIRE(cpp.code.find("template <typename T> void validate_sparse_csc(") != std::string::npos);
  REQUIRE(cpp.code.find("const sparse_matrix<T>& validate_sparse_csc(") == std::string::npos);
  REQUIRE(cpp.code.find("mpf_runtime::sparse_matrix<double>") != std::string::npos);
  REQUIRE(cpp.code.find("sparse_tridiagonal_factor") != std::string::npos);
  REQUIRE(cpp.code.find("sparse_row_lu_factor") != std::string::npos);
  REQUIRE(cpp.code.find("mpf_runtime::matlab_mldivide_sparse_real_square") != std::string::npos);
  REQUIRE(cpp.code.find("mpf_runtime::matlab_mrdivide_sparse_real_square") != std::string::npos);
  REQUIRE(cpp.code.find("__mpf_sparse_csc_tag") == std::string::npos);
  for (const auto* result : {&javascript, &cpp}) {
    for (const auto line : {1U, 2U, 4U, 6U, 7U, 9U}) {
      REQUIRE(std::any_of(result->source_map.segments.begin(), result->source_map.segments.end(),
                          [line](const auto& segment) { return segment.original_line == line; }));
    }
  }
}

TEST_CASE("Matlab sparse matrix products preserve storage and target isolation") {
  const std::string source =
      "A = sparse([1 0 2; 0 3 0]);\n"
      "B = sparse([0 4; 5 0; 0 6]);\n"
      "sparse_product = A * B;\n"
      "right_full_product = A * full(B);\n"
      "left_full_product = full(A) * B;\n"
      "dense_sparse_product = full(sparse_product);\n"
      "disp(dense_sparse_product(1,2), dense_sparse_product(2,1), "
      "right_full_product(1,2), left_full_product(2,1), issparse(sparse_product), "
      "issparse(right_full_product), issparse(left_full_product), nnz(sparse_product))\n";
  const auto javascript =
      transpile_array(source, mpf::SourceLanguage::matlab, mpf::TargetLanguage::javascript);
  const auto cpp = transpile_array(source, mpf::SourceLanguage::matlab, mpf::TargetLanguage::cpp);
  REQUIRE(javascript.success());
  REQUIRE(cpp.success());
  REQUIRE(javascript.code.find("function __mpf_sparse_sparse_mtimes(") != std::string::npos);
  REQUIRE(javascript.code.find("function __mpf_sparse_dense_mtimes(") != std::string::npos);
  REQUIRE(javascript.code.find("function __mpf_dense_sparse_mtimes(") != std::string::npos);
  REQUIRE(javascript.code.find("__mpf_sparse_sparse_mtimes(A, B)") != std::string::npos);
  REQUIRE(javascript.code.find("__mpf_sparse_dense_mtimes(A, __mpf_full(B))") != std::string::npos);
  REQUIRE(javascript.code.find("__mpf_dense_sparse_mtimes(__mpf_full(A), B)") != std::string::npos);
  REQUIRE(javascript.code.find("mpf_runtime::sparse_sparse_mtimes") == std::string::npos);
  REQUIRE(cpp.code.find("sparse_matrix<double> sparse_sparse_mtimes(") != std::string::npos);
  REQUIRE(cpp.code.find("std::vector<std::vector<double>> sparse_dense_mtimes(") !=
          std::string::npos);
  REQUIRE(cpp.code.find("std::vector<std::vector<double>> dense_sparse_mtimes(") !=
          std::string::npos);
  REQUIRE(cpp.code.find("mpf_runtime::sparse_sparse_mtimes(A, B)") != std::string::npos);
  REQUIRE(cpp.code.find("mpf_runtime::sparse_dense_mtimes(A, mpf_runtime::full(B))") !=
          std::string::npos);
  REQUIRE(cpp.code.find("mpf_runtime::dense_sparse_mtimes(mpf_runtime::full(A), B)") !=
          std::string::npos);
  REQUIRE(cpp.code.find("__mpf_sparse_sparse_mtimes") == std::string::npos);
  for (const auto* result : {&javascript, &cpp}) {
    for (std::size_t line = 1U; line <= 7U; ++line) {
      REQUIRE(std::any_of(result->source_map.segments.begin(), result->source_map.segments.end(),
                          [line](const auto& segment) { return segment.original_line == line; }));
    }
  }
}

TEST_CASE("Matlab sparse constructors and transpose preserve canonical CSC plans") {
  const std::string source =
      "zero_matrix = sparse(3, 4);\n"
      "empty_triplets = sparse([], [], [], 3, 4);\n"
      "cancelled = sparse([1 3 1 2], [1 1 1 3], [2 4 -2 5], 3, 4, 8);\n"
      "inferred = sparse([2 1 2], [3 2 3], [4 5 1]);\n"
      "row_broadcast = sparse(2, [1 2 3], [7 0 -7], 3, 3);\n"
      "column_broadcast = sparse([1 2 3], 2, [1 0 3], 3, 3);\n"
      "value_broadcast = sparse([1 2], [2 3], 5, 3, 3);\n"
      "transposed = cancelled.';\n"
      "conjugate_transposed = inferred';\n"
      "dense_transposed = full(transposed);\n"
      "disp(nnz(zero_matrix), nnz(empty_triplets), nnz(cancelled), nnz(row_broadcast), "
      "dense_transposed(1, 3))\n";
  const auto javascript =
      transpile_array(source, mpf::SourceLanguage::matlab, mpf::TargetLanguage::javascript);
  const auto cpp = transpile_array(source, mpf::SourceLanguage::matlab, mpf::TargetLanguage::cpp);
  REQUIRE(javascript.success());
  REQUIRE(cpp.success());
  REQUIRE(javascript.code.find("function __mpf_sparse_from_triplets") != std::string::npos);
  REQUIRE(javascript.code.find("__mpf_sparse(3, 4)") != std::string::npos);
  REQUIRE(javascript.code.find("__mpf_sparse_transpose(cancelled)") != std::string::npos);
  REQUIRE(javascript.code.find("std::stable_sort") == std::string::npos);
  REQUIRE(cpp.code.find("sparse_from_triplets") != std::string::npos);
  REQUIRE(cpp.code.find("std::stable_sort(entries.begin()") != std::string::npos);
  REQUIRE(cpp.code.find("mpf_runtime::sparse_transpose(cancelled)") != std::string::npos);
  REQUIRE(cpp.code.find("__mpf_sparse_from_triplets") == std::string::npos);
  for (const auto* result : {&javascript, &cpp}) {
    for (const auto line : {1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U, 9U, 10U}) {
      REQUIRE(std::any_of(result->source_map.segments.begin(), result->source_map.segments.end(),
                          [line](const auto& segment) { return segment.original_line == line; }));
    }
  }
}

TEST_CASE("Matlab sparse CSC indexing preserves storage shape and target isolation") {
  const std::string source =
      "A = sparse([1 0 2; 0 3 0; 4 0 5]);\n"
      "linear_scalar = A(5);\n"
      "subscript_scalar = A(3, 1);\n"
      "linear = A([9 1; 5 7]);\n"
      "sliced = A(1:2:9);\n"
      "logical_linear = A([true false true false true false true false true]);\n"
      "mask = [true false true false true false true false true];\n"
      "dynamic_logical = A(mask);\n"
      "rows = A([true false true], :);\n"
      "repeated = A([3 1 3], [3 1 3]);\n"
      "reversed = A(3:-1:1, 3:-1:1);\n"
      "empty_linear = A([]);\n"
      "empty_block = A([], [1 3]);\n"
      "column = A(:);\n"
      "disp(linear_scalar, subscript_scalar, nnz(linear), nnz(sliced), "
      "nnz(logical_linear), nnz(dynamic_logical), nnz(rows), nnz(repeated), nnz(reversed), "
      "nnz(empty_linear), nnz(empty_block), nnz(column), issparse(repeated))\n";
  const auto javascript =
      transpile_array(source, mpf::SourceLanguage::matlab, mpf::TargetLanguage::javascript);
  const auto cpp = transpile_array(source, mpf::SourceLanguage::matlab, mpf::TargetLanguage::cpp);
  REQUIRE(javascript.success());
  REQUIRE(cpp.success());
  REQUIRE(javascript.code.find("function __mpf_sparse_linear_element") != std::string::npos);
  REQUIRE(javascript.code.find("function __mpf_sparse_subscript_element") != std::string::npos);
  REQUIRE(javascript.code.find("function __mpf_sparse_linear_selection") != std::string::npos);
  REQUIRE(javascript.code.find("function __mpf_sparse_submatrix_selection") != std::string::npos);
  REQUIRE(javascript.code.find("__mpf_sparse_linear_element(A,") != std::string::npos);
  REQUIRE(javascript.code.find("__mpf_sparse_submatrix_selection(A,") != std::string::npos);
  REQUIRE(javascript.code.find(", [null, 1])") != std::string::npos);
  REQUIRE(javascript.code.find("mpf_runtime::sparse_") == std::string::npos);
  REQUIRE(cpp.code.find("double sparse_linear_element(") != std::string::npos);
  REQUIRE(cpp.code.find("double sparse_subscript_element(") != std::string::npos);
  REQUIRE(cpp.code.find("sparse_matrix<double> sparse_linear_selection(") != std::string::npos);
  REQUIRE(cpp.code.find("sparse_matrix<double> sparse_submatrix_selection(") != std::string::npos);
  REQUIRE(cpp.code.find("mpf_runtime::sparse_linear_element(A,") != std::string::npos);
  REQUIRE(cpp.code.find("mpf_runtime::sparse_submatrix_selection(A,") != std::string::npos);
  REQUIRE(cpp.code.find("std::nullopt, std::optional<std::size_t>{1}") != std::string::npos);
  REQUIRE(cpp.code.find("__mpf_sparse_linear_element") == std::string::npos);
  for (const auto* result : {&javascript, &cpp}) {
    for (std::size_t line = 1U; line <= 15U; ++line) {
      REQUIRE(std::any_of(result->source_map.segments.begin(), result->source_map.segments.end(),
                          [line](const auto& segment) { return segment.original_line == line; }));
    }
  }
}

TEST_CASE("Matlab sparse CSC indexed mutation preserves canonical storage and order") {
  const std::string source =
      "A = sparse([1 0 2; 0 3 0; 4 0 5]);\n"
      "A(2,1) = 7;\n"
      "A(1,3) = 0;\n"
      "A([1 3 1]) = [8 9 10];\n"
      "A(2:3,[2 3]) = [0 11; 12 0];\n"
      "A(4,4) = 13;\n"
      "A(:,2) = [];\n"
      "B = sparse([1 2; 0 3]);\n"
      "B([1 4]) = B([2 3]);\n"
      "B([1 1 2]) = [4 5 0];\n"
      "disp(nnz(A), nnz(B), issparse(A), issparse(B))\n";
  const auto javascript =
      transpile_array(source, mpf::SourceLanguage::matlab, mpf::TargetLanguage::javascript);
  const auto cpp = transpile_array(source, mpf::SourceLanguage::matlab, mpf::TargetLanguage::cpp);
  REQUIRE(javascript.success());
  REQUIRE(cpp.success());
  REQUIRE(javascript.code.find("function __mpf_sparse_assign(") != std::string::npos);
  REQUIRE(javascript.code.find("function __mpf_sparse_erase(") != std::string::npos);
  REQUIRE(javascript.code.find("__mpf_sparse_assign(A,") != std::string::npos);
  REQUIRE(javascript.code.find("__mpf_sparse_erase(A,") != std::string::npos);
  REQUIRE(javascript.code.find("mpf_runtime::sparse_assign") == std::string::npos);
  REQUIRE(cpp.code.find("void sparse_assign_linear(") != std::string::npos);
  REQUIRE(cpp.code.find("void sparse_assign_subscripts(") != std::string::npos);
  REQUIRE(cpp.code.find("void sparse_erase_indexed(") != std::string::npos);
  REQUIRE(cpp.code.find("mpf_runtime::sparse_assign_linear(A,") != std::string::npos);
  REQUIRE(cpp.code.find("mpf_runtime::sparse_assign_subscripts(A,") != std::string::npos);
  REQUIRE(cpp.code.find("mpf_runtime::sparse_erase_indexed(A,") != std::string::npos);
  REQUIRE(cpp.code.find("__mpf_sparse_assign") == std::string::npos);
  for (const auto* result : {&javascript, &cpp}) {
    for (std::size_t line = 1U; line <= 11U; ++line) {
      REQUIRE(std::any_of(result->source_map.segments.begin(), result->source_map.segments.end(),
                          [line](const auto& segment) { return segment.original_line == line; }));
    }
  }
}

TEST_CASE("Matlab sparse CSC contract fails closed outside the supported vertical slice") {
  const std::vector<std::string> unsupported{"A = sparse([1 0; 0 1]);\nvalue = A(1, 1, 1);\n",
                                             "A = sparse([1 0; 0 1]);\n"
                                             "value = A(reshape([1 2], 1, 1, 2));\n",
                                             "A = sparse([1 0; 0 1]);\nvalue = A([1+0i]);\n",
                                             "A = sparse([1 0; 0 1]);\nB = ~A;\n",
                                             "A = sparse([1 0; 0 1; 1 1]);\nB = A \\ [1; 2; 3];\n",
                                             "A = sparse([1+2i 0; 0 1]);\n",
                                             "A = sparse([1 2; 3 4], 2);\n",
                                             "A = sparse([]);\n",
                                             "A = sparse(0, 3);\n",
                                             "m = 3;\nA = sparse(m, 3);\n",
                                             "i = [1 2];\nj = [1 2];\nv = [3 4];\n"
                                             "A = sparse(i, j, v);\n",
                                             "A = sparse([1 2], [1 2 3], [4 5 6], 3, 3);\n",
                                             "A = sparse([1 2], [1 2], [4 5], 3, 3, -1);\n"};
  for (const auto& source : unsupported) {
    for (const auto target : {mpf::TargetLanguage::javascript, mpf::TargetLanguage::cpp}) {
      const auto result = transpile_array(source, mpf::SourceLanguage::matlab, target);
      REQUIRE(!result.success());
      REQUIRE(result.diagnostics.front().code == "MPF2054");
    }
  }
}

TEST_CASE("Matlab sparse reshape preserves CSC order and target isolation") {
  const std::string source =
      "A = sparse([1 0 2; 0 3 0]);\n"
      "vector = reshape(A, [6 1]);\n"
      "inferred_rows = reshape(A, [], 3);\n"
      "inferred_columns = reshape(A, 2, []);\n"
      "folded = reshape(A, 1, 2, 3);\n"
      "restored = reshape(folded, [2 3]);\n"
      "disp(vector(4), vector(5), inferred_rows(2,2), inferred_columns(1,3), "
      "folded(1,4), restored(1,3), nnz(folded), issparse(restored))\n";
  const auto javascript =
      transpile_array(source, mpf::SourceLanguage::matlab, mpf::TargetLanguage::javascript);
  const auto cpp = transpile_array(source, mpf::SourceLanguage::matlab, mpf::TargetLanguage::cpp);
  REQUIRE(javascript.success());
  REQUIRE(cpp.success());
  REQUIRE(javascript.code.find("function __mpf_sparse_reshape(") != std::string::npos);
  REQUIRE(javascript.code.find("__mpf_sparse_reshape(A, [2, 3], [6, 1], [6, 1])") !=
          std::string::npos);
  REQUIRE(javascript.code.find("__mpf_sparse_reshape(A, [2, 3], [2, 3], [2, 3])") !=
          std::string::npos);
  REQUIRE(javascript.code.find("__mpf_sparse_reshape(A, [2, 3], [1, 2, 3], [1, 6])") !=
          std::string::npos);
  REQUIRE(javascript.code.find("mpf_runtime::sparse_reshape") == std::string::npos);
  REQUIRE(cpp.code.find("sparse_matrix<double> sparse_reshape(") != std::string::npos);
  REQUIRE(cpp.code.find("mpf_runtime::sparse_reshape(A, std::array<std::size_t, 2>{2, 3}, "
                        "std::array<std::size_t, 2>{6, 1}, "
                        "std::array<std::size_t, 2>{6, 1})") != std::string::npos);
  REQUIRE(cpp.code.find("std::array<std::size_t, 3>{1, 2, 3}, "
                        "std::array<std::size_t, 2>{1, 6})") != std::string::npos);
  REQUIRE(cpp.code.find("__mpf_sparse_reshape") == std::string::npos);
  for (const auto* result : {&javascript, &cpp}) {
    for (std::size_t line = 1U; line <= 7U; ++line) {
      REQUIRE(std::any_of(result->source_map.segments.begin(), result->source_map.segments.end(),
                          [line](const auto& segment) { return segment.original_line == line; }));
    }
  }
}

TEST_CASE("Matlab reshape rejects invalid inferred and sparse dimensions") {
  const std::vector<std::string> invalid{"A = sparse([1 0; 0 1]);\nB = reshape(A, [], []);\n",
                                         "A = sparse([1 0; 0 1]);\nB = reshape(A, [], 3);\n",
                                         "A = sparse([1 0; 0 1]);\nB = reshape(A, 4);\n",
                                         "A = sparse([1 0; 0 1]);\nB = reshape(A, 0, 4);\n",
                                         "A = sparse([1 0; 0 1]);\nB = reshape(A, [3 2]);\n"};
  for (const auto& source : invalid) {
    for (const auto target : {mpf::TargetLanguage::javascript, mpf::TargetLanguage::cpp}) {
      const auto result = transpile_array(source, mpf::SourceLanguage::matlab, target);
      REQUIRE(!result.success());
      REQUIRE(result.diagnostics.front().code == "MPF2027" ||
              result.diagnostics.front().code == "MPF2024" ||
              result.diagnostics.front().code == "MPF2022");
    }
  }
}

TEST_CASE("Matlab static dense matrix solve and integer power use target-owned plans") {
  const std::string source =
      "coefficient = [4 1; 2 3];\n"
      "right_hand_side = [9; 8];\n"
      "solution = coefficient \\ right_hand_side;\n"
      "right_solution = [5 7] / coefficient;\n"
      "squared = coefficient ^ 2;\n"
      "inverse = coefficient ^ -1;\n"
      "scaled_right = coefficient / 2;\n"
      "scaled_left = 2 \\ coefficient;\n"
      "disp(solution(1) + right_solution(2) + squared(1, 1) + inverse(2, 2) + "
      "scaled_right(1, 1) + scaled_left(2, 2))\n";
  const auto javascript =
      transpile_array(source, mpf::SourceLanguage::matlab, mpf::TargetLanguage::javascript);
  const auto cpp = transpile_array(source, mpf::SourceLanguage::matlab, mpf::TargetLanguage::cpp);
  REQUIRE(javascript.success());
  REQUIRE(cpp.success());
  REQUIRE(javascript.code.find(
              "__mpf_matlab_mldivide_structured_real_square(coefficient, right_hand_side)") !=
          std::string::npos);
  REQUIRE(
      javascript.code.find("__mpf_matlab_mrdivide_structured_real_square([5, 7], coefficient)") !=
      std::string::npos);
  REQUIRE(javascript.code.find("__mpf_matlab_mldivide_structured_square") == std::string::npos);
  REQUIRE(javascript.code.find("__mpf_matlab_mpower(coefficient, -1)") != std::string::npos);
  REQUIRE(javascript.code.find("__mpf_matlab_divide(coefficient, 2, [2, 2], [1, 1], [2, 2])") !=
          std::string::npos);
  REQUIRE(
      javascript.code.find("__mpf_matlab_left_divide(2, coefficient, [1, 1], [2, 2], [2, 2])") !=
      std::string::npos);
  REQUIRE(javascript.code.find("__mpf_matlab_lu_factor") != std::string::npos);
  REQUIRE(javascript.code.find("__mpf_matlab_lu_apply_transpose") != std::string::npos);
  REQUIRE(javascript.code.find("__mpf_matlab_lu_rcond") != std::string::npos);
  REQUIRE(javascript.code.find("matrix is singular to working precision") != std::string::npos);
  REQUIRE(javascript.code.find("matrix is close to singular or badly scaled") != std::string::npos);
  REQUIRE(javascript.code.find("throw new RangeError('MPF Matlab matrix is singular") ==
          std::string::npos);
  REQUIRE(javascript.code.find("Number.isSafeInteger(exponent)") != std::string::npos);
  REQUIRE(cpp.code.find("mpf_runtime::matlab_mldivide_structured_real_square(coefficient, "
                        "right_hand_side)") != std::string::npos);
  REQUIRE(cpp.code.find("mpf_runtime::matlab_mrdivide_structured_real_square") !=
          std::string::npos);
  REQUIRE(cpp.code.find("mpf_runtime::matlab_mldivide_structured_square") == std::string::npos);
  REQUIRE(cpp.code.find("mpf_runtime::matlab_mpower(coefficient, -1)") != std::string::npos);
  REQUIRE(cpp.code.find("mpf_runtime::matlab_divide_broadcast") != std::string::npos);
  REQUIRE(cpp.code.find("mpf_runtime::matlab_left_divide_broadcast") != std::string::npos);
  REQUIRE(cpp.code.find("matlab_lu_factorization") != std::string::npos);
  REQUIRE(cpp.code.find("matlab_lu_apply_transpose") != std::string::npos);
  REQUIRE(cpp.code.find("matlab_lu_rcond") != std::string::npos);
  REQUIRE(cpp.code.find("matrix is singular to working precision") != std::string::npos);
  REQUIRE(cpp.code.find("matrix is close to singular or badly scaled") != std::string::npos);
  REQUIRE(cpp.code.find("throw std::domain_error(\"MPF Matlab matrix is singular") ==
          std::string::npos);
  REQUIRE(cpp.code.find("maximum_safe_integer") != std::string::npos);
  REQUIRE(cpp.code.find("MPF runtime error: ") != std::string::npos);
  REQUIRE(cpp.code.find("inline double ieee_divide") == std::string::npos);
  REQUIRE(javascript.source_map.segments.size() >= 6U);
  REQUIRE(cpp.source_map.segments.size() >= 6U);
}

TEST_CASE("Matlab square division dispatches target-owned real structure kernels") {
  const std::string source =
      "diagonal = [2 0 0; 0 4 0; 0 0 5];\n"
      "lower = [2 0 0; 1 3 0; 4 -2 5];\n"
      "upper = [2 1 4; 0 3 -2; 0 0 5];\n"
      "diagonal_solution = diagonal \\ [4; 8; 15];\n"
      "lower_solution = lower \\ [2; 7; 15];\n"
      "upper_solution = upper \\ [16; 0; 15];\n"
      "dense_solution = [4 1; 2 3] \\ [9; 8];\n"
      "right_solution = [2 7 15] / upper;\n"
      "disp(diagonal_solution(3) + lower_solution(2) + upper_solution(1) + "
      "dense_solution(1) + right_solution(2))\n";
  const auto javascript =
      transpile_array(source, mpf::SourceLanguage::matlab, mpf::TargetLanguage::javascript);
  const auto cpp = transpile_array(source, mpf::SourceLanguage::matlab, mpf::TargetLanguage::cpp);
  REQUIRE(javascript.success());
  REQUIRE(cpp.success());
  REQUIRE(javascript.code.find("__mpf_matlab_square_structure") != std::string::npos);
  REQUIRE(javascript.code.find("__mpf_matlab_diagonal_apply") != std::string::npos);
  REQUIRE(javascript.code.find("__mpf_matlab_triangular_apply") != std::string::npos);
  REQUIRE(javascript.code.find("__mpf_matlab_structured_real_square_solve") != std::string::npos);
  REQUIRE(javascript.code.find("return __mpf_matlab_lu_solve(left, right)") != std::string::npos);
  REQUIRE(cpp.code.find("matlab_classify_square_structure") != std::string::npos);
  REQUIRE(cpp.code.find("matlab_diagonal_apply") != std::string::npos);
  REQUIRE(cpp.code.find("matlab_triangular_apply") != std::string::npos);
  REQUIRE(cpp.code.find("matlab_structured_real_square_solve") != std::string::npos);
  REQUIRE(cpp.code.find("return matlab_lu_solve(left, right)") != std::string::npos);
  for (const auto line : {4U, 5U, 6U, 7U, 8U}) {
    REQUIRE(std::any_of(javascript.source_map.segments.begin(),
                        javascript.source_map.segments.end(),
                        [line](const auto& segment) { return segment.original_line == line; }));
    REQUIRE(std::any_of(cpp.source_map.segments.begin(), cpp.source_map.segments.end(),
                        [line](const auto& segment) { return segment.original_line == line; }));
  }
}

TEST_CASE("Matlab real square policy plans pivoted tridiagonal and Cholesky solves") {
  const std::string source =
      "tridiagonal = [0 2 0; 1 3 4; 0 5 6];\n"
      "tridiagonal_rhs = [4; 19; 28];\n"
      "tridiagonal_solution = tridiagonal \\ tridiagonal_rhs;\n"
      "tridiagonal_right = [2 23 26] / tridiagonal;\n"
      "positive_definite = [4 2 2; 2 10 -2; 2 -2 6];\n"
      "positive_definite_rhs = [14; 16; 16];\n"
      "positive_definite_left = positive_definite \\ positive_definite_rhs;\n"
      "positive_definite_right = [14 16 16] / positive_definite;\n"
      "symmetric_indefinite = [0 1 2; 1 0 3; 2 3 0];\n"
      "indefinite_solution = symmetric_indefinite \\ [8; 10; 8];\n"
      "disp(tridiagonal_solution(2) + tridiagonal_right(3) + positive_definite_left(3) + "
      "positive_definite_right(1) + indefinite_solution(1))\n";
  const auto javascript =
      transpile_array(source, mpf::SourceLanguage::matlab, mpf::TargetLanguage::javascript);
  const auto cpp = transpile_array(source, mpf::SourceLanguage::matlab, mpf::TargetLanguage::cpp);
  REQUIRE(javascript.success());
  REQUIRE(cpp.success());
  REQUIRE(javascript.code.find("__mpf_matlab_tridiagonal_factor") != std::string::npos);
  REQUIRE(javascript.code.find("__mpf_matlab_tridiagonal_apply_transpose") != std::string::npos);
  REQUIRE(javascript.code.find("__mpf_matlab_tridiagonal_rcond") != std::string::npos);
  REQUIRE(javascript.code.find("__mpf_matlab_cholesky_factor") != std::string::npos);
  REQUIRE(javascript.code.find("__mpf_matlab_cholesky_apply") != std::string::npos);
  REQUIRE(javascript.code.find("__mpf_matlab_cholesky_rcond") != std::string::npos);
  REQUIRE(javascript.code.find("structure === 'tridiagonal'") != std::string::npos);
  REQUIRE(javascript.code.find("structure === 'symmetric'") != std::string::npos);
  REQUIRE(javascript.code.find("if (factor.positiveDefinite)") != std::string::npos);
  REQUIRE(cpp.code.find("matlab_tridiagonal_factorization") != std::string::npos);
  REQUIRE(cpp.code.find("matlab_tridiagonal_apply_transpose") != std::string::npos);
  REQUIRE(cpp.code.find("matlab_tridiagonal_rcond") != std::string::npos);
  REQUIRE(cpp.code.find("matlab_cholesky_factorization") != std::string::npos);
  REQUIRE(cpp.code.find("matlab_cholesky_apply") != std::string::npos);
  REQUIRE(cpp.code.find("matlab_cholesky_rcond") != std::string::npos);
  REQUIRE(cpp.code.find("matlab_square_structure::tridiagonal") != std::string::npos);
  REQUIRE(cpp.code.find("matlab_square_structure::symmetric") != std::string::npos);
  REQUIRE(cpp.code.find("if (factor.positive_definite)") != std::string::npos);
  for (const auto line : {3U, 4U, 7U, 8U, 10U}) {
    REQUIRE(std::any_of(javascript.source_map.segments.begin(),
                        javascript.source_map.segments.end(),
                        [line](const auto& segment) { return segment.original_line == line; }));
    REQUIRE(std::any_of(cpp.source_map.segments.begin(), cpp.source_map.segments.end(),
                        [line](const auto& segment) { return segment.original_line == line; }));
  }
}

TEST_CASE("Matlab rectangular solve selects rank-aware basic CPQR solutions for both targets") {
  const std::string source =
      "over = [1 0; 0 1; 1 1] \\ [1; 2; 4];\n"
      "under = [1 0 1; 0 1 1] \\ [2; 3];\n"
      "right_over = [2 3 0] / [1 0 0; 0 1 0];\n"
      "right_under = [1 2] / [1 0; 0 1; 1 1];\n"
      "ranked = [1 2; 2 4; 3 6] \\ [1; 2; 3];\n"
      "disp(over(1) + under(1) + right_over(1) + right_under(1) + ranked(2))\n";
  const auto javascript =
      transpile_array(source, mpf::SourceLanguage::matlab, mpf::TargetLanguage::javascript);
  const auto cpp = transpile_array(source, mpf::SourceLanguage::matlab, mpf::TargetLanguage::cpp);
  REQUIRE(javascript.success());
  REQUIRE(cpp.success());
  REQUIRE(javascript.code.find("__mpf_matlab_cpqr_factor") != std::string::npos);
  REQUIRE(javascript.code.find("__mpf_matlab_basic_least_squares") != std::string::npos);
  REQUIRE(javascript.code.find("__mpf_matlab_overdetermined_solve") != std::string::npos);
  REQUIRE(javascript.code.find("__mpf_matlab_underdetermined_solve") != std::string::npos);
  REQUIRE(javascript.code.find("__mpf_matlab_mldivide_overdetermined") != std::string::npos);
  REQUIRE(javascript.code.find("__mpf_matlab_mldivide_underdetermined") != std::string::npos);
  REQUIRE(javascript.code.find("__mpf_matlab_mrdivide_overdetermined") != std::string::npos);
  REQUIRE(javascript.code.find("__mpf_matlab_mrdivide_underdetermined") != std::string::npos);
  REQUIRE(javascript.code.find("rank deficient to working precision") != std::string::npos);
  REQUIRE(cpp.code.find("matlab_cpqr_factorization") != std::string::npos);
  REQUIRE(cpp.code.find("matlab_basic_least_squares") != std::string::npos);
  REQUIRE(cpp.code.find("matlab_overdetermined_solve") != std::string::npos);
  REQUIRE(cpp.code.find("matlab_underdetermined_solve") != std::string::npos);
  REQUIRE(cpp.code.find("mpf_runtime::matlab_mldivide_overdetermined") != std::string::npos);
  REQUIRE(cpp.code.find("mpf_runtime::matlab_mldivide_underdetermined") != std::string::npos);
  REQUIRE(cpp.code.find("mpf_runtime::matlab_mrdivide_overdetermined") != std::string::npos);
  REQUIRE(cpp.code.find("mpf_runtime::matlab_mrdivide_underdetermined") != std::string::npos);
  REQUIRE(cpp.code.find("rank deficient to working precision") != std::string::npos);
  REQUIRE(std::any_of(javascript.source_map.segments.begin(), javascript.source_map.segments.end(),
                      [](const auto& segment) { return segment.original_line == 5U; }));
  REQUIRE(std::any_of(cpp.source_map.segments.begin(), cpp.source_map.segments.end(),
                      [](const auto& segment) { return segment.original_line == 5U; }));
}

TEST_CASE("Matlab complex rectangular solve uses target-owned CPQR and conjugate right division") {
  const std::string source =
      "over = [1+1i 0; 0 1-1i; 1+1i 1-1i] \\ [1+3i; 2-4i; 3-1i];\n"
      "under = [1+1i 0 0; 0 1-1i 0] \\ [1+3i; 2-4i];\n"
      "right_over = [1+3i 2-4i 0] / [1+1i 0 0; 0 1-1i 0];\n"
      "right_under = [3-1i 4+2i] / [1-1i 0; 0 1+1i; 0 0];\n"
      "disp(real(over(1)) + real(under(2)) + real(right_over(1)) + "
      "real(right_under(2)))\n";
  const auto javascript =
      transpile_array(source, mpf::SourceLanguage::matlab, mpf::TargetLanguage::javascript);
  const auto cpp = transpile_array(source, mpf::SourceLanguage::matlab, mpf::TargetLanguage::cpp);
  REQUIRE(javascript.success());
  REQUIRE(cpp.success());
  REQUIRE(javascript.code.find("__mpf_matlab_complex_cpqr_factor") != std::string::npos);
  REQUIRE(javascript.code.find("__mpf_matlab_apply_complex_cpqr_qh") != std::string::npos);
  REQUIRE(javascript.code.find("__mpf_matlab_complex_basic_least_squares") != std::string::npos);
  REQUIRE(javascript.code.find("__mpf_matlab_mldivide_complex_overdetermined") !=
          std::string::npos);
  REQUIRE(javascript.code.find("__mpf_matlab_mldivide_complex_underdetermined") !=
          std::string::npos);
  REQUIRE(javascript.code.find("__mpf_matlab_mrdivide_complex_overdetermined") !=
          std::string::npos);
  REQUIRE(javascript.code.find("__mpf_matlab_mrdivide_complex_underdetermined") !=
          std::string::npos);
  REQUIRE(javascript.code.find("__mpf_matlab_complex_ctranspose") != std::string::npos);
  REQUIRE(cpp.code.find("matlab_complex_cpqr_factorization") != std::string::npos);
  REQUIRE(cpp.code.find("matlab_apply_complex_cpqr_qh") != std::string::npos);
  REQUIRE(cpp.code.find("matlab_complex_basic_least_squares") != std::string::npos);
  REQUIRE(cpp.code.find("mpf_runtime::matlab_mldivide_complex_overdetermined") !=
          std::string::npos);
  REQUIRE(cpp.code.find("mpf_runtime::matlab_mldivide_complex_underdetermined") !=
          std::string::npos);
  REQUIRE(cpp.code.find("mpf_runtime::matlab_mrdivide_complex_overdetermined") !=
          std::string::npos);
  REQUIRE(cpp.code.find("mpf_runtime::matlab_mrdivide_complex_underdetermined") !=
          std::string::npos);
  REQUIRE(cpp.code.find("matlab_complex_ctranspose") != std::string::npos);
  for (const auto line : {1U, 2U, 3U, 4U}) {
    REQUIRE(std::any_of(javascript.source_map.segments.begin(),
                        javascript.source_map.segments.end(),
                        [line](const auto& segment) { return segment.original_line == line; }));
    REQUIRE(std::any_of(cpp.source_map.segments.begin(), cpp.source_map.segments.end(),
                        [line](const auto& segment) { return segment.original_line == line; }));
  }
}

TEST_CASE("Matlab square solve condition warnings remain target-owned and source-mapped") {
  const std::string source =
      "singular = [1 0; 0 0];\n"
      "exact = singular \\ [1; 1];\n"
      "nearly = [16 2 3 13; 5 11 10 8; 9 7 6 12; 4 14 15 1];\n"
      "conditioned = [34 34 34 34] / nearly;\n"
      "disp(exact(1) + conditioned(1))\n";
  const auto javascript =
      transpile_array(source, mpf::SourceLanguage::matlab, mpf::TargetLanguage::javascript);
  const auto cpp = transpile_array(source, mpf::SourceLanguage::matlab, mpf::TargetLanguage::cpp);
  REQUIRE(javascript.success());
  REQUIRE(cpp.success());
  REQUIRE(javascript.code.find("__mpf_matlab_lu_rcond") != std::string::npos);
  REQUIRE(javascript.code.find("__mpf_matlab_warn_square_condition") != std::string::npos);
  REQUIRE(cpp.code.find("matlab_lu_rcond") != std::string::npos);
  REQUIRE(cpp.code.find("matlab_warn_square_condition") != std::string::npos);
  for (const auto line : {2U, 4U}) {
    REQUIRE(std::any_of(javascript.source_map.segments.begin(),
                        javascript.source_map.segments.end(),
                        [line](const auto& segment) { return segment.original_line == line; }));
    REQUIRE(std::any_of(cpp.source_map.segments.begin(), cpp.source_map.segments.end(),
                        [line](const auto& segment) { return segment.original_line == line; }));
  }
}

TEST_CASE("Matlab contextual end uses static fast paths and typed dynamic extent plans") {
  const std::string source =
      "values = [10 20 30 40];\n"
      "matrix = [1 2 3; 4 5 6];\n"
      "tail = values(2:end);\n"
      "disp(values(end) + values(end - 1) + tail(end) + matrix(end, end) + matrix(end))\n";
  const auto javascript =
      transpile_array(source, mpf::SourceLanguage::matlab, mpf::TargetLanguage::javascript);
  const auto cpp = transpile_array(source, mpf::SourceLanguage::matlab, mpf::TargetLanguage::cpp);
  REQUIRE(javascript.success());
  REQUIRE(cpp.success());
  REQUIRE(javascript.code.find("__mpf_get(values, [4]") != std::string::npos);
  REQUIRE(javascript.code.find("__mpf_get(matrix, [2, 3]") != std::string::npos);
  REQUIRE(cpp.code.find("mpf_runtime::matrix_linear_index(matrix, static_cast<std::int64_t>(6)") !=
          std::string::npos);

  const std::string dynamic_source =
      "function [last, tail, corner, selected] = inspect_dynamic(values, matrix)\n"
      "last = values(end);\n"
      "tail = sum(values(2:end));\n"
      "corner = matrix(end, end) + matrix(end);\n"
      "selected = sum(values([1 end]));\n"
      "end\n";
  const auto dynamic_javascript =
      transpile_array(dynamic_source, mpf::SourceLanguage::matlab, mpf::TargetLanguage::javascript);
  const auto dynamic_cpp =
      transpile_array(dynamic_source, mpf::SourceLanguage::matlab, mpf::TargetLanguage::cpp);
  REQUIRE(dynamic_javascript.success());
  REQUIRE(dynamic_cpp.success());
  REQUIRE(dynamic_javascript.code.find("(__mpf_extent) =>") != std::string::npos);
  REQUIRE(dynamic_javascript.code.find("__mpf_resolve_extent") != std::string::npos);
  REQUIRE(dynamic_cpp.code.find("[&](std::size_t __mpf_extent)") != std::string::npos);
  REQUIRE(dynamic_cpp.code.find("mpf_runtime::linear_index_column_major") != std::string::npos);
  REQUIRE(dynamic_cpp.code.find("mpf_runtime::section_nd") != std::string::npos);
  REQUIRE(dynamic_cpp.code.find("decltype(mpf_runtime::linear_index_column_major(values, [&]") ==
          std::string::npos);
  for (const auto* result : {&dynamic_javascript, &dynamic_cpp}) {
    REQUIRE(std::any_of(result->source_map.segments.begin(), result->source_map.segments.end(),
                        [](const auto& segment) { return segment.original_line == 2U; }));
    REQUIRE(std::any_of(result->source_map.segments.begin(), result->source_map.segments.end(),
                        [](const auto& segment) { return segment.original_line == 5U; }));
  }

  const auto growth = transpile_array("values = [1 2 3];\nvalues(end + 1) = 4;\n",
                                      mpf::SourceLanguage::matlab, mpf::TargetLanguage::javascript);
  const auto outside = transpile_array("value = end;\n", mpf::SourceLanguage::matlab,
                                       mpf::TargetLanguage::javascript);
  REQUIRE(growth.success());
  REQUIRE(growth.code.find("__mpf_grow(values") != std::string::npos);
  REQUIRE(!outside.success());
  REQUIRE(outside.diagnostics.front().code == "MPF2048");
}

TEST_CASE("Matlab indexed mutation plans grow and erase vectors matrices and tensors") {
  const std::string source =
      "row = [1 2 3];\n"
      "row(5:7) = [5 6 7];\n"
      "row([2 9]) = [20 90];\n"
      "matrix = [1 2; 3 4];\n"
      "matrix(:, 3) = [5; 6];\n"
      "matrix([1 4], 4) = [7; 8];\n"
      "matrix(:, 2) = [];\n"
      "cube = reshape([1 2 3 4 5 6 7 8], 2, 2, 2);\n"
      "cube(3, 2, 2) = 9;\n"
      "cube(:, 1, :) = [];\n"
      "disp(row(4), row(9), matrix(4, 3), cube(3, 1, 2))\n";
  const auto javascript =
      transpile_array(source, mpf::SourceLanguage::matlab, mpf::TargetLanguage::javascript);
  const auto cpp = transpile_array(source, mpf::SourceLanguage::matlab, mpf::TargetLanguage::cpp);
  REQUIRE(javascript.success());
  REQUIRE(cpp.success());
  REQUIRE(javascript.code.find("__mpf_grow") != std::string::npos);
  REQUIRE(javascript.code.find("__mpf_erase") != std::string::npos);
  REQUIRE(cpp.code.find("mpf_runtime::assign_growing_linear_column_major") != std::string::npos);
  REQUIRE(cpp.code.find("mpf_runtime::assign_growing_section_nd") != std::string::npos);
  REQUIRE(cpp.code.find("mpf_runtime::erase_indexed") != std::string::npos);
  REQUIRE(std::any_of(javascript.source_map.segments.begin(), javascript.source_map.segments.end(),
                      [](const auto& segment) { return segment.original_line == 10U; }));
  REQUIRE(std::any_of(cpp.source_map.segments.begin(), cpp.source_map.segments.end(),
                      [](const auto& segment) { return segment.original_line == 10U; }));
}

TEST_CASE("Matlab indexed deletion rejects ambiguous dimensions and invalid selectors") {
  const auto linear_matrix =
      transpile_array("matrix = [1 2; 3 4];\nmatrix([1 2]) = [];\n", mpf::SourceLanguage::matlab,
                      mpf::TargetLanguage::javascript);
  const auto element =
      transpile_array("matrix = [1 2; 3 4];\nmatrix(1, 1) = [];\n", mpf::SourceLanguage::matlab,
                      mpf::TargetLanguage::javascript);
  const auto outside =
      transpile_array("matrix = [1 2; 3 4];\nmatrix(:, 3) = [];\n", mpf::SourceLanguage::matlab,
                      mpf::TargetLanguage::javascript);
  const auto partial_tensor =
      transpile_array("tensor = reshape([1 2 3 4 5 6 7 8], 2, 2, 2);\ntensor(1, :, 1) = [];\n",
                      mpf::SourceLanguage::matlab, mpf::TargetLanguage::javascript);
  REQUIRE(!linear_matrix.success());
  REQUIRE(!element.success());
  REQUIRE(!outside.success());
  REQUIRE(!partial_tensor.success());
  REQUIRE(linear_matrix.diagnostics.front().code == "MPF2050");
  REQUIRE(element.diagnostics.front().code == "MPF2050");
  REQUIRE(outside.diagnostics.front().code == "MPF2021");
  REQUIRE(partial_tensor.diagnostics.front().code == "MPF2050");
}

TEST_CASE("Matlab logical indexing reads and writes in column-major linear order") {
  const std::string source =
      "values = [10 20 30 40];\n"
      "mask = [true false true false];\n"
      "selected = values(mask);\n"
      "values(mask) = [1 2];\n"
      "matrix = [1 2; 3 4];\n"
      "matrix_mask = [true false; false true];\n"
      "picked = matrix(matrix_mask);\n"
      "disp(selected(1) + selected(2) + values(1) + values(3) + picked(1) + picked(2))\n";
  const auto javascript =
      transpile_array(source, mpf::SourceLanguage::matlab, mpf::TargetLanguage::javascript);
  const auto cpp = transpile_array(source, mpf::SourceLanguage::matlab, mpf::TargetLanguage::cpp);
  REQUIRE(javascript.success());
  REQUIRE(cpp.success());
  REQUIRE(javascript.code.find("{ kind: \"logical\", value: mask }") != std::string::npos);
  REQUIRE(javascript.code.find("__mpf_selector_indices") != std::string::npos);
  REQUIRE(cpp.code.find("mpf_runtime::logical_selector{mask}") != std::string::npos);
  REQUIRE(cpp.code.find("selector_indices(std::size_t size") != std::string::npos);

  const auto mismatch =
      transpile_array("values = [1 2 3];\nmask = [true false];\nbad = values(mask);\n",
                      mpf::SourceLanguage::matlab, mpf::TargetLanguage::javascript);
  REQUIRE(!mismatch.success());
  REQUIRE(mismatch.diagnostics.front().code == "MPF2049");
}

TEST_CASE("Matlab generalized selectors preserve order duplicates dimensions and emptiness") {
  const std::string source =
      "values = [10 20 30 40];\n"
      "picked = values([4 2 2]);\n"
      "values([1 1 3]) = [5 6 7];\n"
      "empty = values([]);\n"
      "matrix = [1 2 3; 4 5 6; 7 8 9];\n"
      "rows = [true false true];\n"
      "columns = [false true true];\n"
      "block = matrix(rows, columns);\n"
      "matrix(rows, [3 1]) = [30 10; 90 70];\n"
      "mask = values > 6;\n"
      "selected = values(mask);\n"
      "disp(picked(1), picked(2), picked(3), length(empty), block(2, 2), "
      "matrix(1, 1), matrix(3, 3), selected(2))\n";
  const auto javascript =
      transpile_array(source, mpf::SourceLanguage::matlab, mpf::TargetLanguage::javascript);
  const auto cpp = transpile_array(source, mpf::SourceLanguage::matlab, mpf::TargetLanguage::cpp);
  REQUIRE(javascript.success());
  REQUIRE(cpp.success());
  REQUIRE(javascript.code.find("{ kind: \"numeric\", value: [4, 2, 2] }") != std::string::npos);
  REQUIRE(javascript.code.find("{ kind: \"logical\", value: rows }") != std::string::npos);
  REQUIRE(javascript.code.find("{ kind: \"empty\", value: __mpf_empty_array([0, 0]) }") !=
          std::string::npos);
  REQUIRE(cpp.code.find("mpf_runtime::numeric_selector{") != std::string::npos);
  REQUIRE(cpp.code.find("mpf_runtime::logical_selector{rows}") != std::string::npos);
  REQUIRE(cpp.code.find("mpf_runtime::empty_selector{std::vector<std::vector<double>>{}}") !=
          std::string::npos);
  REQUIRE(javascript.source_map.segments.size() >= 10U);
  REQUIRE(cpp.source_map.segments.size() >= 10U);

  const auto fractional =
      transpile_array("values = [1 2 3];\nbad = values([1 1.5]);\n", mpf::SourceLanguage::matlab,
                      mpf::TargetLanguage::javascript);
  const auto nonnumeric =
      transpile_array("values = [1 2 3];\nbad = values(['x']);\n", mpf::SourceLanguage::matlab,
                      mpf::TargetLanguage::javascript);
  REQUIRE(!fractional.success());
  REQUIRE(!nonnumeric.success());
  REQUIRE(fractional.diagnostics.front().code == "MPF2023");
  REQUIRE(nonnumeric.diagnostics.front().code == "MPF2023");
}

TEST_CASE("Matlab array comparisons compose with broadcast logical indexing") {
  const std::string source =
      "values = [10 20 30 40];\n"
      "selected = values(values >= 30);\n"
      "values(values < 25) = 0;\n"
      "matrix = [1 2 3; 4 5 6];\n"
      "picked = matrix(matrix > [2 2 2]);\n"
      "boolean_grid = [true false] == [true; false];\n"
      "reshaped_mask = reshape(matrix >= 3, 6, 1);\n"
      "disp(sum(selected) + sum(values) + sum(picked) + "
      "numel(boolean_grid(boolean_grid)) + numel(reshaped_mask(reshaped_mask)))\n";
  const auto javascript =
      transpile_array(source, mpf::SourceLanguage::matlab, mpf::TargetLanguage::javascript);
  const auto cpp = transpile_array(source, mpf::SourceLanguage::matlab, mpf::TargetLanguage::cpp);
  REQUIRE(javascript.success());
  REQUIRE(cpp.success());
  REQUIRE(javascript.code.find("__mpf_matlab_greater_equal(values, 30, [4], [1], [4])") !=
          std::string::npos);
  REQUIRE(javascript.code.find("__mpf_matlab_greater(matrix, [2, 2, 2], [2, 3], [1, 3], "
                               "[2, 3])") != std::string::npos);
  REQUIRE(cpp.code.find("mpf_runtime::matlab_greater_broadcast") != std::string::npos);
}

TEST_CASE("Matlab logical arrays and condition truthiness lower independently per target") {
  const std::string source =
      "row = [1 0 3];\n"
      "column = [1; 0];\n"
      "both = row & column;\n"
      "either = row | column;\n"
      "inverse = ~row;\n"
      "cube = reshape([1 0 2 0], 2, 1, 2);\n"
      "pages = reshape([0 1], 1, 1, 2);\n"
      "tensor_mask = cube | pages;\n"
      "full_condition = 0;\n"
      "if [1 2; 3 4]\n"
      "  full_condition = 1;\n"
      "end\n"
      "empty_condition = 1;\n"
      "if []\n"
      "  empty_condition = 0;\n"
      "end\n"
      "scalar_short = 0 && (1 / 0);\n"
      "scalar_condition = 0;\n"
      "if 1 | 0\n"
      "  scalar_condition = 1;\n"
      "end\n"
      "disp(numel(both(both)) + numel(either(either)) + numel(inverse(inverse)) + "
      "numel(tensor_mask(tensor_mask)) + full_condition + empty_condition + "
      "scalar_condition)\n"
      "disp(scalar_short)\n";
  const auto javascript =
      transpile_array(source, mpf::SourceLanguage::matlab, mpf::TargetLanguage::javascript);
  const auto cpp = transpile_array(source, mpf::SourceLanguage::matlab, mpf::TargetLanguage::cpp);
  REQUIRE(javascript.success());
  REQUIRE(cpp.success());
  REQUIRE(javascript.code.find("__mpf_matlab_and(row, column") != std::string::npos);
  REQUIRE(javascript.code.find("__mpf_matlab_or(cube, pages") != std::string::npos);
  REQUIRE(javascript.code.find("__mpf_matlab_not(row)") != std::string::npos);
  REQUIRE(javascript.code.find("if (__mpf_matlab_truthy(") != std::string::npos);
  REQUIRE(javascript.code.find("__mpf_matlab_scalar_logical(0) &&") != std::string::npos);
  REQUIRE(cpp.code.find("mpf_runtime::matlab_and_broadcast") != std::string::npos);
  REQUIRE(cpp.code.find("mpf_runtime::matlab_or_broadcast") != std::string::npos);
  REQUIRE(cpp.code.find("mpf_runtime::matlab_not(row)") != std::string::npos);
  REQUIRE(cpp.code.find("if (mpf_runtime::matlab_truthy(") != std::string::npos);
  REQUIRE(cpp.code.find("mpf_runtime::matlab_scalar_logical(0) &&") != std::string::npos);
  REQUIRE(cpp.code.find("mpf_runtime::ieee_divide(1, 0)") != std::string::npos);
  REQUIRE(cpp.code.find("if (right != 0.0) return left / right;") != std::string::npos);
  REQUIRE(cpp.code.find("std::signbit(left) != std::signbit(right)") != std::string::npos);
  REQUIRE(cpp.code.find("static_cast<double>(1) / static_cast<double>(0)") == std::string::npos);
  for (const auto* result : {&javascript, &cpp}) {
    REQUIRE(std::any_of(result->source_map.segments.begin(), result->source_map.segments.end(),
                        [](const auto& segment) { return segment.original_line == 3U; }));
    REQUIRE(std::any_of(result->source_map.segments.begin(), result->source_map.segments.end(),
                        [](const auto& segment) { return segment.original_line == 10U; }));
  }

  const auto short_array = transpile_array("bad = [1 0] && 1;\n", mpf::SourceLanguage::matlab,
                                           mpf::TargetLanguage::javascript);
  const auto contextual_array =
      transpile_array("if [1 0] & 1\n  disp(1)\nend\n", mpf::SourceLanguage::matlab,
                      mpf::TargetLanguage::javascript);
  REQUIRE(!short_array.success());
  REQUIRE(!contextual_array.success());
  REQUIRE(short_array.diagnostics.front().code == "MPF2051");
  REQUIRE(contextual_array.diagnostics.front().code == "MPF2051");
}

TEST_CASE("Matlab all and any preserve first-dimension vector N-D and empty identities") {
  const std::string source =
      "row = [1 2 0];\n"
      "matrix = [1 0 3; 4 5 6];\n"
      "cube = reshape([1 0 3 4 5 6 0 8], 2, 2, 2);\n"
      "empty_rows = reshape([], 0, 3);\n"
      "column_all = all(matrix);\n"
      "row_any = any(matrix, 2);\n"
      "pages_all = all(cube, [1 2]);\n"
      "empty_all = all(empty_rows);\n"
      "disp(all(row) + any(row) + column_all(1, 1) + row_any(2, 1) + "
      "pages_all(1, 1, 2) + empty_all(1, 3) + all([], 'all') + any(matrix, 'all'))\n";
  const auto javascript =
      transpile_array(source, mpf::SourceLanguage::matlab, mpf::TargetLanguage::javascript);
  const auto cpp = transpile_array(source, mpf::SourceLanguage::matlab, mpf::TargetLanguage::cpp);
  REQUIRE(javascript.success());
  REQUIRE(cpp.success());
  REQUIRE(javascript.code.find("__mpf_matlab_logical_reduce") != std::string::npos);
  REQUIRE(javascript.code.find("[0, 1], [2, 2, 2], [1, 1, 2], [1, 1, 2]") != std::string::npos);
  REQUIRE(cpp.code.find("mpf_runtime::matlab_logical_reduce<true, 3>") != std::string::npos);
  REQUIRE(cpp.code.find("std::array<std::size_t, 2>{0, 3}") != std::string::npos);
  for (const auto* result : {&javascript, &cpp}) {
    REQUIRE(std::any_of(result->source_map.segments.begin(), result->source_map.segments.end(),
                        [](const auto& segment) { return segment.original_line == 7U; }));
    REQUIRE(std::any_of(result->source_map.segments.begin(), result->source_map.segments.end(),
                        [](const auto& segment) { return segment.original_line == 9U; }));
  }

  const std::string dynamic_source =
      "disp(reduce_all([1 2 3]))\n"
      "function result = reduce_all(values)\n"
      "result = all(values, 'all');\n"
      "end\n";
  const auto dynamic_javascript =
      transpile_array(dynamic_source, mpf::SourceLanguage::matlab, mpf::TargetLanguage::javascript);
  const auto dynamic_cpp =
      transpile_array(dynamic_source, mpf::SourceLanguage::matlab, mpf::TargetLanguage::cpp);
  REQUIRE(dynamic_javascript.success());
  REQUIRE(dynamic_cpp.success());
  REQUIRE(dynamic_javascript.code.find("__mpf_matlab_logical_total(values, true)") !=
          std::string::npos);
  REQUIRE(dynamic_cpp.code.find("mpf_runtime::matlab_logical_total<true>(values)") !=
          std::string::npos);
}

TEST_CASE("Matlab all and any reject unstable or invalid reduction contracts") {
  const auto runtime_dimension =
      transpile_array("matrix = [1 2; 3 4];\ndimension = 2;\nbad = all(matrix, dimension);\n",
                      mpf::SourceLanguage::matlab, mpf::TargetLanguage::javascript);
  const auto duplicate_dimensions = transpile_array(
      "bad = any([1 2; 3 4], [1 1]);\n", mpf::SourceLanguage::matlab, mpf::TargetLanguage::cpp);
  const auto zero_dimension = transpile_array("bad = all([1 2], 0);\n", mpf::SourceLanguage::matlab,
                                              mpf::TargetLanguage::javascript);
  const auto character_input =
      transpile_array("bad = any('abc');\n", mpf::SourceLanguage::matlab, mpf::TargetLanguage::cpp);
  for (const auto* result :
       {&runtime_dimension, &duplicate_dimensions, &zero_dimension, &character_input}) {
    REQUIRE(!result->success());
    REQUIRE(result->diagnostics.front().code == "MPF2052");
  }
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
  REQUIRE(cpp.code.find("mpf_runtime::length(matrix, std::array<std::size_t, 2>{2, 2})") !=
          std::string::npos);
}

TEST_CASE("Matlab matrix and element-wise arithmetic lower through target-owned plans") {
  const std::string source =
      "left = [1 2; 3 4];\n"
      "right = [5 6; 7 8];\n"
      "product = left * right;\n"
      "values = (left + 1) .* 2 ./ right;\n"
      "powers = left .^ 2;\n"
      "reversed = 2 .\\ right;\n"
      "disp(product(1, 1) + values(1, 1) + powers(1, 1) + reversed(1, 1))\n";
  const auto javascript =
      transpile_array(source, mpf::SourceLanguage::matlab, mpf::TargetLanguage::javascript);
  const auto cpp = transpile_array(source, mpf::SourceLanguage::matlab, mpf::TargetLanguage::cpp);
  REQUIRE(javascript.success());
  REQUIRE(cpp.success());
  REQUIRE(javascript.code.find("__mpf_matlab_mtimes(left, right)") != std::string::npos);
  REQUIRE(javascript.code.find("__mpf_matlab_add(left, 1, [2, 2], [1, 1], [2, 2])") !=
          std::string::npos);
  REQUIRE(javascript.code.find("__mpf_matlab_power(left, 2, [2, 2], [1, 1], [2, 2])") !=
          std::string::npos);
  REQUIRE(javascript.code.find("__mpf_matlab_left_divide(2, right, [1, 1], [2, 2], [2, 2])") !=
          std::string::npos);
  REQUIRE(cpp.code.find("mpf_runtime::matlab_mtimes(left, right)") != std::string::npos);
  REQUIRE(cpp.code.find("mpf_runtime::matlab_add_broadcast") != std::string::npos);
  REQUIRE(cpp.code.find("mpf_runtime::matlab_power_broadcast") != std::string::npos);
  REQUIRE(cpp.code.find("mpf_runtime::matlab_left_divide_broadcast") != std::string::npos);
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
  REQUIRE(cpp.code.find("mpf_runtime::linear_section_column_major(") != std::string::npos);
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
      REQUIRE(cpp.code.find("mpf_runtime::assign_linear_section_column_major(matrix") !=
              std::string::npos);
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
