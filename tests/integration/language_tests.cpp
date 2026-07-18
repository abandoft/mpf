#include <algorithm>
#include <string>

#include "mpf/transpiler.hpp"
#include "test_framework.hpp"

namespace {

mpf::TranspileResult transpile(const std::string& source, const mpf::SourceLanguage language,
                               const mpf::TargetLanguage target = mpf::TargetLanguage::javascript) {
  mpf::TranspileOptions options;
  options.language = language;
  options.target = target;
  options.emit_source_banner = false;
  return mpf::Transpiler{}.transpile(source, options);
}

TEST_CASE("C++17 target emits a function template and executable program") {
  const auto result = transpile(
      "def magnitude(x):\n"
      "    if x < 0:\n"
      "        x = -x\n"
      "    return x\n"
      "print(magnitude(-42))\n",
      mpf::SourceLanguage::python, mpf::TargetLanguage::cpp);
  REQUIRE(result.success());
  REQUIRE(result.code.find("template <typename T0>") != std::string::npos);
  REQUIRE(result.code.find("auto magnitude(T0 x)") != std::string::npos);
  REQUIRE(result.code.find("int main()") != std::string::npos);
  REQUIRE(result.code.find("mpf_runtime::print(magnitude(-42));") != std::string::npos);
}

TEST_CASE("both backends lower power and floor division from the same AST") {
  const std::string source = "power = -2 ** 2\nquotient = 7 // 2\nprint(power, quotient)\n";
  const auto javascript = transpile(source, mpf::SourceLanguage::python);
  const auto cpp = transpile(source, mpf::SourceLanguage::python, mpf::TargetLanguage::cpp);
  REQUIRE(javascript.success());
  REQUIRE(cpp.success());
  REQUIRE(javascript.code.find("power = -(2 ** 2);") != std::string::npos);
  REQUIRE(javascript.code.find("__mpf_python_floor_divide(7, 2)") != std::string::npos);
  REQUIRE(cpp.code.find("std::pow(2, 2)") != std::string::npos);
  REQUIRE(cpp.code.find("mpf_runtime::python_floor_divide(7, 2)") != std::string::npos);
}

TEST_CASE("C++17 target preserves Python strings and true division") {
  const auto result = transpile("message = 'value'\nratio = 7 / 2\nprint(message, ratio)\n",
                                mpf::SourceLanguage::python, mpf::TargetLanguage::cpp);
  REQUIRE(result.success());
  REQUIRE(result.code.find("std::string{\"value\"}") != std::string::npos);
  REQUIRE(result.code.find("mpf_runtime::python_true_divide(7, 2)") != std::string::npos);
}

TEST_CASE("Python chained comparisons and conditional expressions lower lazily in both backends") {
  const std::string source =
      "def probe(value):\n"
      "    print(value)\n"
      "    return value\n"
      "first = 1 < probe(2) < 3\n"
      "second = 3 < probe(2) < probe(99)\n"
      "choice = probe(40) + 2 if first and not second else probe(0)\n"
      "nested = 1 if False else 2 if True else probe(98)\n"
      "values = [1, 2] if first else [9]\n"
      "numeric_equality = 7 if True == 1 == 1.0 else 0\n"
      "sequence_equality = 8 if [1, 2] == [1, 2] != [2, 1] else 0\n"
      "print(choice, nested, sum(values), numeric_equality, sequence_equality)\n";
  const auto javascript = transpile(source, mpf::SourceLanguage::python);
  const auto cpp = transpile(source, mpf::SourceLanguage::python, mpf::TargetLanguage::cpp);
  REQUIRE(javascript.success());
  REQUIRE(cpp.success());
  REQUIRE(javascript.code.find("if (!(mpf_internal_comparison") != std::string::npos);
  REQUIRE(javascript.code.find("__mpf_py_equal(mpf_internal_comparison") != std::string::npos);
  REQUIRE(javascript.code.find("__mpf_truthy(first)") != std::string::npos);
  REQUIRE(cpp.code.find("auto&& mpf_internal_comparison") != std::string::npos);
  REQUIRE(cpp.code.find("mpf_runtime::truthy(first)") != std::string::npos);
}

TEST_CASE("Python equality identity and membership have target-explicit lowering") {
  const std::string portable =
      "items = [1, 2]\n"
      "pair = (1, 'x')\n"
      "result = 1 if 1 in items and 3 not in pair and 'bc' in 'abcd' else 0\n"
      "singletons = 2 if None is None and True is not False else 0\n"
      "kinds = 3 if [1, 2] != (1, 2) and 1 != '1' else 0\n"
      "chain = 4 if 1 in items is not None else 0\n"
      "print(result, singletons, kinds, chain)\n";
  const auto javascript = transpile(portable, mpf::SourceLanguage::python);
  const auto cpp = transpile(portable, mpf::SourceLanguage::python, mpf::TargetLanguage::cpp);
  REQUIRE(javascript.success());
  REQUIRE(cpp.success());
  REQUIRE(javascript.code.find("__mpf_py_contains") != std::string::npos);
  REQUIRE(javascript.code.find("__mpf_py_is") != std::string::npos);
  REQUIRE(javascript.code.find("__mpf_tuple([") != std::string::npos);
  REQUIRE(cpp.code.find("mpf_runtime::py_contains") != std::string::npos);
  REQUIRE(cpp.code.find("mpf_runtime::py_is") != std::string::npos);
  REQUIRE(cpp.code.find("mpf_runtime::py_equal") != std::string::npos);

  const std::string object_identity =
      "items = [1, 2]\n"
      "same = items\n"
      "print(1 if items is same else 0)\n";
  REQUIRE(transpile(object_identity, mpf::SourceLanguage::python).success());
  const auto identity_cpp =
      transpile(object_identity, mpf::SourceLanguage::python, mpf::TargetLanguage::cpp);
  REQUIRE(!identity_cpp.success());
  REQUIRE(identity_cpp.diagnostics.front().code == "MPF2044");
}

}  // namespace

TEST_CASE("Python function, branch, return and call transpile end to end") {
  const auto result = transpile(
      "def magnitude(x):\n"
      "    if x < 0:\n"
      "        x = -x\n"
      "    return x\n"
      "print(magnitude(-4))\n",
      mpf::SourceLanguage::python);
  REQUIRE(result.success());
  REQUIRE(result.code.find("export function magnitude(x)") != std::string::npos);
  REQUIRE(result.code.find("if (__mpf_truthy(x < 0))") != std::string::npos);
  REQUIRE(result.code.find("return x;") != std::string::npos);
  REQUIRE(result.code.find("console.log(magnitude(-4));") != std::string::npos);
}

TEST_CASE("Python defaults positional-only and keyword-only parameters normalize once") {
  const std::string source =
      "def combine(left, /, right=2, *, scale=1):\n"
      "    return (left + right) * scale\n"
      "print(combine(40), combine(40, scale=2), "
      "combine(40, scale=2, right=1))\n";
  const auto javascript =
      transpile(source, mpf::SourceLanguage::python, mpf::TargetLanguage::javascript);
  const auto cpp = transpile(source, mpf::SourceLanguage::python, mpf::TargetLanguage::cpp);
  REQUIRE(javascript.success());
  REQUIRE(cpp.success());
  REQUIRE(javascript.code.find("function combine(left, right = 2, scale = 1)") !=
          std::string::npos);
  REQUIRE(javascript.code.find("combine(40, 2, 1)") != std::string::npos);
  REQUIRE(javascript.code.find("combine(40, 2, 2)") != std::string::npos);
  REQUIRE(javascript.code.find("combine(40, 1, 2)") != std::string::npos);
  REQUIRE(cpp.code.find("combine(40, 2, 1)") != std::string::npos);
  REQUIRE(cpp.code.find("combine(40, 2, 2)") != std::string::npos);
  REQUIRE(cpp.code.find("combine(40, 1, 2)") != std::string::npos);
}

TEST_CASE("Python parameter association rejects invalid signatures calls and defaults") {
  const auto non_default_after_default = transpile(
      "def invalid(first=1, second):\n    return first + second\n", mpf::SourceLanguage::python);
  const auto positional_by_keyword =
      transpile("def identity(value, /):\n    return value\nprint(identity(value=1))\n",
                mpf::SourceLanguage::python);
  const auto keyword_as_positional =
      transpile("def add(value, /, *, offset):\n    return value + offset\nprint(add(1, 2))\n",
                mpf::SourceLanguage::python);
  const auto missing_keyword_only =
      transpile("def add(value, /, *, offset):\n    return value + offset\nprint(add(1))\n",
                mpf::SourceLanguage::python);
  const auto unknown_keyword =
      transpile("def identity(value):\n    return value\nprint(identity(missing=1))\n",
                mpf::SourceLanguage::python);
  const auto duplicate =
      transpile("def add(left, right):\n    return left + right\nprint(add(1, left=2))\n",
                mpf::SourceLanguage::python);
  const auto mutable_default =
      transpile("def invalid(values=[]):\n    return values\n", mpf::SourceLanguage::python);
  const auto variadic =
      transpile("def invalid(*values):\n    return 1\n", mpf::SourceLanguage::python);
  REQUIRE(!non_default_after_default.success());
  REQUIRE(!positional_by_keyword.success());
  REQUIRE(!keyword_as_positional.success());
  REQUIRE(!missing_keyword_only.success());
  REQUIRE(!unknown_keyword.success());
  REQUIRE(!duplicate.success());
  REQUIRE(!mutable_default.success());
  REQUIRE(!variadic.success());
  REQUIRE(positional_by_keyword.diagnostics.front().code == "MPF2041");
  REQUIRE(keyword_as_positional.diagnostics.front().code == "MPF2034");
  REQUIRE(missing_keyword_only.diagnostics.front().code == "MPF2034");
  REQUIRE(unknown_keyword.diagnostics.front().code == "MPF2041");
  REQUIRE(duplicate.diagnostics.front().code == "MPF2041");
  REQUIRE(mutable_default.diagnostics.front().code == "MPF2041");
}

TEST_CASE("Python fixed sequence unpacking lowers once in both backends") {
  const std::string source =
      "def pair(value):\n"
      "    return value + 1, value + 2\n"
      "def forward(value):\n"
      "    return pair(value)\n"
      "first, second = forward(39)\n"
      "(first, second) = (second, first)\n"
      "[left, right] = [20, 22]\n"
      "single, = (42,)\n"
      "label, answer = ('answer', 42)\n"
      "stored = (20, 22)\n"
      "stored_left, stored_right = stored\n"
      "stored_list = [20, 22]\n"
      "list_left, list_right = stored_list\n"
      "same, same = (1, 42)\n"
      "print(first, second, left + right, single, label, answer, "
      "stored_left + stored_right, list_left + list_right, same)\n";
  const auto javascript =
      transpile(source, mpf::SourceLanguage::python, mpf::TargetLanguage::javascript);
  const auto cpp = transpile(source, mpf::SourceLanguage::python, mpf::TargetLanguage::cpp);
  REQUIRE(javascript.success());
  REQUIRE(cpp.success());
  REQUIRE(javascript.code.find("const mpf_internal_unpack_") != std::string::npos);
  REQUIRE(javascript.code.find("first = mpf_internal_unpack_") != std::string::npos);
  REQUIRE(javascript.code.find("second = mpf_internal_unpack_") != std::string::npos);
  REQUIRE(cpp.code.find("const auto mpf_internal_outputs_") != std::string::npos);
  REQUIRE(cpp.code.find("std::get<0>(mpf_internal_outputs_") != std::string::npos);
  REQUIRE(cpp.code.find(".at(0);") != std::string::npos);
}

TEST_CASE("Python nested and starred assignment patterns lower independently") {
  const std::string source =
      "def payload():\n"
      "    return ((10, [20, 21, 22, 23]), 42)\n"
      "(left, [inner_left, *middle, inner_right]), answer = payload()\n"
      "head, *empty, tail = (20, 22)\n"
      "print(left, inner_left, sum(middle), inner_right, answer, "
      "head + tail, len(empty))\n";
  const auto javascript =
      transpile(source, mpf::SourceLanguage::python, mpf::TargetLanguage::javascript);
  const auto cpp = transpile(source, mpf::SourceLanguage::python, mpf::TargetLanguage::cpp);
  REQUIRE(javascript.success());
  REQUIRE(cpp.success());
  REQUIRE(javascript.code.find("middle = [mpf_internal_unpack_") != std::string::npos);
  REQUIRE(javascript.code.find("empty = [];") != std::string::npos);
  REQUIRE(cpp.code.find("std::vector<std::int64_t> middle") != std::string::npos);
  REQUIRE(cpp.code.find("std::vector<double> empty") != std::string::npos);
  REQUIRE(cpp.code.find("std::get<0>(std::get<0>(mpf_internal_outputs_") != std::string::npos);
}

TEST_CASE("heterogeneous Python starred capture remains JavaScript-only") {
  const std::string source =
      "head, *middle, tail = (0, 1, 'two', 3)\n"
      "print(head, middle, tail)\n";
  const auto javascript =
      transpile(source, mpf::SourceLanguage::python, mpf::TargetLanguage::javascript);
  const auto cpp = transpile(source, mpf::SourceLanguage::python, mpf::TargetLanguage::cpp);
  REQUIRE(javascript.success());
  REQUIRE(!cpp.success());
  REQUIRE(cpp.diagnostics.front().code == "MPF2020");
}

TEST_CASE("Python unpacking rejects dynamic duplicate-star and mismatched patterns") {
  const auto scalar = transpile("left, right = 42\n", mpf::SourceLanguage::python);
  const auto short_tuple = transpile("left, right = (42,)\n", mpf::SourceLanguage::python);
  const auto long_list = transpile("left, right = [1, 2, 3]\n", mpf::SourceLanguage::python);
  const auto duplicate_star =
      transpile("left, *middle, *right = (1, 2, 3)\n", mpf::SourceLanguage::python);
  const auto nested_mismatch =
      transpile("left, (middle, right) = (1, (2,))\n", mpf::SourceLanguage::python);
  const auto dynamic = transpile(
      "def values(flag):\n"
      "    if flag:\n"
      "        return (1, 2)\n"
      "    return (1, 2, 3)\n"
      "left, right = values(True)\n",
      mpf::SourceLanguage::python);
  REQUIRE(!scalar.success());
  REQUIRE(!short_tuple.success());
  REQUIRE(!long_list.success());
  REQUIRE(!duplicate_star.success());
  REQUIRE(!nested_mismatch.success());
  REQUIRE(!dynamic.success());
  REQUIRE(scalar.diagnostics.front().code == "MPF2042");
  REQUIRE(short_tuple.diagnostics.front().code == "MPF2042");
  REQUIRE(long_list.diagnostics.front().code == "MPF2042");
  REQUIRE(duplicate_star.diagnostics.front().code == "MPF1200");
  REQUIRE(nested_mismatch.diagnostics.front().code == "MPF2042");
  REQUIRE(dynamic.diagnostics.front().code == "MPF2042");
}

TEST_CASE("Python tokenized statement parser separates syntax from expression contents") {
  const std::string source =
      "def classify(value, offset,) -> int:\n"
      "    result = value + offset\n"
      "    if result == 42:\n"
      "        return result\n"
      "    elif result < 0:\n"
      "        return 0\n"
      "    else:\n"
      "        return 1\n"
      "text = 'a=b:c'\n"
      "answer = classify(40, 2)\n"
      "if text == 'a=b:c':\n"
      "    print(answer)\n";
  for (const auto target : {mpf::TargetLanguage::javascript, mpf::TargetLanguage::cpp}) {
    const auto result = transpile(source, mpf::SourceLanguage::python, target);
    REQUIRE(result.success());
    REQUIRE(result.code.find("classify") != std::string::npos);
    REQUIRE(result.code.find("a=b:c") != std::string::npos);
  }
}

TEST_CASE("Python tokenized statement parser rejects malformed structures with recovery") {
  const auto chained = transpile("first = second = 1\n", mpf::SourceLanguage::python);
  const auto parameter =
      transpile("def bad(value: int):\n    return value\n", mpf::SourceLanguage::python);
  const auto orphan = transpile("else:\n    print(1)\nprint(2)\n", mpf::SourceLanguage::python);
  REQUIRE(!chained.success());
  REQUIRE(!parameter.success());
  REQUIRE(!orphan.success());
  REQUIRE(chained.diagnostics.front().code == "MPF1200");
  REQUIRE(parameter.diagnostics.front().code == "MPF1200");
  REQUIRE(orphan.diagnostics.front().code == "MPF1200");
  REQUIRE(orphan.diagnostics.front().location.line == 1);
}

TEST_CASE("function dependency order supports forward calls and direct recursion") {
  const std::string source =
      "def entry():\n"
      "    return helper()\n"
      "def helper():\n"
      "    return 42\n"
      "def factorial(value):\n"
      "    if value <= 1:\n"
      "        return 1\n"
      "    return value * factorial(value - 1)\n"
      "print(factorial(5), entry())\n";
  const auto javascript = transpile(source, mpf::SourceLanguage::python);
  const auto cpp = transpile(source, mpf::SourceLanguage::python, mpf::TargetLanguage::cpp);
  REQUIRE(javascript.success());
  REQUIRE(cpp.success());
  REQUIRE(cpp.code.find("auto helper()") < cpp.code.find("auto entry()"));
  REQUIRE(cpp.code.find("std::int64_t factorial(T0 value);") != std::string::npos);
  REQUIRE(cpp.code.find("std::int64_t factorial(T0 value) {") != std::string::npos);
}

TEST_CASE("known mutual recursion receives C++17 forward declarations") {
  const std::string source =
      "def is_even(value):\n"
      "    if value == 0:\n"
      "        return True\n"
      "    return is_odd(value - 1)\n"
      "def is_odd(value):\n"
      "    if value == 0:\n"
      "        return False\n"
      "    return is_even(value - 1)\n"
      "print(is_even(10), is_odd(10))\n";
  const auto javascript = transpile(source, mpf::SourceLanguage::python);
  const auto cpp = transpile(source, mpf::SourceLanguage::python, mpf::TargetLanguage::cpp);
  REQUIRE(javascript.success());
  REQUIRE(cpp.success());
  REQUIRE(cpp.code.find("bool is_even(T0 value);") != std::string::npos);
  REQUIRE(cpp.code.find("bool is_odd(T0 value);") != std::string::npos);
}

TEST_CASE("unknown recursive returns remain JavaScript-only until statically representable") {
  const std::string source =
      "def repeat(value):\n"
      "    return repeat(value)\n";
  const auto javascript = transpile(source, mpf::SourceLanguage::python);
  const auto cpp = transpile(source, mpf::SourceLanguage::python, mpf::TargetLanguage::cpp);
  REQUIRE(javascript.success());
  REQUIRE(!cpp.success());
  REQUIRE(cpp.diagnostics.front().code == "MPF2035");
}

TEST_CASE("Matlab function output becomes a JavaScript return value") {
  const auto result = transpile(
      "function y = square(x)\n"
      "  y = x ^ 2;\n"
      "end\n",
      mpf::SourceLanguage::matlab);
  REQUIRE(result.success());
  REQUIRE(result.code.find("export function square(x)") != std::string::npos);
  REQUIRE(result.code.find("y = x ** 2;") != std::string::npos);
  REQUIRE(result.code.find("return y;") != std::string::npos);
}

TEST_CASE("Matlab tokenized parser handles multi-output signatures and expression boundaries") {
  const std::string source =
      "function [sum_value product_value] = pair(left, right)\n"
      "  sum_value = left + right;\n"
      "  product_value = left * right;\n"
      "end\n"
      "text = 'a=b:c';\n"
      "if text == 'a=b:c'\n"
      "  disp(42)\n"
      "end\n";
  for (const auto target : {mpf::TargetLanguage::javascript, mpf::TargetLanguage::cpp}) {
    const auto result = transpile(source, mpf::SourceLanguage::matlab, target);
    REQUIRE(result.success());
    REQUIRE(result.code.find("pair") != std::string::npos);
    REQUIRE(result.code.find("a=b:c") != std::string::npos);
  }
}

TEST_CASE("Matlab multi-output calls lower independently in both backends") {
  const std::string source =
      "[first, second] = pair(6, 7);\n"
      "single = pair(20, 22);\n"
      "disp(first + second + single)\n"
      "function [sum_value, product_value] = pair(left, right)\n"
      "  sum_value = left + right;\n"
      "  product_value = left * right;\n"
      "end\n";
  const auto javascript = transpile(source, mpf::SourceLanguage::matlab);
  const auto cpp = transpile(source, mpf::SourceLanguage::matlab, mpf::TargetLanguage::cpp);
  REQUIRE(javascript.success());
  REQUIRE(cpp.success());
  REQUIRE(javascript.code.find("[first, second] = pair(6, 7);") != std::string::npos);
  REQUIRE(javascript.code.find("single = (pair(20, 22))[0];") != std::string::npos);
  REQUIRE(cpp.code.find("const auto mpf_internal_outputs_") != std::string::npos);
  REQUIRE(cpp.code.find("first = std::get<0>(mpf_internal_outputs_") != std::string::npos);
  REQUIRE(cpp.code.find("single = std::get<0>(pair(20, 22));") != std::string::npos);
}

TEST_CASE("Matlab multi-output metadata propagates through forward local functions") {
  const std::string source =
      "[value, doubled] = outer(20);\n"
      "disp(value + doubled)\n"
      "function [value, doubled] = outer(input)\n"
      "  [value, doubled] = inner(input);\n"
      "end\n"
      "function [value, doubled] = inner(input)\n"
      "  value = input + 1;\n"
      "  doubled = input * 2;\n"
      "end\n";
  const auto javascript = transpile(source, mpf::SourceLanguage::matlab);
  const auto cpp = transpile(source, mpf::SourceLanguage::matlab, mpf::TargetLanguage::cpp);
  REQUIRE(javascript.success());
  REQUIRE(cpp.success());
  REQUIRE(cpp.code.find("inner(T0 input)") < cpp.code.find("outer(T0 input)"));
  REQUIRE(cpp.code.find("decltype(std::get<0>(inner(input)))") != std::string::npos);
  REQUIRE(cpp.code.find("const auto mpf_internal_outputs_") != std::string::npos);
}

TEST_CASE("Matlab multi-output assignment validates its source semantics") {
  const std::string function =
      "function [left, right] = pair(value)\n"
      "  left = value;\n"
      "  right = value + 1;\n"
      "end\n";
  const auto non_call =
      transpile("[left, right] = [1, 2];\n" + function, mpf::SourceLanguage::matlab);
  const auto too_many =
      transpile("[left, right, extra] = pair(1);\n" + function, mpf::SourceLanguage::matlab);
  const auto duplicate =
      transpile("[left, left] = pair(1);\n" + function, mpf::SourceLanguage::matlab);
  const auto wrong_inputs =
      transpile("[left, right] = pair(1, 2);\n" + function, mpf::SourceLanguage::matlab);
  REQUIRE(!non_call.success());
  REQUIRE(!too_many.success());
  REQUIRE(!duplicate.success());
  REQUIRE(!wrong_inputs.success());
  REQUIRE(non_call.diagnostics.front().code == "MPF2034");
  REQUIRE(too_many.diagnostics.front().code == "MPF2034");
  REQUIRE(duplicate.diagnostics.front().code == "MPF2034");
  REQUIRE(wrong_inputs.diagnostics.front().code == "MPF2034");
}

TEST_CASE("Matlab output variables propagate function call types before C++ declarations") {
  const auto result = transpile(
      "function result = answer()\n"
      "  result = 42;\n"
      "end\n"
      "value = answer();\n"
      "disp(value)\n",
      mpf::SourceLanguage::matlab, mpf::TargetLanguage::cpp);
  REQUIRE(result.success());
  REQUIRE(result.code.find("std::int64_t value{};") != std::string::npos);
  REQUIRE(result.code.find("decltype(answer())") == std::string::npos);
}

TEST_CASE("Matlab tokenized parser rejects malformed statements with recovery") {
  const auto chained = transpile("first = second = 1\n", mpf::SourceLanguage::matlab);
  const auto command = transpile("disp 42\n", mpf::SourceLanguage::matlab);
  const auto parameters =
      transpile("function value = bad(left right)\nvalue = 1\nend\n", mpf::SourceLanguage::matlab);
  const auto orphan = transpile("else\ndisp(1)\nend\ndisp(2)\n", mpf::SourceLanguage::matlab);
  REQUIRE(!chained.success());
  REQUIRE(!command.success());
  REQUIRE(!parameters.success());
  REQUIRE(!orphan.success());
  REQUIRE(chained.diagnostics.front().code == "MPF1200");
  REQUIRE(command.diagnostics.front().code == "MPF1200");
  REQUIRE(parameters.diagnostics.front().code == "MPF1200");
  REQUIRE(orphan.diagnostics.front().code == "MPF1200");
  REQUIRE(orphan.diagnostics.front().location.line == 1);
}

TEST_CASE("Fortran scalar program and intrinsic map to JavaScript") {
  const auto result = transpile(
      "program demo\n"
      "  implicit none\n"
      "  integer :: n = 4\n"
      "  if (n .ge. 2) then\n"
      "    print *, sqrt(16.0)\n"
      "  end if\n"
      "end program demo\n",
      mpf::SourceLanguage::fortran);
  REQUIRE(result.success());
  REQUIRE(result.code.find("let n;") != std::string::npos);
  REQUIRE(result.code.find("if (n >= 2)") != std::string::npos);
  REQUIRE(result.code.find("console.log(Math.sqrt(16.0));") != std::string::npos);
}

TEST_CASE("Fortran tokenized parser handles selectors contextual names and compact terminators") {
  const std::string source =
      "PROGRAM TOKENS\n"
      "IMPLICIT NONE\n"
      "INTEGER(KIND=8) :: total = 0, index\n"
      "INTEGER :: block(2) = [20, 22]\n"
      "DO index = 1, 2\n"
      "  IF (index .EQ. 1) THEN\n"
      "    total = total + block(index)\n"
      "  ELSE IF (index .EQ. 2) THEN\n"
      "    total = total + block(index)\n"
      "  ELSE\n"
      "    total = -1\n"
      "  ENDIF\n"
      "ENDDO\n"
      "WRITE(*,*) total\n"
      "END PROGRAM TOKENS\n";
  for (const auto target : {mpf::TargetLanguage::javascript, mpf::TargetLanguage::cpp}) {
    const auto result = transpile(source, mpf::SourceLanguage::fortran, target);
    REQUIRE(result.success());
    REQUIRE(result.code.find("total") != std::string::npos);
    REQUIRE(result.code.find("block") != std::string::npos);
  }
}

TEST_CASE("Fortran functions subroutines recursion and CALL lower through both backends") {
  const std::string source =
      "program procedures\n"
      "implicit none\n"
      "print *, factorial(5), add_one(41), absolute(-42), combine(40, 2)\n"
      "call announce(42)\n"
      "call countdown(2)\n"
      "contains\n"
      "recursive integer function factorial(n) result(value)\n"
      "integer, intent(in) :: n\n"
      "if (n <= 1) then\n"
      "value = 1\n"
      "else\n"
      "value = n * factorial(n - 1)\n"
      "end if\n"
      "end function factorial\n"
      "integer function add_one(n) result(value)\n"
      "integer, intent(in) :: n\n"
      "value = n + 1\n"
      "end function add_one\n"
      "integer function absolute(input) result(value)\n"
      "integer, intent(in) :: input\n"
      "if (input < 0) then\n"
      "value = -input\n"
      "return\n"
      "end if\n"
      "value = input\n"
      "end function absolute\n"
      "function combine(left, right) result(value)\n"
      "integer, intent(in) :: left, right\n"
      "integer :: value\n"
      "value = left + right\n"
      "end function combine\n"
      "subroutine announce(value)\n"
      "integer, intent(in) :: value\n"
      "print *, value\n"
      "return\n"
      "end subroutine announce\n"
      "recursive subroutine countdown(value)\n"
      "integer, intent(in) :: value\n"
      "if (value > 0) then\n"
      "print *, value\n"
      "call countdown(value - 1)\n"
      "end if\n"
      "end subroutine countdown\n"
      "end program procedures\n";
  const auto javascript = transpile(source, mpf::SourceLanguage::fortran);
  const auto cpp = transpile(source, mpf::SourceLanguage::fortran, mpf::TargetLanguage::cpp);
  REQUIRE(javascript.success());
  REQUIRE(cpp.success());
  REQUIRE(javascript.code.find("export function factorial(n)") != std::string::npos);
  REQUIRE(javascript.code.find("export function announce(value)") != std::string::npos);
  REQUIRE(cpp.code.find("std::int64_t factorial(const T0& n);") != std::string::npos);
  REQUIRE(cpp.code.find("void countdown(const T0& value);") != std::string::npos);
  REQUIRE(cpp.code.find("auto announce(const T0& value)") != std::string::npos);
  REQUIRE(cpp.code.find("announce(42);") != std::string::npos);
}

TEST_CASE("Fortran procedure misuse fails closed before target emission") {
  const auto mutation = transpile(
      "program bad\n"
      "integer :: value = 1\n"
      "call update(value)\n"
      "contains\n"
      "subroutine update(argument)\n"
      "integer, intent(in) :: argument\n"
      "argument = 2\n"
      "end subroutine update\n"
      "end program bad\n",
      mpf::SourceLanguage::fortran);
  const auto mismatch = transpile(
      "program bad\n"
      "print *, answer()\n"
      "contains\n"
      "integer function answer() result(value)\n"
      "value = 42\n"
      "end function other\n"
      "end program bad\n",
      mpf::SourceLanguage::fortran);
  const auto arity = transpile(
      "program bad\n"
      "print *, answer(1)\n"
      "contains\n"
      "integer function answer() result(value)\n"
      "value = 42\n"
      "end function answer\n"
      "end program bad\n",
      mpf::SourceLanguage::fortran);
  const auto call_function = transpile(
      "program bad\n"
      "call answer()\n"
      "contains\n"
      "integer function answer() result(value)\n"
      "value = 42\n"
      "end function answer\n"
      "end program bad\n",
      mpf::SourceLanguage::fortran);
  const auto use_subroutine = transpile(
      "program bad\n"
      "integer :: value\n"
      "value = ping()\n"
      "contains\n"
      "subroutine ping()\n"
      "return\n"
      "end subroutine ping\n"
      "end program bad\n",
      mpf::SourceLanguage::fortran);
  REQUIRE(!mutation.success());
  REQUIRE(!mismatch.success());
  REQUIRE(!arity.success());
  REQUIRE(!call_function.success());
  REQUIRE(!use_subroutine.success());
  REQUIRE(mutation.diagnostics.front().code == "MPF2036");
  REQUIRE(mismatch.diagnostics.front().code == "MPF1200");
  REQUIRE(arity.diagnostics.front().code == "MPF2034");
  REQUIRE(call_function.diagnostics.front().code == "MPF2037");
  REQUIRE(use_subroutine.diagnostics.front().code == "MPF2037");
}

TEST_CASE("Fortran scalar OUT and INOUT arguments lower to references and writebacks") {
  const std::string source =
      "program references\n"
      "integer :: value = 40, produced, written, answer\n"
      "call outer_increment(value)\n"
      "call produce(produced)\n"
      "answer = compute(40, written)\n"
      "print *, value, produced, answer, written\n"
      "contains\n"
      "subroutine outer_increment(argument)\n"
      "integer, intent(inout) :: argument\n"
      "call increment(argument)\n"
      "end subroutine outer_increment\n"
      "subroutine increment(argument)\n"
      "integer, intent(inout) :: argument\n"
      "argument = argument + 2\n"
      "end subroutine increment\n"
      "subroutine produce(argument)\n"
      "integer, intent(out) :: argument\n"
      "argument = 42\n"
      "end subroutine produce\n"
      "integer function compute(input, output) result(result_value)\n"
      "integer, intent(in) :: input\n"
      "integer, intent(out) :: output\n"
      "output = input + 1\n"
      "result_value = input + 2\n"
      "end function compute\n"
      "end program references\n";
  const auto javascript = transpile(source, mpf::SourceLanguage::fortran);
  const auto cpp = transpile(source, mpf::SourceLanguage::fortran, mpf::TargetLanguage::cpp);
  REQUIRE(javascript.success());
  REQUIRE(cpp.success());
  REQUIRE(javascript.code.find("argument.value = argument.value + 2") != std::string::npos);
  REQUIRE(javascript.code.find("{ value: undefined }") != std::string::npos);
  REQUIRE(javascript.code.find("mpf_internal_call_result_") != std::string::npos);
  REQUIRE(cpp.code.find("auto increment(T0& argument)") != std::string::npos);
  REQUIRE(cpp.code.find("compute(const T0& input, T1& output)") != std::string::npos);
}

TEST_CASE("Fortran reference actuals enforce definability and precise region alias rules") {
  const auto literal = transpile(
      "program bad\n"
      "call produce(1)\n"
      "contains\n"
      "subroutine produce(value)\n"
      "integer, intent(out) :: value\n"
      "value = 1\n"
      "end subroutine produce\n"
      "end program bad\n",
      mpf::SourceLanguage::fortran);
  const auto alias = transpile(
      "program bad\n"
      "integer :: value = 1\n"
      "call update(value, value)\n"
      "contains\n"
      "subroutine update(left, right)\n"
      "integer, intent(inout) :: left, right\n"
      "left = left + 1\n"
      "right = right + 1\n"
      "end subroutine update\n"
      "end program bad\n",
      mpf::SourceLanguage::fortran);
  const auto disjoint_sections = transpile(
      "program bad\n"
      "integer :: values(4) = [1,2,3,4]\n"
      "call update(values(1:2), values(3:4))\n"
      "contains\n"
      "subroutine update(left, right)\n"
      "integer, intent(inout) :: left(:), right(:)\n"
      "left(1) = 8\n"
      "right(1) = 9\n"
      "end subroutine update\n"
      "end program bad\n",
      mpf::SourceLanguage::fortran);
  const auto overlapping_sections = transpile(
      "program bad\n"
      "integer :: values(6) = [1,2,3,4,5,6]\n"
      "call update(values(1:4:2), values(3:6:2))\n"
      "contains\n"
      "subroutine update(left, right)\n"
      "integer, intent(inout) :: left(:), right(:)\n"
      "left(1) = 8\n"
      "right(1) = 9\n"
      "end subroutine update\n"
      "end program bad\n",
      mpf::SourceLanguage::fortran);
  const auto dynamic_sections = transpile(
      "program bad\n"
      "integer :: values(6) = [1,2,3,4,5,6]\n"
      "integer :: split = 3\n"
      "call update(values(1:split), values(split+1:6))\n"
      "contains\n"
      "subroutine update(left, right)\n"
      "integer, intent(inout) :: left(:), right(:)\n"
      "left(1) = 8\n"
      "right(1) = 9\n"
      "end subroutine update\n"
      "end program bad\n",
      mpf::SourceLanguage::fortran);
  const auto missing_output = transpile(
      "program bad\n"
      "integer :: value\n"
      "call produce(value)\n"
      "contains\n"
      "subroutine produce(value)\n"
      "integer, intent(out) :: value\n"
      "return\n"
      "end subroutine produce\n"
      "end program bad\n",
      mpf::SourceLanguage::fortran);
  REQUIRE(!literal.success());
  REQUIRE(!alias.success());
  REQUIRE(disjoint_sections.success());
  REQUIRE(!overlapping_sections.success());
  REQUIRE(!dynamic_sections.success());
  REQUIRE(!missing_output.success());
  REQUIRE(literal.diagnostics.front().code == "MPF2038");
  REQUIRE(alias.diagnostics.front().code == "MPF2038");
  REQUIRE(overlapping_sections.diagnostics.front().code == "MPF2038");
  REQUIRE(dynamic_sections.diagnostics.front().code == "MPF2038");
  REQUIRE(missing_output.diagnostics.front().code == "MPF2036");
}

TEST_CASE("Fortran disjoint strided and rank-two writable regions lower to both targets") {
  const std::string strided =
      "program regions\n"
      "integer :: values(6) = [1,2,3,4,5,6]\n"
      "call update(values(1:6:2), values(2:6:2))\n"
      "print *, values(1), values(2)\n"
      "contains\n"
      "subroutine update(left, right)\n"
      "integer, intent(inout) :: left(:), right(:)\n"
      "left(1) = 40\n"
      "right(1) = 2\n"
      "end subroutine update\n"
      "end program regions\n";
  const std::string rank_two =
      "program regions\n"
      "integer :: values(2,4) = reshape([1,2,3,4,5,6,7,8], [2,4])\n"
      "call update(values(:,1:2), values(:,3:4))\n"
      "print *, values(1,1), values(1,3)\n"
      "contains\n"
      "subroutine update(left, right)\n"
      "integer, intent(inout) :: left(:,:), right(:,:)\n"
      "left(1,1) = 40\n"
      "right(1,1) = 2\n"
      "end subroutine update\n"
      "end program regions\n";
  const std::string descending =
      "program regions\n"
      "integer :: values(6) = [1,2,3,4,5,6]\n"
      "call update(values(6:1:-2), values(5:1:-2))\n"
      "contains\n"
      "subroutine update(left, right)\n"
      "integer, intent(inout) :: left(:), right(:)\n"
      "left(1) = 40\n"
      "right(1) = 2\n"
      "end subroutine update\n"
      "end program regions\n";
  for (const auto target : {mpf::TargetLanguage::javascript, mpf::TargetLanguage::cpp}) {
    const auto strided_result = transpile(strided, mpf::SourceLanguage::fortran, target);
    const auto rank_two_result = transpile(rank_two, mpf::SourceLanguage::fortran, target);
    const auto descending_result = transpile(descending, mpf::SourceLanguage::fortran, target);
    REQUIRE(strided_result.success());
    REQUIRE(rank_two_result.success());
    REQUIRE(descending_result.success());
    const auto strided_writeback = target == mpf::TargetLanguage::javascript
                                       ? "__mpf_set_section(values"
                                       : "mpf_runtime::assign_slice(values";
    const auto block_writeback = target == mpf::TargetLanguage::javascript
                                     ? "__mpf_set_section(values"
                                     : "mpf_runtime::assign_block(values";
    REQUIRE(strided_result.code.find(strided_writeback) != std::string::npos);
    REQUIRE(rank_two_result.code.find(block_writeback) != std::string::npos);
    REQUIRE(descending_result.code.find(strided_writeback) != std::string::npos);
  }
}

TEST_CASE("Fortran procedure keywords remain contextual entity names") {
  const std::string source =
      "program contextual\n"
      "integer :: function = 40, result = 2\n"
      "print *, function + result\n"
      "call ping\n"
      "contains\n"
      "subroutine ping()\n"
      "return\n"
      "end subroutine ping\n"
      "end program contextual\n";
  for (const auto target : {mpf::TargetLanguage::javascript, mpf::TargetLanguage::cpp}) {
    const auto result = transpile(source, mpf::SourceLanguage::fortran, target);
    REQUIRE(result.success());
    REQUIRE(
        result.code.find(target == mpf::TargetLanguage::javascript ? "mpf_function" : "function") !=
        std::string::npos);
    REQUIRE(result.code.find("ping()") != std::string::npos);
  }
}

TEST_CASE("Fortran keyword and optional argument association lowers to both backends") {
  const std::string source =
      "program association\n"
      "integer :: first, second\n"
      "first = compute(right=2, left=40)\n"
      "second = defaulted(40)\n"
      "contains\n"
      "integer function compute(left, right) result(value)\n"
      "integer, intent(in) :: left, right\n"
      "value = left + right\n"
      "end function compute\n"
      "integer function defaulted(required, extra) result(value)\n"
      "integer, intent(in) :: required\n"
      "integer, intent(in), optional :: extra\n"
      "if (present(extra)) then\n"
      "value = required + extra\n"
      "else\n"
      "value = required + 1\n"
      "end if\n"
      "end function defaulted\n"
      "end program association\n";
  mpf::TranspileOptions javascript_options;
  javascript_options.language = mpf::SourceLanguage::fortran;
  javascript_options.target = mpf::TargetLanguage::javascript;
  javascript_options.emit_source_banner = false;
  const auto javascript = mpf::Transpiler{}.transpile(source, javascript_options);
  auto cpp_options = javascript_options;
  cpp_options.target = mpf::TargetLanguage::cpp;
  const auto cpp = mpf::Transpiler{}.transpile(source, cpp_options);
  REQUIRE(javascript.success());
  REQUIRE(cpp.success());
  REQUIRE(javascript.code.find("compute(40, 2)") != std::string::npos);
  REQUIRE(javascript.code.find("defaulted(40, undefined)") != std::string::npos);
  REQUIRE(javascript.code.find("extra !== undefined") != std::string::npos);
  REQUIRE(cpp.code.find("mpf_runtime::optional_argument<std::int64_t> extra") != std::string::npos);
  REQUIRE(cpp.code.find("defaulted(40, std::nullopt)") != std::string::npos);
  REQUIRE(cpp.code.find("extra.has_value()") != std::string::npos);
}

TEST_CASE("Fortran argument association rejects invalid keyword and OPTIONAL contracts") {
  const auto unknown = transpile(
      "program bad\ninteger :: value\nvalue = add(left=1, missing=2)\ncontains\n"
      "integer function add(left, right) result(total)\n"
      "integer, intent(in) :: left, right\ntotal = left + right\n"
      "end function add\nend program bad\n",
      mpf::SourceLanguage::fortran);
  const auto duplicate = transpile(
      "program bad\ninteger :: value\nvalue = add(1, left=2)\ncontains\n"
      "integer function add(left, right) result(total)\n"
      "integer, intent(in) :: left, right\ntotal = left + right\n"
      "end function add\nend program bad\n",
      mpf::SourceLanguage::fortran);
  const auto missing = transpile(
      "program bad\ninteger :: value\nvalue = add(right=2)\ncontains\n"
      "integer function add(left, right) result(total)\n"
      "integer, intent(in) :: left, right\ntotal = left + right\n"
      "end function add\nend program bad\n",
      mpf::SourceLanguage::fortran);
  const auto local_optional = transpile(
      "program bad\ninteger, optional :: value\nend program bad\n", mpf::SourceLanguage::fortran);
  const auto invalid_present = transpile(
      "program bad\ncontains\nsubroutine consume(value)\n"
      "integer, intent(in) :: value\n"
      "if (present(value)) then\nreturn\nend if\n"
      "end subroutine consume\nend program bad\n",
      mpf::SourceLanguage::fortran);
  REQUIRE(!unknown.success());
  REQUIRE(!duplicate.success());
  REQUIRE(!missing.success());
  REQUIRE(!local_optional.success());
  REQUIRE(!invalid_present.success());
  REQUIRE(unknown.diagnostics.front().code == "MPF2040");
  REQUIRE(duplicate.diagnostics.front().code == "MPF2040");
  REQUIRE(missing.diagnostics.front().code == "MPF2034");
  REQUIRE(local_optional.diagnostics.front().code == "MPF2040");
  REQUIRE(invalid_present.diagnostics.front().code == "MPF2040");
}

TEST_CASE("Fortran writable scalar and array OPTIONAL arguments preserve writeback") {
  const std::string source =
      "program optional_writeback\n"
      "integer :: scalar = 40\n"
      "integer :: values(3) = [1,2,3]\n"
      "call update(scalar)\n"
      "call update()\n"
      "call scale(values)\n"
      "call scale()\n"
      "contains\n"
      "subroutine update(value)\n"
      "integer, intent(inout), optional :: value\n"
      "if (present(value)) then\nvalue = value + 2\nend if\n"
      "end subroutine update\n"
      "subroutine scale(items)\n"
      "integer, intent(inout), optional :: items(:)\n"
      "if (present(items)) then\nitems(1) = items(1) * 2\nend if\n"
      "end subroutine scale\n"
      "end program optional_writeback\n";
  mpf::TranspileOptions options;
  options.language = mpf::SourceLanguage::fortran;
  options.target = mpf::TargetLanguage::javascript;
  options.emit_source_banner = false;
  const auto javascript = mpf::Transpiler{}.transpile(source, options);
  options.target = mpf::TargetLanguage::cpp;
  const auto cpp = mpf::Transpiler{}.transpile(source, options);
  REQUIRE(javascript.success());
  REQUIRE(cpp.success());
  REQUIRE(javascript.code.find("update(undefined)") != std::string::npos);
  REQUIRE(javascript.code.find("value !== undefined") != std::string::npos);
  REQUIRE(javascript.code.find("{ value: scalar }") != std::string::npos);
  REQUIRE(cpp.code.find("mpf_runtime::optional_argument<std::int64_t> value") != std::string::npos);
  REQUIRE(cpp.code.find("mpf_runtime::optional_argument<std::vector<std::int64_t>> items") !=
          std::string::npos);
  REQUIRE(cpp.code.find("update(std::nullopt)") != std::string::npos);
}

TEST_CASE("Fortran tokenized parser rejects unsupported declarations and orphan terminators") {
  const auto attribute =
      transpile("program bad\ninteger, dimension(2) :: values\nend program bad\n",
                mpf::SourceLanguage::fortran);
  const auto counted =
      transpile("program bad\ninteger :: index\ndo index = 1\nend do\nend program bad\n",
                mpf::SourceLanguage::fortran);
  const auto orphan = transpile("program bad\nelse\nprint *, 1\nend if\nend program bad\n",
                                mpf::SourceLanguage::fortran);
  REQUIRE(!attribute.success());
  REQUIRE(!counted.success());
  REQUIRE(!orphan.success());
  REQUIRE(attribute.diagnostics.front().code == "MPF1200");
  REQUIRE(counted.diagnostics.front().code == "MPF1200");
  REQUIRE(orphan.diagnostics.front().code == "MPF1200");
}

TEST_CASE("Fortran free and fixed source forms normalize before both backends") {
  const std::string free_source =
      "program forms\n"
      "integer :: value = 40 + &\n"
      "  &2; print *, value\n"
      "end program forms\n";
  const std::string fixed_source =
      "C fixed form\n"
      "      PROGRAM FORMS\n"
      "      INTEGER VALUE\n"
      "      VALUE = 40 +\n"
      "     &        2\n"
      "      PRINT *, VALUE\n"
      "      END\n";
  for (const auto target : {mpf::TargetLanguage::javascript, mpf::TargetLanguage::cpp}) {
    mpf::TranspileOptions free_options;
    free_options.language = mpf::SourceLanguage::fortran;
    free_options.target = target;
    free_options.filename = "forms.f90";
    free_options.emit_source_banner = false;
    const auto free_result = mpf::Transpiler{}.transpile(free_source, free_options);

    auto fixed_options = free_options;
    fixed_options.filename = "forms.f";
    const auto fixed_result = mpf::Transpiler{}.transpile(fixed_source, fixed_options);
    REQUIRE(free_result.success());
    REQUIRE(fixed_result.success());
    REQUIRE(free_result.code.find("42") != std::string::npos);
    REQUIRE(fixed_result.code.find("42") != std::string::npos);
    REQUIRE(free_result.code.find("40 + 2") == std::string::npos);
    REQUIRE(fixed_result.code.find("40 + 2") == std::string::npos);
  }
}

TEST_CASE("Python and Matlab logical source normalization is backend independent") {
  const std::string python_source =
      "values = [\n"
      "    1,\n"
      "    2,\n"
      "    3,\n"
      "]\n"
      "total = (sum(values) +\n"
      "         4)\n"
      "continued = 10 + \\\n"
      "    20\n"
      "left = 1; right = 2\n"
      "if (total == 10 and\n"
      "        continued == 30):\n"
      "    print(total + continued + left + right)\n";
  const std::string matlab_source =
      "values = [1 2 ...\n"
      "          3 4]; total = sum(values); extra = 2\n"
      "continued = 10 + ...\n"
      "            20\n"
      "if total == 10, disp(total + continued + extra), end\n";

  for (const auto target : {mpf::TargetLanguage::javascript, mpf::TargetLanguage::cpp}) {
    const auto python = transpile(python_source, mpf::SourceLanguage::python, target);
    const auto matlab = transpile(matlab_source, mpf::SourceLanguage::matlab, target);
    REQUIRE(python.success());
    REQUIRE(matlab.success());
    REQUIRE(python.code.find("total + continued + left + right") != std::string::npos);
    REQUIRE(matlab.code.find("total + continued + extra") != std::string::npos);
  }
}

TEST_CASE("logical source diagnostics retain public source identity") {
  mpf::TranspileOptions options;
  options.emit_source_banner = false;
  options.language = mpf::SourceLanguage::python;
  options.filename = "broken.py";
  const auto python = mpf::Transpiler{}.transpile("value = (1 +\n  2\n", options);
  REQUIRE(!python.success());
  REQUIRE(python.diagnostics.front().code == "MPF1402");
  REQUIRE(python.diagnostics.front().source_name == "broken.py");
  REQUIRE(python.diagnostics.front().location.line == 1);

  options.language = mpf::SourceLanguage::matlab;
  options.filename = "broken.m";
  const auto matlab = mpf::Transpiler{}.transpile("%{\nnever closed\n", options);
  REQUIRE(!matlab.success());
  REQUIRE(matlab.diagnostics.front().code == "MPF1504");
  REQUIRE(matlab.diagnostics.front().source_name == "broken.m");
  REQUIRE(matlab.diagnostics.front().location.line == 2);
}

TEST_CASE("explicit Fortran source form and continuation diagnostics are stable") {
  mpf::TranspileOptions options;
  options.language = mpf::SourceLanguage::fortran;
  options.fortran_source_form = mpf::FortranSourceForm::free;
  options.emit_source_banner = false;
  const auto unfinished = mpf::Transpiler{}.transpile("value = 1 + &\n", options);
  REQUIRE(!unfinished.success());
  REQUIRE(unfinished.diagnostics.front().code == "MPF1302");
  REQUIRE(unfinished.diagnostics.front().location.line == 1);
  REQUIRE(unfinished.diagnostics.front().source_name == "<memory>");

  options.fortran_source_form = mpf::FortranSourceForm::fixed;
  options.language = mpf::SourceLanguage::automatic;
  const auto fixed = mpf::Transpiler{}.transpile(
      "      INTEGER VALUE\n"
      "      VALUE = 42\n"
      "      PRINT *, VALUE\n"
      "      END\n",
      options);
  REQUIRE(fixed.success());
}

TEST_CASE("script module kind emits strict mode and no exports") {
  mpf::TranspileOptions options;
  options.language = mpf::SourceLanguage::python;
  options.module_kind = mpf::ModuleKind::script;
  options.emit_source_banner = false;
  const auto result = mpf::Transpiler{}.transpile("def answer():\n    return 42\n", options);
  REQUIRE(result.success());
  REQUIRE(result.code.find("\"use strict\";") == 0);
  REQUIRE(result.code.find("export function") == std::string::npos);
}

TEST_CASE("TypeScript functions defaults control flow and strict equality lower to both targets") {
  const std::string source =
      "export function accumulate(limit: number, step: number = 1): number {\n"
      "  let total: number = 0;\n"
      "  let cursor: number = 0;\n"
      "  while (cursor < limit) {\n"
      "    total = total + step;\n"
      "    cursor = cursor + 1;\n"
      "  }\n"
      "  if (total === 42) { total = total + 0; } else { total = 0; }\n"
      "  return total;\n"
      "}\n"
      "const answer: number = accumulate(42);\n"
      "const ratio: number = 7 / 2;\n"
      "console.log(answer, ratio);\n";
  const auto javascript = transpile(source, mpf::SourceLanguage::typescript);
  const auto cpp = transpile(source, mpf::SourceLanguage::typescript, mpf::TargetLanguage::cpp);
  REQUIRE(javascript.success());
  REQUIRE(cpp.success());
  REQUIRE(javascript.code.find("export function accumulate(limit, step = 1)") != std::string::npos);
  REQUIRE(javascript.code.find("total === 42") != std::string::npos);
  REQUIRE(javascript.code.find("accumulate(42, 1)") != std::string::npos);
  REQUIRE(cpp.code.find("mpf_runtime::ieee_divide(7, 2)") != std::string::npos);
  REQUIRE(cpp.code.find("mpf_runtime::optional_argument<double> step") != std::string::npos);

  const auto private_function =
      transpile("function hidden(): number { return 42; }\nconsole.log(hidden());\n",
                mpf::SourceLanguage::typescript);
  REQUIRE(private_function.success());
  REQUIRE(private_function.code.find("function hidden()") != std::string::npos);
  REQUIRE(private_function.code.find("export function hidden()") == std::string::npos);
}

TEST_CASE("TypeScript lexical blocks preserve shadowing and assignments to outer bindings") {
  const std::string source =
      "let value: number = 1;\n"
      "if (true) {\n"
      "  let value: string = \"inner\";\n"
      "  console.log(value);\n"
      "}\n"
      "if (true) { value = 42; }\n"
      "console.log(value);\n";
  const auto javascript = transpile(source, mpf::SourceLanguage::typescript);
  const auto cpp = transpile(source, mpf::SourceLanguage::typescript, mpf::TargetLanguage::cpp);
  REQUIRE(javascript.success());
  REQUIRE(cpp.success());
  REQUIRE(javascript.code.find("if (true) {\n  let value;") != std::string::npos);
  REQUIRE(javascript.code.find("value = \"inner\";") != std::string::npos);
  REQUIRE(cpp.code.find("if (true) {\n      std::string value_1{};") != std::string::npos);
  REQUIRE(cpp.code.find("value_1 = std::string{\"inner\"};") != std::string::npos);
  REQUIRE(cpp.code.find("value = 42;") != std::string::npos);
}

TEST_CASE("TypeScript canonical for loops preserve update continue break and loop scope") {
  const std::string source =
      "let total: number = 0;\n"
      "for (let index: number = 0; index < 10; index++) {\n"
      "  if (index === 2) { continue; }\n"
      "  if (index === 6) { break; }\n"
      "  total = total + index;\n"
      "}\n"
      "console.log(total);\n";
  const auto javascript = transpile(source, mpf::SourceLanguage::typescript);
  const auto cpp = transpile(source, mpf::SourceLanguage::typescript, mpf::TargetLanguage::cpp);
  REQUIRE(javascript.success());
  REQUIRE(cpp.success());
  REQUIRE(javascript.code.find("for (index = 0; index < 10; index = index + 1)") !=
          std::string::npos);
  REQUIRE(javascript.code.find("__mpf_py_equal") == std::string::npos);
  REQUIRE(cpp.code.find("for (index = 0;") != std::string::npos);
  REQUIRE(cpp.code.find("index = index + 1)") != std::string::npos);

  const auto leaked =
      transpile("for (let index: number = 0; index < 1; index++) {}\nconsole.log(index);\n",
                mpf::SourceLanguage::typescript);
  REQUIRE(!leaked.success());
  REQUIRE(std::any_of(
      leaked.diagnostics.begin(), leaked.diagnostics.end(), [](const mpf::Diagnostic& diagnostic) {
        return diagnostic.message.find("undefined identifier 'index'") != std::string::npos;
      }));
}

TEST_CASE("TypeScript frontend rejects semantics that cannot yet be preserved") {
  const struct Case {
    const char* source;
    const char* needle;
  } cases[]{
      {"var value = 1;\n", "unsupported TypeScript statement keyword"},
      {"const value = 1;\nvalue = 2;\n", "const binding"},
      {"value = 1;\n", "undeclared TypeScript name"},
      {"const same = 1 == 1;\n", "loose equality"},
      {"const choose = (value: number) => value;\n", "arrow"},
      {"if (true) { let local = 1; }\nconsole.log(local);\n", "undefined identifier"},
      {"for (const index: number = 0; index < 2; index++) {}\n", "must use let"},
      {"for (let index: number = 0; 42; index++) {}\n", "requires a boolean"},
      {"for (let index: number = 0; index < 2; other++) {}\n", "must assign the induction binding"},
      {"let index: number = 9;\n"
       "for (let index: number = index; index < 2; index++) {}\n",
       "before it is definitely assigned"},
      {"if (true) { function nested(): number { return 1; } }\n", "nested TypeScript function"},
      {"const values: number[] = [1];\nconsole.log(values[0.5]);\n", "index must be an integer"}};
  for (const auto& test : cases) {
    const auto result = transpile(test.source, mpf::SourceLanguage::typescript);
    REQUIRE(!result.success());
    REQUIRE(std::any_of(result.diagnostics.begin(), result.diagnostics.end(),
                        [&](const mpf::Diagnostic& diagnostic) {
                          return diagnostic.message.find(test.needle) != std::string::npos;
                        }));
  }
}
