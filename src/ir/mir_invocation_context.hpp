#pragma once

#include <string_view>
#include <vector>

#include "mir.hpp"

namespace mpf::detail::mir {

[[nodiscard]] bool is_output_count_query(const Program& program, const Expression& expression);
void verify_invocation_contexts(const Program& program, std::vector<Diagnostic>& diagnostics,
                                std::string_view stage);

}  // namespace mpf::detail::mir
