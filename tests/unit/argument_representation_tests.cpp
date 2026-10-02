#include <algorithm>
#include <utility>

#include "frontends/common/registry.hpp"
#include "ir/mir.hpp"
#include "semantic/analyzer.hpp"
#include "source/source_manager.hpp"
#include "test_framework.hpp"

TEST_CASE("Matlab same-extent layout adaptation is an independently verified MIR call boundary") {
  mpf::detail::SourceManager sources;
  const auto id = sources.add(
      "disp(checked(reshape([],0,5)))\n"
      "function output = checked(value)\n"
      "arguments\n"
      "value (:,:) double\n"
      "end\n"
      "output = length(value)\n"
      "end\n",
      "argument_representation.m");
  const auto& frontend = mpf::detail::matlab_frontend();
  auto parsed = mpf::detail::parse_with_frontend(frontend, sources.source(id));
  REQUIRE(parsed.diagnostics.empty());
  auto hir = frontend.lower(std::move(parsed.ast));
  REQUIRE(hir.diagnostics.empty());
  auto analysis = mpf::detail::analyze_program(hir.program, std::move(hir.semantics));
  REQUIRE(analysis.empty());
  auto mir = mpf::detail::mir::lower_from_hir(std::move(hir.program), std::move(analysis.semantics),
                                              analysis.names);
  REQUIRE(mir.diagnostics.empty());
  REQUIRE(mpf::detail::mir::verify(mir.program, "argument-layout").empty());
  auto call =
      std::find_if(mir.program.calls.begin(), mir.program.calls.end(), [](const auto& entry) {
        return !entry.arguments.empty() && entry.arguments.front().boundary.dimensions_declared;
      });
  REQUIRE(call != mir.program.calls.end());
  auto& argument = call->arguments.front();
  REQUIRE(argument.boundary.validated_rank == 2U);
  REQUIRE(mpf::detail::has_argument_boundary_conversion(
      argument.boundary.conversion, mpf::detail::ArgumentBoundaryConversion::matlab_size));
  const auto* actual = mpf::detail::mir::shape(mir.program, argument.shape);
  const auto* validated = mpf::detail::mir::shape(mir.program, argument.validated_shape);
  REQUIRE(actual != nullptr);
  REQUIRE(validated != nullptr);
  REQUIRE(actual->layout != validated->layout);
  argument.boundary.conversion = mpf::detail::ArgumentBoundaryConversion::none;
  const auto rejected = mpf::detail::mir::verify(mir.program, "missing-layout-conversion");
  REQUIRE(std::any_of(rejected.begin(), rejected.end(), [](const auto& diagnostic) {
    return diagnostic.message.find("boundary-conversion contract") != std::string::npos;
  }));
}
