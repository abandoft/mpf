#include "frontend.hpp"

#include "../frontends/frontend_registry.hpp"

namespace mpf::detail {

FrontendParseResult parse_program(const SourceText& source, const SourceLanguage language,
                                  const FortranSourceForm fortran_source_form) {
  const auto* frontend = find_frontend(language);
  if (frontend != nullptr) {
    return frontend->parse(source, {fortran_source_form, frontend->manifest.maximum_version,
                                    std::pmr::get_default_resource()});
  }
  FrontendParseResult result;
  result.diagnostics.push_back(
      {DiagnosticSeverity::error, "MPF0002", "source language could not be determined", {1, 1}});
  return result;
}

}  // namespace mpf::detail
