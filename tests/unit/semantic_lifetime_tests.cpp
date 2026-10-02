#include <cstddef>
#include <string>
#include <utility>

#include "frontends/common/registry.hpp"
#include "ir/semantic_table.hpp"
#include "mpf/transpiler.hpp"
#include "semantic/analyzer.hpp"
#include "source/source_manager.hpp"
#include "test_framework.hpp"

namespace {

void require_normalization_growth(const std::string& text,
                                  const mpf::detail::FrontendDescriptor& frontend) {
  mpf::detail::SourceManager sources;
  const auto id = sources.add(text, "semantic_lifetime");
  auto parsed = mpf::detail::parse_with_frontend(frontend, sources.source(id));
  REQUIRE(parsed.diagnostics.empty());
  auto hir = frontend.lower(std::move(parsed.ast));
  REQUIRE(hir.diagnostics.empty());
  hir.semantics.expressions.shrink_to_fit();
  const auto original_capacity = hir.semantics.expressions.capacity();
  auto analysis = mpf::detail::analyze_program(hir.program, std::move(hir.semantics));
  REQUIRE(analysis.empty());
  REQUIRE(analysis.semantics.expressions.size() > original_capacity);
  REQUIRE(mpf::detail::hir::verify_semantics(hir.program, analysis.semantics, "growth").empty());
}

}  // namespace

TEST_CASE("Matlab nested calls and selectors survive dense semantic table growth") {
  const std::string function =
      "function output = pick(value)\n"
      "arguments\n"
      "value {mustBeFinite} = 1\n"
      "end\n"
      "output = value\n"
      "end\n";
  for (const auto* expression :
       {"disp(pick())", "disp(values(pick()))", "disp(values(pick(),pick()))",
        "disp(values(pick():2))", "disp(values(1:pick()))", "values(pick()) = 2"}) {
    std::string source = "values = [1,2;3,4]\n";
    for (std::size_t repeat = 0; repeat < 8U; ++repeat) {
      source += expression;
      source += '\n';
    }
    require_normalization_growth(source + function, mpf::detail::matlab_frontend());
  }
}

TEST_CASE("Python tuple assembly reacquires semantic facts after default cloning") {
  std::string source =
      "def pick(value=1) -> int:\n"
      "    return value\n";
  for (std::size_t repeat = 0; repeat < 8U; ++repeat) {
    source += "values = (pick(), pick())\n";
  }
  source += "first,second = values\nprint(first)\n";
  require_normalization_growth(source, mpf::detail::python_frontend());
}

TEST_CASE("Fortran optional call normalization preserves surrounding selector facts") {
  std::string source =
      "program main\n"
      "implicit none\n"
      "integer :: values(3) = [1,2,3]\n";
  for (std::size_t repeat = 0; repeat < 8U; ++repeat) {
    source += "print *, values(pick())\n";
  }
  source +=
      "contains\n"
      "integer function pick(value) result(output)\n"
      "integer, optional, intent(in) :: value\n"
      "output = 1\n"
      "end function\n"
      "end program\n";
  require_normalization_growth(source, mpf::detail::fortran_frontend());
}
