#pragma once

#include "semantic_facts.hpp"

namespace mpf::detail::hir {
[[nodiscard]] bool argument_output_contract_matches(const StatementFacts& facts,
                                                    const ArgumentValidationPlan& plan) noexcept;
}  // namespace mpf::detail::hir
