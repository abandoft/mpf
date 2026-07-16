#include <mpf/transpiler.hpp>
#include <mpf/version.hpp>
#include <string>
#include <string_view>

int main() {
  if (std::string_view{MPF_VERSION_STRING} != MPF_EXPECT_VERSION) return 6;
  mpf::Diagnostic diagnostic{mpf::DiagnosticSeverity::note, "MPFTEST", "consumer", {1, 1}};
  diagnostic.source_name = "consumer.cpp";
  const auto diagnostic_json = mpf::render_diagnostics_json({diagnostic});
  if (diagnostic_json.find("\"schemaVersion\":1") == std::string::npos) return 4;
  if (mpf::registered_source_languages().size() != 4 ||
      mpf::registered_target_languages().size() != 2 || !mpf::source_language_name_known("py") ||
      !mpf::target_language_name_known("cpp")) {
    return 5;
  }

  const bool javascript = mpf::backend_available(mpf::TargetLanguage::javascript);
  const bool cpp = mpf::backend_available(mpf::TargetLanguage::cpp);
  if (javascript != static_cast<bool>(MPF_EXPECT_JAVASCRIPT) ||
      cpp != static_cast<bool>(MPF_EXPECT_CPP)) {
    return 1;
  }

  mpf::TranspileOptions options;
  options.language = mpf::SourceLanguage::python;
  options.emit_source_banner = false;
  options.target = javascript ? mpf::TargetLanguage::javascript : mpf::TargetLanguage::cpp;
  if (javascript || cpp) {
    return mpf::Transpiler{}.transpile("print(42)\n", options).success() ? 0 : 2;
  }
  const auto unavailable = mpf::Transpiler{}.transpile("print(42)\n", options);
  return !unavailable.success() && !unavailable.diagnostics.empty() &&
                 unavailable.diagnostics.front().code == "MPF0003"
             ? 0
             : 3;
}
