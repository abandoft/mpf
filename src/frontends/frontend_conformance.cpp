#include "frontend_conformance.hpp"

#include <algorithm>
#include <iterator>
#include <string>
#include <utility>

#include "../ir/hir_dump.hpp"
#include "frontend_registry.hpp"

namespace mpf::detail {
namespace {

bool has_error(const std::vector<Diagnostic>& diagnostics) {
  return std::any_of(diagnostics.begin(), diagnostics.end(), [](const Diagnostic& diagnostic) {
    return diagnostic.severity == DiagnosticSeverity::error;
  });
}

void append(std::vector<Diagnostic>& destination, std::vector<Diagnostic> source) {
  destination.insert(destination.end(), std::make_move_iterator(source.begin()),
                     std::make_move_iterator(source.end()));
}

void conformance_error(std::vector<Diagnostic>& diagnostics, std::string message) {
  diagnostics.push_back({DiagnosticSeverity::error, "MPF0009", std::move(message), {1, 1}});
}

}  // namespace

std::vector<Diagnostic> run_frontend_conformance(const FrontendDescriptor& descriptor,
                                                 const SourceText& source,
                                                 const FrontendParseOptions& options) {
  std::vector<Diagnostic> diagnostics;
  const FrontendDescriptor* catalog[]{&descriptor};
  if (!validate_frontend_catalog(catalog, 1)) {
    conformance_error(diagnostics, "frontend descriptor contract is invalid");
    return diagnostics;
  }

  auto first = descriptor.parse(source, options);
  auto second = descriptor.parse(source, options);
  append(diagnostics, std::move(first.diagnostics));
  append(diagnostics, std::move(second.diagnostics));
  if (has_error(diagnostics)) return diagnostics;
  append(diagnostics, descriptor.verify(first.ast));
  append(diagnostics, descriptor.verify(second.ast));
  if (has_error(diagnostics)) return diagnostics;

  auto first_hir = descriptor.lower(std::move(first.ast));
  auto second_hir = descriptor.lower(std::move(second.ast));
  append(diagnostics, std::move(first_hir.diagnostics));
  append(diagnostics, std::move(second_hir.diagnostics));
  if (has_error(diagnostics)) return diagnostics;
  if (first_hir.program.language != descriptor.language ||
      second_hir.program.language != descriptor.language) {
    conformance_error(diagnostics, "frontend lowering changed the language identity");
  } else if (dump_hir(first_hir.program) != dump_hir(second_hir.program)) {
    conformance_error(diagnostics, "frontend parse and AST-to-HIR lowering are not deterministic");
  }
  return diagnostics;
}

}  // namespace mpf::detail
