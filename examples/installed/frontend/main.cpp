#include <array>
#include <string_view>

#include <mpf/mpf.hpp>

int main() {
  struct Case {
    mpf::SourceLanguage language;
    std::string_view source;
  };
  constexpr std::array<Case, 4> cases{{
      {mpf::SourceLanguage::python, "value = 42\nprint(value)\n"},
      {mpf::SourceLanguage::matlab, "value = 42;\ndisp(value);\n"},
      {mpf::SourceLanguage::fortran,
       "program example\nimplicit none\ninteger :: value\nvalue = 42\nprint *, value\nend program "
       "example\n"},
      {mpf::SourceLanguage::typescript,
       "const value: number = 42;\nconsole.log(value);\n"},
  }};
  for (const auto& value : cases) {
    mpf::TranspileOptions options;
    options.language = value.language;
    options.target = mpf::TargetLanguage::javascript;
    options.emit_source_banner = false;
    const auto result = mpf::Transpiler{}.transpile(value.source, options);
    if (!result.success() || result.code.empty()) return 1;
  }
  return 0;
}
