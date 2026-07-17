#pragma once

#include <vector>

#include "frontends/common/descriptor.hpp"

namespace mpf::detail {

[[nodiscard]] std::vector<Diagnostic> run_frontend_conformance(
    const FrontendDescriptor& descriptor, const SourceText& source,
    const FrontendParseOptions& options = {});

}  // namespace mpf::detail
