#pragma once

#include "ir/hir.hpp"

namespace mpf::detail::semantic {

enum class ValidatorCallError { none, arity, validated_argument, operand, range_flag };

struct ValidatorSourceOperand {
  ArgumentValidatorOperandKind kind{ArgumentValidatorOperandKind::numeric_literal};
  std::string value;
  HirNodeId origin{};
};

struct ValidatorCallContract {
  ValidatorCallError error{ValidatorCallError::none};
  std::vector<ValidatorSourceOperand> operands;
  ArgumentRangeBoundary range_boundary{ArgumentRangeBoundary::inclusive};
};

// Decode only after name binding selects a standard builtin. The owned HIR call remains the
// authoritative source; no parser-produced copy of thresholds or flags crosses this boundary.
[[nodiscard]] ValidatorCallContract decode_standard_validator_call(
    const hir::Expression& expression, std::string_view formal, ArgumentValidator validator);
[[nodiscard]] std::string_view validator_call_error_message(ValidatorCallError error) noexcept;

}  // namespace mpf::detail::semantic
