#include <algorithm>
#include <string>

#include "mpf/transpiler.hpp"
#include "test_framework.hpp"

namespace {

mpf::TranspileResult python(const std::string& source,
                            const mpf::TargetLanguage target = mpf::TargetLanguage::javascript) {
  mpf::TranspileOptions options;
  options.language = mpf::SourceLanguage::python;
  options.target = target;
  options.emit_source_banner = false;
  return mpf::Transpiler{}.transpile(source, options);
}

mpf::TranspileResult matlab(const std::string& source,
                            const mpf::TargetLanguage target = mpf::TargetLanguage::javascript) {
  mpf::TranspileOptions options;
  options.language = mpf::SourceLanguage::matlab;
  options.target = target;
  options.emit_source_banner = false;
  return mpf::Transpiler{}.transpile(source, options);
}

}  // namespace

TEST_CASE("semantic analysis rejects undefined identifiers") {
  const auto result = python("print(missing)\n");
  REQUIRE(!result.success());
  REQUIRE(result.code.empty());
  REQUIRE(!result.diagnostics.empty());
  REQUIRE(result.diagnostics.front().code == "MPF2001");
}

TEST_CASE("semantic analysis rejects use before definite assignment") {
  const auto result = python("print(value)\nvalue = 1\n");
  REQUIRE(!result.success());
  REQUIRE(result.diagnostics.front().code == "MPF2003");
}

TEST_CASE("semantic analysis rejects zero and non-integer Python range steps") {
  const auto zero = python("for index in range(0, 3, 0):\n    print(index)\n");
  REQUIRE(!zero.success());
  REQUIRE(zero.diagnostics.front().code == "MPF2005");
  const auto real = python("for index in range(0, 3, 0.5):\n    print(index)\n");
  REQUIRE(!real.success());
  REQUIRE(real.diagnostics.front().code == "MPF2006");
}

TEST_CASE("user functions shadow mathematical builtins") {
  const std::string source =
      "def abs(value):\n"
      "    return value + 100\n"
      "print(abs(1))\n";
  const auto javascript = python(source);
  const auto cpp = python(source, mpf::TargetLanguage::cpp);
  REQUIRE(javascript.success());
  REQUIRE(cpp.success());
  REQUIRE(javascript.code.find("Math.abs") == std::string::npos);
  REQUIRE(cpp.code.find("std::abs") == std::string::npos);
}

TEST_CASE("target reserved words are mangled without collisions") {
  const std::string source = "delete = 40\ntemplate = 2\nprint(delete + template)\n";
  const auto javascript = python(source);
  const auto cpp = python(source, mpf::TargetLanguage::cpp);
  REQUIRE(javascript.success());
  REQUIRE(cpp.success());
  REQUIRE(javascript.code.find("mpf_delete") != std::string::npos);
  REQUIRE(cpp.code.find("mpf_template") != std::string::npos);
}

TEST_CASE("dynamic reassignment remains valid for JavaScript but fails closed for C++17") {
  const std::string source = "value = 1\nvalue = 'text'\nprint(value)\n";
  const auto javascript = python(source);
  const auto cpp = python(source, mpf::TargetLanguage::cpp);
  REQUIRE(javascript.success());
  REQUIRE(!cpp.success());
  REQUIRE(cpp.code.empty());
  REQUIRE(cpp.diagnostics.front().code == "MPF2007");
}

TEST_CASE("Python mixed and-or result types remain JavaScript-only") {
  const std::string source = "value = 0 and 'text'\nprint(value)\n";
  const std::string calls =
      "def left():\n"
      "    return 0\n"
      "def right():\n"
      "    return 'text'\n"
      "print(left() and right())\n";
  const std::string parameters =
      "def choose(left, right):\n"
      "    return left or right\n"
      "print(choose(0, 2))\n";
  const auto javascript = python(source);
  const auto cpp = python(source, mpf::TargetLanguage::cpp);
  REQUIRE(javascript.success());
  REQUIRE(!cpp.success());
  REQUIRE(cpp.diagnostics.front().code == "MPF2032");
  REQUIRE(python(calls).success());
  REQUIRE(python(parameters).success());
  const auto calls_cpp = python(calls, mpf::TargetLanguage::cpp);
  const auto parameters_cpp = python(parameters, mpf::TargetLanguage::cpp);
  REQUIRE(!calls_cpp.success());
  REQUIRE(!parameters_cpp.success());
  REQUIRE(calls_cpp.diagnostics.front().code == "MPF2032");
  REQUIRE(parameters_cpp.diagnostics.front().code == "MPF2032");
}

TEST_CASE("Python conditional ordering identity and membership fail closed at owned boundaries") {
  const std::string mixed_conditional = "flag = True\nvalue = 1 if flag else 'one'\nprint(value)\n";
  const std::string mixed_equality = "value = 1 == '1'\nprint(value)\n";
  const std::string invalid_ordering = "value = 1 < '2' < 3\nprint(value)\n";
  const std::string invalid_identity = "value = 1 is 1\nprint(value)\n";
  const std::string invalid_membership = "value = 1 in 2\nprint(value)\n";
  const std::string invalid_string_membership = "value = 1 in '123'\nprint(value)\n";
  REQUIRE(python(mixed_conditional).success());
  REQUIRE(python(mixed_equality).success());
  const auto conditional_cpp = python(mixed_conditional, mpf::TargetLanguage::cpp);
  const auto equality_cpp = python(mixed_equality, mpf::TargetLanguage::cpp);
  const auto ordering = python(invalid_ordering);
  const auto identity = python(invalid_identity);
  const auto membership = python(invalid_membership);
  const auto string_membership = python(invalid_string_membership);
  REQUIRE(!conditional_cpp.success());
  REQUIRE(equality_cpp.success());
  REQUIRE(!ordering.success());
  REQUIRE(!identity.success());
  REQUIRE(!membership.success());
  REQUIRE(!string_membership.success());
  REQUIRE(conditional_cpp.diagnostics.front().code == "MPF2044");
  REQUIRE(ordering.diagnostics.front().code == "MPF2044");
  REQUIRE(identity.diagnostics.front().code == "MPF2045");
  REQUIRE(membership.diagnostics.front().code == "MPF2045");
  REQUIRE(string_membership.diagnostics.front().code == "MPF2045");
}

TEST_CASE("Python float conversion validates arity and supported source types") {
  const auto valid = python("value = float('nan')\nif value:\n    print(1)\n");
  const auto missing = python("value = float()\n");
  const auto list = python("value = float([1])\n");
  REQUIRE(valid.success());
  REQUIRE(!missing.success());
  REQUIRE(!list.success());
  REQUIRE(missing.diagnostics.front().code == "MPF2033");
  REQUIRE(list.diagnostics.front().code == "MPF2033");
}

TEST_CASE("Matlab complex semantics authorize square matrix kernels and fail closed beyond them") {
  const auto valid = matlab(
      "z = complex(1, 2);\n"
      "w = (z + 3i) ./ (2 - 1i);\n"
      "disp(real(w), imag(w), abs(w), real(conj(w)))\n");
  const auto valid_cpp = matlab(
      "z = complex(1, 2);\n"
      "w = (z + 3i) ./ (2 - 1i);\n"
      "disp(real(w), imag(w), abs(w), real(conj(w)))\n",
      mpf::TargetLanguage::cpp);
  REQUIRE(valid.success());
  REQUIRE(valid_cpp.success());

  const auto comparison = matlab("value = 1i < 2i;\ndisp(value)\n");
  const auto matrix_product =
      matlab("left = [1i 2; 3 4];\nright = left * left;\ndisp(real(right(1,1)))\n");
  const auto matrix_product_cpp =
      matlab("left = [1i 2; 3 4];\nright = left * left;\ndisp(real(right(1,1)))\n",
             mpf::TargetLanguage::cpp);
  const auto rectangular_solve =
      matlab("left = [1i 2; 3 4; 5 6];\nright = left \\ [1; 2; 3];\ndisp(right)\n");
  const auto reduction = matlab("value = sum([1i 2i]);\ndisp(value)\n");
  const auto invalid_constructor = matlab("value = complex(1i, 2);\ndisp(value)\n");
  const auto has_complex_boundary = [](const mpf::TranspileResult& result) {
    return std::any_of(result.diagnostics.begin(), result.diagnostics.end(),
                       [](const auto& diagnostic) { return diagnostic.code == "MPF2053"; });
  };
  REQUIRE(!comparison.success());
  REQUIRE(matrix_product.success());
  REQUIRE(matrix_product_cpp.success());
  REQUIRE(matrix_product.code.find("__mpf_matlab_complex_mtimes") != std::string::npos);
  REQUIRE(matrix_product_cpp.code.find("mpf_runtime::matlab_complex_mtimes") != std::string::npos);
  REQUIRE(!rectangular_solve.success());
  REQUIRE(!reduction.success());
  REQUIRE(!invalid_constructor.success());
  REQUIRE(has_complex_boundary(comparison));
  REQUIRE(has_complex_boundary(rectangular_solve));
  REQUIRE(has_complex_boundary(reduction));
  REQUIRE(has_complex_boundary(invalid_constructor));
}

TEST_CASE("Matlab imaginary unit names are builtin constants until shadowed") {
  const auto builtin = matlab("disp(imag(i), imag(j))\n");
  const auto builtin_cpp = matlab("disp(imag(i), imag(j))\n", mpf::TargetLanguage::cpp);
  REQUIRE(builtin.success());
  REQUIRE(builtin_cpp.success());
  REQUIRE(builtin.code.find("__mpf_complex(0, 1)") != std::string::npos);
  REQUIRE(builtin_cpp.code.find("std::complex<double>{0.0, 1.0}") != std::string::npos);

  const auto shadowed = matlab("i = 40;\nj = 2;\ndisp(i + j)\n");
  const auto shadowed_cpp = matlab("i = 40;\nj = 2;\ndisp(i + j)\n", mpf::TargetLanguage::cpp);
  REQUIRE(shadowed.success());
  REQUIRE(shadowed_cpp.success());
  REQUIRE(shadowed.code.find("__mpf_complex_tag") == std::string::npos);
  REQUIRE(shadowed_cpp.code.find("#include <complex>") == std::string::npos);
}

TEST_CASE("Matlab array operators validate matrix solve and power contracts") {
  const auto mismatched = matlab("value = [1 2] + [1 2 3];\ndisp(value)\n");
  const auto matrix_divide = matlab("value = [1 2; 3 4] / [1 0; 0 1];\ndisp(value)\n");
  const auto rectangular_left = matlab("value = [1 2; 3 4; 5 6] \\ [1; 2; 3];\ndisp(value)\n");
  const auto wide_right = matlab("value = [1 2 3] / [1 2 3; 4 5 6];\ndisp(value)\n");
  const auto tall_right = matlab("value = [1 2] / [1 0; 0 1; 1 1];\ndisp(value)\n");
  const auto incompatible_right = matlab("value = [1 2] / [1 2 3; 4 5 6];\ndisp(value)\n");
  const auto nonsquare_power = matlab("value = [1 2 3; 4 5 6] ^ 2;\ndisp(value)\n");
  const auto matrix_exponent = matlab("value = [1 2; 3 4] ^ [2];\ndisp(value)\n");
  const auto fractional_power = matlab("value = [1 2; 3 4] ^ 0.5;\ndisp(value)\n");
  const auto unsafe_power = matlab("value = [1 2; 3 4] ^ 9007199254740992;\ndisp(value)\n");
  REQUIRE(!mismatched.success());
  REQUIRE(matrix_divide.success());
  REQUIRE(rectangular_left.success());
  REQUIRE(wide_right.success());
  REQUIRE(tall_right.success());
  REQUIRE(!incompatible_right.success());
  REQUIRE(!nonsquare_power.success());
  REQUIRE(!matrix_exponent.success());
  REQUIRE(!fractional_power.success());
  REQUIRE(!unsafe_power.success());
  REQUIRE(mismatched.code.empty());
  REQUIRE(mismatched.diagnostics.front().code == "MPF2046");
  REQUIRE(incompatible_right.diagnostics.front().code == "MPF2046");
  REQUIRE(nonsquare_power.diagnostics.front().code == "MPF2046");
  REQUIRE(matrix_exponent.diagnostics.front().code == "MPF2046");
  REQUIRE(fractional_power.diagnostics.front().code == "MPF2046");
  REQUIRE(unsafe_power.diagnostics.front().code == "MPF2046");
}

TEST_CASE("Matlab selector analysis distinguishes scalar slice numeric logical and empty") {
  const auto generalized = matlab(
      "values = [10 20 30 40];\n"
      "numeric = values([4 2 2]);\n"
      "empty = values([]);\n"
      "mask = values > 15;\n"
      "logical = values(mask);\n"
      "matrix = [1 2 3; 4 5 6];\n"
      "rows = [true false];\n"
      "columns = matrix(rows, [3 1]);\n"
      "disp(numeric, empty, logical, columns)\n");
  const auto mismatched = matlab(
      "matrix = [1 2 3; 4 5 6];\n"
      "rows = [true false true];\n"
      "bad = matrix(rows, :);\n");
  REQUIRE(generalized.success());
  REQUIRE(!mismatched.success());
  REQUIRE(mismatched.diagnostics.front().code == "MPF2049");
}

TEST_CASE("function bodies may bind globals initialized before the function is called") {
  const std::string source =
      "def answer():\n"
      "    return value\n"
      "value = 42\n"
      "print(answer())\n";
  REQUIRE(python(source).success());
  REQUIRE(python(source, mpf::TargetLanguage::cpp).success());
}

TEST_CASE("loop control and return statements are rejected outside their contexts") {
  const auto break_result = python("break\n");
  const auto continue_result = python("continue\n");
  const auto return_result = python("return 1\n");
  REQUIRE(!break_result.success());
  REQUIRE(!continue_result.success());
  REQUIRE(!return_result.success());
  REQUIRE(break_result.diagnostics.front().code == "MPF2010");
  REQUIRE(continue_result.diagnostics.front().code == "MPF2011");
  REQUIRE(return_result.diagnostics.front().code == "MPF2012");
}

TEST_CASE("CFG analysis warns about unreachable statements without failing transpilation") {
  const auto result = python(
      "for index in range(3):\n"
      "    break\n"
      "    print(index)\n");
  REQUIRE(result.success());
  REQUIRE(!result.diagnostics.empty());
  REQUIRE(result.diagnostics.front().severity == mpf::DiagnosticSeverity::warning);
  REQUIRE(result.diagnostics.front().code == "MPF2101");
}

TEST_CASE("C++17 return paths require compatible types and no implicit null fallthrough") {
  const std::string incompatible =
      "def choose(flag):\n"
      "    if flag:\n"
      "        return 1\n"
      "    else:\n"
      "        return 'one'\n";
  const std::string fallthrough =
      "def maybe(flag):\n"
      "    if flag:\n"
      "        return 1\n";
  REQUIRE(python(incompatible).success());
  const auto incompatible_cpp = python(incompatible, mpf::TargetLanguage::cpp);
  const auto fallthrough_cpp = python(fallthrough, mpf::TargetLanguage::cpp);
  REQUIRE(!incompatible_cpp.success());
  REQUIRE(!fallthrough_cpp.success());
  REQUIRE(incompatible_cpp.diagnostics.front().code == "MPF2008");
  REQUIRE(fallthrough_cpp.diagnostics.front().code == "MPF2009");
}
