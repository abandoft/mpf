#include <cstddef>
#include <cstdint>
#include <string_view>

#include "mpf/transpiler.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, const std::size_t size) {
  if (size == 0) return 0;
  mpf::TranspileOptions options;
  switch (data[0] % 3U) {
    case 0: options.language = mpf::SourceLanguage::python; break;
    case 1: options.language = mpf::SourceLanguage::matlab; break;
    default: options.language = mpf::SourceLanguage::fortran; break;
  }
  options.target =
      size > 1 && (data[1] & 1U) != 0 ? mpf::TargetLanguage::cpp : mpf::TargetLanguage::javascript;
  options.emit_source_banner = false;
  options.resource_limits.max_source_bytes = 1024U * 1024U;
  options.resource_limits.max_tokens = 256U * 1024U;
  options.resource_limits.max_ast_nodes = 256U * 1024U;
  options.resource_limits.max_hir_nodes = 256U * 1024U;
  options.resource_limits.max_mir_instructions = 512U * 1024U;
  options.resource_limits.max_lir_nodes = 512U * 1024U;
  options.resource_limits.max_generated_bytes = 8U * 1024U * 1024U;
  const auto source = std::string_view(reinterpret_cast<const char*>(data + 1U), size - 1U);
  static_cast<void>(mpf::Transpiler{}.transpile(source, options));
  return 0;
}
