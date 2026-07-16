#include "backend_conformance.hpp"

#include <algorithm>
#include <iterator>
#include <string>
#include <utility>

#include "../compiler/code_binding.hpp"
#include "../core/backend_registry.hpp"

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

std::vector<Diagnostic> run_backend_conformance(const BackendDescriptor& descriptor,
                                                const mir::Program& program,
                                                const TranspileOptions& options) {
  std::vector<Diagnostic> diagnostics;
  const BackendDescriptor* catalog[]{&descriptor};
  if (!validate_backend_catalog(catalog, 1, true)) {
    conformance_error(diagnostics, "backend descriptor contract is invalid");
    return diagnostics;
  }
  append(diagnostics, mir::verify(program, "backend-conformance"));
  append(diagnostics, validate_code_bindings(program, descriptor.binding, descriptor.name));
  append(diagnostics, descriptor.validate(program));
  if (has_error(diagnostics)) return diagnostics;

  auto first = descriptor.lower(program, options);
  auto second = descriptor.lower(program, options);
  append(diagnostics, std::move(first.diagnostics));
  append(diagnostics, std::move(second.diagnostics));
  if (has_error(diagnostics)) return diagnostics;
  if (first.artifact == nullptr || second.artifact == nullptr ||
      first.artifact->target() != descriptor.target ||
      second.artifact->target() != descriptor.target) {
    conformance_error(diagnostics, "backend lowering produced an invalid target artifact");
    return diagnostics;
  }
  append(diagnostics, descriptor.verify(*first.artifact));
  append(diagnostics, descriptor.verify(*second.artifact));
  if (has_error(diagnostics)) return diagnostics;

  const auto first_code = descriptor.emit(*first.artifact, options);
  const auto second_code = descriptor.emit(*second.artifact, options);
  if (first_code.empty()) {
    conformance_error(diagnostics, "backend emitted an empty program");
  } else if (first_code != second_code) {
    conformance_error(diagnostics, "backend lowering or emission is not deterministic");
  }
  return diagnostics;
}

}  // namespace mpf::detail
