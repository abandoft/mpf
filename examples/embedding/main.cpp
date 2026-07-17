#include <iostream>
#include <mpf/mpf.hpp>

int main() {
  mpf::TranspileOptions options;
  options.language = mpf::SourceLanguage::python;
  options.target = mpf::TargetLanguage::cpp;
  const auto result = mpf::Transpiler{}.transpile("print(6 * 7)\n", options);
  if (!result.success()) {
    return 1;
  }
  std::cout << result.code;
  return 0;
}
