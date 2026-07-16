#pragma once

#include <vector>

#include "../ir/mir.hpp"

namespace mpf::detail {

[[nodiscard]] std::vector<Diagnostic> validate_javascript_capabilities(
    const mir::Program& program, const mir::AliasEffectTable& alias_effects);

}  // namespace mpf::detail
