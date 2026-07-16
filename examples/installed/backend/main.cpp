#include <array>
#include <string_view>

#include "mpf/transpiler.hpp"

int main() {
  constexpr std::string_view source = "value = abs(-42)\nprint(value)\n";
  for (const auto target :
       {mpf::TargetLanguage::javascript, mpf::TargetLanguage::cpp}) {
    mpf::TranspileOptions options;
    options.language = mpf::SourceLanguage::python;
    options.target = target;
    options.filename = "installed-example.py";
    options.emit_source_banner = false;
    const auto first = mpf::Transpiler{}.transpile(source, options);
    const auto second = mpf::Transpiler{}.transpile(source, options);
    if (!first.success() || first.code.empty() || first.code != second.code ||
        first.source_map.to_json() != second.source_map.to_json() ||
        first.dependencies != second.dependencies) {
      return 1;
    }
  }
  return 0;
}
