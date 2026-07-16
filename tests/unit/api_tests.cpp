#include <string>

#include "mpf/transpiler.hpp"
#include "test_framework.hpp"

TEST_CASE("language names round-trip") {
  REQUIRE(mpf::language_from_name("python") == mpf::SourceLanguage::python);
  REQUIRE(mpf::language_from_name("MATLAB") == mpf::SourceLanguage::matlab);
  REQUIRE(mpf::language_from_name("f90") == mpf::SourceLanguage::fortran);
  REQUIRE(std::string(mpf::to_string(mpf::SourceLanguage::fortran)) == "fortran");
  REQUIRE(mpf::target_from_name("cpp") == mpf::TargetLanguage::cpp);
  REQUIRE(mpf::target_from_name("JavaScript") == mpf::TargetLanguage::javascript);
  REQUIRE(std::string(mpf::to_string(mpf::TargetLanguage::cpp)) == "cpp");
  REQUIRE(mpf::fortran_source_form_from_name("FREE") == mpf::FortranSourceForm::free);
  REQUIRE(mpf::fortran_source_form_from_name("fixed") == mpf::FortranSourceForm::fixed);
  REQUIRE(std::string(mpf::to_string(mpf::FortranSourceForm::automatic)) == "auto");
  REQUIRE(mpf::frontend_available(mpf::SourceLanguage::python));
  REQUIRE(mpf::source_language_name_known("PY"));
  REQUIRE(!mpf::source_language_name_known("unknown"));
  REQUIRE(mpf::target_language_name_known("JS"));
  REQUIRE(!mpf::target_language_name_known("unknown"));
  REQUIRE(mpf::registered_source_languages().size() == 3);
  REQUIRE(mpf::registered_target_languages().size() == 2);
  REQUIRE(mpf::backend_available(mpf::TargetLanguage::javascript));
  REQUIRE(mpf::backend_available(mpf::TargetLanguage::cpp));
}

TEST_CASE("filename extension selects the frontend") {
  mpf::TranspileOptions options;
  options.filename = "calculation.py";
  options.emit_source_banner = false;
  const auto result = mpf::Transpiler{}.transpile("value = 40 + 2\n", options);
  REQUIRE(result.success());
  REQUIRE(result.code.find("value = 40 + 2;") != std::string::npos);
}

TEST_CASE("transpilation returns a deterministic source map v3 for both targets") {
  for (const auto target : {mpf::TargetLanguage::javascript, mpf::TargetLanguage::cpp}) {
    mpf::TranspileOptions options;
    options.language = mpf::SourceLanguage::python;
    options.target = target;
    options.filename = "mapping.py";
    options.generated_filename = target == mpf::TargetLanguage::cpp ? "mapping.cpp" : "mapping.js";
    options.emit_source_banner = false;
    const auto result = mpf::Transpiler{}.transpile("value = 40 + 2\nprint(value)\n", options);
    REQUIRE(result.success());
    REQUIRE(!result.source_map.empty());
    REQUIRE(result.source_map.version == 3);
    REQUIRE(result.source_map.sources.size() == 1);
    REQUIRE(result.source_map.sources.front() == "mapping.py");
    REQUIRE(result.source_map.segments.size() >= 2);
    const auto first_json = result.source_map.to_json();
    REQUIRE(first_json == result.source_map.to_json());
    REQUIRE(first_json.find("\"version\":3") != std::string::npos);
    REQUIRE(first_json.find("mapping.py") != std::string::npos);
    REQUIRE(!result.source_map.mappings().empty());
    REQUIRE(result.dependencies.empty());
  }
}

TEST_CASE("ambiguous input produces a diagnostic and no output") {
  const auto result = mpf::Transpiler{}.transpile("x = 1\n");
  REQUIRE(!result.success());
  REQUIRE(result.code.empty());
  REQUIRE(!result.diagnostics.empty());
  REQUIRE(result.diagnostics.front().code == "MPF0002");
  REQUIRE(result.diagnostics.front().source_name == "<memory>");
  REQUIRE(result.diagnostics.front().source_id == 0);
}

TEST_CASE("unsupported syntax fails closed") {
  mpf::TranspileOptions options;
  options.language = mpf::SourceLanguage::python;
  const auto result =
      mpf::Transpiler{}.transpile("for item in values:\n    print(item)\n", options);
  REQUIRE(!result.success());
  REQUIRE(result.code.empty());
  REQUIRE(result.diagnostics.front().code == "MPF1200");
}

TEST_CASE("compilation resource limits fail closed at every materialized stage") {
  const auto run = [](const mpf::ResourceLimits& limits) {
    mpf::TranspileOptions options;
    options.language = mpf::SourceLanguage::python;
    options.emit_source_banner = false;
    options.resource_limits = limits;
    return mpf::Transpiler{}.transpile("value = [1, 2, 3]\nprint(value[0])\n", options);
  };
  const auto require_limit = [](const mpf::TranspileResult& result, const std::string& stage) {
    REQUIRE(!result.success());
    REQUIRE(result.code.empty());
    REQUIRE(result.dependencies.empty());
    REQUIRE(!result.diagnostics.empty());
    REQUIRE(result.diagnostics.front().code == "MPF0010");
    REQUIRE(result.diagnostics.front().message.find(stage) != std::string::npos);
  };

  mpf::ResourceLimits limits;
  limits.max_source_bytes = 1;
  require_limit(run(limits), "source-bytes");

  limits = {};
  limits.max_tokens = 1;
  require_limit(run(limits), "tokens");

  limits = {};
  limits.max_parser_depth = 0;
  require_limit(run(limits), "parser-depth");

  limits = {};
  limits.max_arena_bytes = 1;
  require_limit(run(limits), "arena-bytes");

  limits = {};
  limits.max_ast_nodes = 1;
  require_limit(run(limits), "ast-nodes");

  limits = {};
  limits.max_hir_nodes = 1;
  require_limit(run(limits), "hir-nodes");

  limits = {};
  limits.max_mir_instructions = 1;
  require_limit(run(limits), "mir-instructions");

  limits = {};
  limits.max_lir_nodes = 1;
  require_limit(run(limits), "target-lir-nodes");

  limits = {};
  limits.max_generated_bytes = 1;
  require_limit(run(limits), "generated-bytes");

  limits = {};
  limits.max_source_map_bytes = 1;
  require_limit(run(limits), "source-map-bytes");
}

TEST_CASE(
    "compilation report exposes deterministic stage identities and machine-readable metrics") {
  mpf::TranspileOptions options;
  options.language = mpf::SourceLanguage::python;
  options.emit_source_banner = false;
  const auto result = mpf::Transpiler{}.transpile("value = 1\nprint(value)\n", options);
  REQUIRE(result.success());
  REQUIRE(result.report.source_bytes != 0);
  REQUIRE(result.report.total_nanoseconds != 0);
  REQUIRE(result.report.peak_arena_bytes != 0);
  REQUIRE(result.report.stages.size() >= 7);
  REQUIRE(result.report.stages.front().stage == "source-complexity");
  REQUIRE(result.report.to_json().find("\"peakArenaBytes\":") != std::string::npos);
  REQUIRE(result.report.to_json().find("\"target-lir\"") != std::string::npos);
}

TEST_CASE("diagnostic text renderer includes source context and a stable range") {
  mpf::Diagnostic diagnostic{mpf::DiagnosticSeverity::error, "MPFTEST", "unexpected value", {2, 3}};
  diagnostic.end_location = {2, 6};
  diagnostic.source_name = "sample.py";
  const auto rendered = mpf::render_diagnostic_text(diagnostic, "first\nabcdef\n");
  REQUIRE(rendered.find("sample.py:2:3: error MPFTEST: unexpected value") != std::string::npos);
  REQUIRE(rendered.find("2 | abcdef") != std::string::npos);
  REQUIRE(rendered.find("^~~") != std::string::npos);
}

TEST_CASE("diagnostic JSON renderer escapes content and exposes schema version one") {
  mpf::Diagnostic diagnostic{
      mpf::DiagnosticSeverity::warning, "MPF\"TEST", "line one\nline two", {4, 2}};
  diagnostic.source_name = "quoted\\file.py";
  diagnostic.source_id = 7;
  const auto json = mpf::render_diagnostics_json({diagnostic});
  REQUIRE(json.find("\"schemaVersion\":1") != std::string::npos);
  REQUIRE(json.find("\"severity\":\"warning\"") != std::string::npos);
  REQUIRE(json.find("MPF\\\"TEST") != std::string::npos);
  REQUIRE(json.find("line one\\nline two") != std::string::npos);
  REQUIRE(json.find("\"sourceId\":7") != std::string::npos);
  REQUIRE(json.find("\"end\":{\"line\":4,\"column\":3}") != std::string::npos);
}
