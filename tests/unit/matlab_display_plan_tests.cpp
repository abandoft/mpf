#include "backends/common/lir_builder.hpp"
#include "backends/javascript/lir_planning.hpp"
#include "backends/javascript/lir_representation.hpp"
#include "frontends/common/registry.hpp"
#include "semantic/analyzer.hpp"
#include "test_framework.hpp"

TEST_CASE("Matlab logical scalar display is planned numerically without changing Python print") {
  using namespace mpf::detail;
  for (const auto language : {mpf::SourceLanguage::matlab, mpf::SourceLanguage::python}) {
    const auto& frontend =
        language == mpf::SourceLanguage::matlab ? matlab_frontend() : python_frontend();
    const auto source = language == mpf::SourceLanguage::matlab ? "disp(1 == 1)\ndisplay(1 == 2)\n"
                                                                : "print(True)\nprint(False)\n";
    auto parsed = parse_with_frontend(frontend, SourceText(source, "logical_display"));
    REQUIRE(parsed.diagnostics.empty());
    auto lowered = frontend.lower(std::move(parsed.ast));
    REQUIRE(lowered.diagnostics.empty());
    auto analysis = analyze_program(lowered.program, std::move(lowered.semantics));
    REQUIRE(analysis.diagnostics.empty());
    auto mir = mir::lower_from_hir(std::move(lowered.program), std::move(analysis.semantics),
                                   analysis.names);
    REQUIRE(mir.diagnostics.empty());
    const auto resolve = [](const HirNodeId, const IntrinsicId) { return CodeBinding{}; };
    auto lir = lower_structured_lir<javascript::lir::SemanticProgram, javascript::lir::Statement,
                                    javascript::lir::Expression, javascript::lir::CaseSelector>(
        mir.program, resolve);
    lir->source_language = language;
    javascript::plan_lir_resources(*lir, mpf::TranspileOptions{});
    javascript::plan_lir_representation(*lir);
    std::vector<mpf::Diagnostic> diagnostics;
    javascript::verify_lir_representation(*lir, diagnostics);
    REQUIRE(diagnostics.empty());
    const auto expected = language == mpf::SourceLanguage::matlab
                              ? javascript::lir::PrintValueForm::matlab_logical_scalar
                              : javascript::lir::PrintValueForm::direct;
    REQUIRE(lir->statements.size() == 2U);
    REQUIRE(lir->statements[0].plan.print_value == expected);
    REQUIRE(lir->statements[1].plan.print_value == expected);
    lir->statements[0].plan.print_value =
        expected == javascript::lir::PrintValueForm::direct
            ? javascript::lir::PrintValueForm::matlab_logical_scalar
            : javascript::lir::PrintValueForm::direct;
    javascript::verify_lir_representation(*lir, diagnostics);
    REQUIRE(!diagnostics.empty());
    mpf::TranspileOptions options;
    options.language = language;
    const auto compiled = mpf::Transpiler{}.transpile(source, options);
    REQUIRE(compiled.success());
    REQUIRE((compiled.code.find("console.log(Number(") != std::string::npos) ==
            (language == mpf::SourceLanguage::matlab));
  }
}
