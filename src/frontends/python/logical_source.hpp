#pragma once

#include "frontends/common/logical_source.hpp"

namespace mpf::detail {

[[nodiscard]] LogicalSourceResult normalize_python_source(const SourceText& source);

}  // namespace mpf::detail
