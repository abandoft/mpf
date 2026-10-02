#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "expression_ast.hpp"

namespace mpf::detail {

enum class ArgumentDirection : std::uint8_t { input, output };

// Explicit call-boundary adaptations owned by MIR.  Class and size declarations can both change
// a Matlab argument before user code observes it.  They are independent flags because a call can
// require a class conversion, a shape conversion, or both.
enum class ArgumentBoundaryConversion : std::uint8_t {
  none = 0,
  matlab_class = 1U << 0U,
  matlab_size = 1U << 1U
};

[[nodiscard]] constexpr ArgumentBoundaryConversion operator|(
    const ArgumentBoundaryConversion left, const ArgumentBoundaryConversion right) noexcept {
  return static_cast<ArgumentBoundaryConversion>(static_cast<std::uint8_t>(left) |
                                                 static_cast<std::uint8_t>(right));
}

constexpr ArgumentBoundaryConversion& operator|=(ArgumentBoundaryConversion& left,
                                                 const ArgumentBoundaryConversion right) noexcept {
  left = left | right;
  return left;
}

[[nodiscard]] constexpr bool has_argument_boundary_conversion(
    const ArgumentBoundaryConversion value, const ArgumentBoundaryConversion expected) noexcept {
  return (static_cast<std::uint8_t>(value) & static_cast<std::uint8_t>(expected)) != 0U;
}

enum class ArgumentClassConstraint : std::uint8_t {
  none,
  matlab_double,
  matlab_logical,
  matlab_char
};

enum class ArgumentValidator : std::uint8_t {
  numeric,
  numeric_or_logical,
  floating,
  real,
  finite,
  non_nan,
  positive,
  nonpositive,
  nonnegative,
  negative,
  nonzero,
  integer,
  nonempty,
  scalar_or_empty,
  vector,
  row,
  column,
  matrix,
  nonmissing,
  nonzero_length_text,
  text,
  text_scalar,
  valid_variable_name,
  greater_than,
  greater_than_or_equal,
  less_than,
  less_than_or_equal
};

enum class ArgumentValidatorOperandKind : std::uint8_t { numeric_literal, input_argument };

[[nodiscard]] constexpr std::optional<std::size_t> argument_validator_operand_count(
    const ArgumentValidator validator) noexcept {
  const auto ordinal = static_cast<std::uint8_t>(validator);
  if (ordinal <= static_cast<std::uint8_t>(ArgumentValidator::valid_variable_name)) return 0U;
  if (ordinal <= static_cast<std::uint8_t>(ArgumentValidator::less_than_or_equal)) return 1U;
  return std::nullopt;
}

// Locale-independent finite binary64 normalization, shared by semantic construction and its
// independent verifier. The canonical token is a decimal floating literal in both targets.
[[nodiscard]] std::optional<std::string> normalize_argument_numeric_literal(std::string_view value);

// Frontend-owned spelling for the explicit operands of a parameterized validation function.
// The value being validated is implicit for the existing no-argument form and is checked by the
// Matlab parser when a parameterized call explicitly names it as the first call operand.
struct ArgumentValidatorOperandSyntax {
  ArgumentValidatorOperandKind kind{ArgumentValidatorOperandKind::numeric_literal};
  std::string value;
};

struct ArgumentValidatorSyntax {
  ArgumentValidator validator{ArgumentValidator::numeric};
  std::vector<ArgumentValidatorOperandSyntax> operands;
};

// Analyzer-owned validator operand.  Source names never cross this boundary: references are
// resolved to the input formal ordinal, while numeric literals remain validated target-neutral
// tokens so both target renderers serialize the exact same IEEE-754 value.
struct ArgumentValidatorOperandPlan {
  ArgumentValidatorOperandKind kind{ArgumentValidatorOperandKind::numeric_literal};
  std::string numeric_literal;
  std::size_t input_ordinal{dynamic_extent};
};

struct ArgumentValidatorPlan {
  ArgumentValidator validator{ArgumentValidator::numeric};
  std::vector<ArgumentValidatorOperandPlan> operands;
};

struct ArgumentDimensionConstraint {
  bool any{false};
  std::size_t extent{0};
};

// Language AST/HIR payload.  It records only source-declared restrictions; the Analyzer maps
// names to formal ordinals and materializes the normalized plan in the semantic side table.
struct ArgumentDeclarationSyntax {
  std::string name;
  std::size_t line{0};
  ArgumentDirection direction{ArgumentDirection::input};
  bool dimensions_declared{false};
  std::vector<ArgumentDimensionConstraint> dimensions;
  ArgumentClassConstraint class_constraint{ArgumentClassConstraint::none};
  std::vector<ArgumentValidatorSyntax> validators;
  bool has_default{false};
};

// Analyzer-owned contract consumed by MIR and target LIR.  `ordinal` indexes either the formal
// input list or the named output list according to `direction`.
struct ArgumentValidationPlan {
  std::size_t ordinal{0};
  std::size_t line{0};
  ArgumentDirection direction{ArgumentDirection::input};
  bool dimensions_declared{false};
  std::vector<ArgumentDimensionConstraint> dimensions;
  ArgumentClassConstraint class_constraint{ArgumentClassConstraint::none};
  std::vector<ArgumentValidatorPlan> validators;
  bool has_default{false};
  // Analyzer-owned ABI rank after class/size normalization. Scalars and character-vector
  // representations use rank zero; dense arrays use their concrete nested-container rank.
  std::size_t validated_rank{0};
};

// Per-call source-semantic adaptation contract.  `validated_rank` is the representation rank
// after Matlab's scalar/array normalization (zero for scalar and character-vector ABIs).
struct ArgumentCallBoundary {
  ArgumentBoundaryConversion conversion{ArgumentBoundaryConversion::none};
  ArgumentClassConstraint class_constraint{ArgumentClassConstraint::none};
  bool dimensions_declared{false};
  std::vector<ArgumentDimensionConstraint> dimensions;
  std::size_t validated_rank{0};
};

[[nodiscard]] constexpr bool operator==(const ArgumentDimensionConstraint left,
                                        const ArgumentDimensionConstraint right) noexcept {
  return left.any == right.any && left.extent == right.extent;
}

[[nodiscard]] inline bool operator==(const ArgumentDeclarationSyntax& left,
                                     const ArgumentDeclarationSyntax& right) noexcept {
  return left.name == right.name && left.line == right.line && left.direction == right.direction &&
         left.dimensions_declared == right.dimensions_declared &&
         left.dimensions == right.dimensions && left.class_constraint == right.class_constraint &&
         left.validators == right.validators && left.has_default == right.has_default;
}

[[nodiscard]] inline bool operator==(const ArgumentValidatorOperandSyntax& left,
                                     const ArgumentValidatorOperandSyntax& right) noexcept {
  return left.kind == right.kind && left.value == right.value;
}

[[nodiscard]] inline bool operator==(const ArgumentValidatorSyntax& left,
                                     const ArgumentValidatorSyntax& right) noexcept {
  return left.validator == right.validator && left.operands == right.operands;
}

[[nodiscard]] inline bool operator==(const ArgumentValidatorOperandPlan& left,
                                     const ArgumentValidatorOperandPlan& right) noexcept {
  return left.kind == right.kind && left.numeric_literal == right.numeric_literal &&
         left.input_ordinal == right.input_ordinal;
}

[[nodiscard]] inline bool operator==(const ArgumentValidatorPlan& left,
                                     const ArgumentValidatorPlan& right) noexcept {
  return left.validator == right.validator && left.operands == right.operands;
}

[[nodiscard]] inline bool operator==(const ArgumentValidationPlan& left,
                                     const ArgumentValidationPlan& right) noexcept {
  return left.ordinal == right.ordinal && left.line == right.line &&
         left.direction == right.direction &&
         left.dimensions_declared == right.dimensions_declared &&
         left.dimensions == right.dimensions && left.class_constraint == right.class_constraint &&
         left.validators == right.validators && left.has_default == right.has_default &&
         left.validated_rank == right.validated_rank;
}

[[nodiscard]] inline bool operator==(const ArgumentCallBoundary& left,
                                     const ArgumentCallBoundary& right) noexcept {
  return left.conversion == right.conversion && left.class_constraint == right.class_constraint &&
         left.dimensions_declared == right.dimensions_declared &&
         left.dimensions == right.dimensions && left.validated_rank == right.validated_rank;
}

[[nodiscard]] inline bool valid_argument_dimensions(
    const bool declared, const std::vector<ArgumentDimensionConstraint>& dimensions) noexcept {
  if (!declared) return dimensions.empty();
  if (dimensions.size() < 2U) return false;
  for (const auto dimension : dimensions) {
    if (dimension.any && dimension.extent != 0U) return false;
  }
  return true;
}

[[nodiscard]] inline bool valid_argument_numeric_literal(const std::string_view value) noexcept {
  if (value.empty()) return false;
  std::size_t cursor = 0U;
  if (value[cursor] == '+' || value[cursor] == '-') ++cursor;
  bool digits = false;
  while (cursor < value.size() && value[cursor] >= '0' && value[cursor] <= '9') {
    digits = true;
    ++cursor;
  }
  if (cursor < value.size() && value[cursor] == '.') {
    ++cursor;
    while (cursor < value.size() && value[cursor] >= '0' && value[cursor] <= '9') {
      digits = true;
      ++cursor;
    }
  }
  if (!digits) return false;
  if (cursor < value.size() && (value[cursor] == 'e' || value[cursor] == 'E')) {
    ++cursor;
    if (cursor < value.size() && (value[cursor] == '+' || value[cursor] == '-')) ++cursor;
    const auto exponent = cursor;
    while (cursor < value.size() && value[cursor] >= '0' && value[cursor] <= '9') ++cursor;
    if (cursor == exponent) return false;
  }
  return cursor == value.size();
}

[[nodiscard]] inline bool valid_argument_declaration_syntax(
    const ArgumentDeclarationSyntax& declaration) noexcept {
  if (declaration.name.empty() || declaration.line == 0U ||
      (declaration.direction != ArgumentDirection::input &&
       declaration.direction != ArgumentDirection::output) ||
      static_cast<std::uint8_t>(declaration.class_constraint) >
          static_cast<std::uint8_t>(ArgumentClassConstraint::matlab_char) ||
      !valid_argument_dimensions(declaration.dimensions_declared, declaration.dimensions) ||
      (declaration.has_default && declaration.direction != ArgumentDirection::input)) {
    return false;
  }
  for (const auto& validator : declaration.validators) {
    const auto operand_count = argument_validator_operand_count(validator.validator);
    if (!operand_count.has_value() || validator.operands.size() != *operand_count) return false;
    for (const auto& operand : validator.operands) {
      if (operand.value.empty() ||
          (operand.kind != ArgumentValidatorOperandKind::numeric_literal &&
           operand.kind != ArgumentValidatorOperandKind::input_argument) ||
          (operand.kind == ArgumentValidatorOperandKind::numeric_literal &&
           !valid_argument_numeric_literal(operand.value))) {
        return false;
      }
    }
  }
  return true;
}

[[nodiscard]] inline bool valid_argument_validation_plan(const ArgumentValidationPlan& plan,
                                                         const std::size_t input_count,
                                                         const std::size_t output_count) {
  const auto count = plan.direction == ArgumentDirection::input ? input_count : output_count;
  if (plan.ordinal >= count || plan.line == 0U ||
      (plan.direction != ArgumentDirection::input && plan.direction != ArgumentDirection::output) ||
      static_cast<std::uint8_t>(plan.class_constraint) >
          static_cast<std::uint8_t>(ArgumentClassConstraint::matlab_char) ||
      !valid_argument_dimensions(plan.dimensions_declared, plan.dimensions) ||
      (plan.has_default && plan.direction != ArgumentDirection::input)) {
    return false;
  }
  for (const auto& validator : plan.validators) {
    const auto operand_count = argument_validator_operand_count(validator.validator);
    if (!operand_count.has_value() || validator.operands.size() != *operand_count) return false;
    for (const auto& operand : validator.operands) {
      if (operand.kind == ArgumentValidatorOperandKind::numeric_literal) {
        const auto normalized = normalize_argument_numeric_literal(operand.numeric_literal);
        if (!normalized.has_value() || *normalized != operand.numeric_literal ||
            operand.input_ordinal != dynamic_extent)
          return false;
      } else if (operand.kind == ArgumentValidatorOperandKind::input_argument) {
        const bool visible =
            operand.input_ordinal < input_count &&
            (plan.direction == ArgumentDirection::output || operand.input_ordinal < plan.ordinal);
        if (!operand.numeric_literal.empty() || !visible) {
          return false;
        }
      } else {
        return false;
      }
    }
  }
  return true;
}

[[nodiscard]] inline bool valid_argument_validation_inventory(
    const std::vector<ArgumentValidationPlan>& plans, const std::size_t input_count,
    const std::size_t output_count) {
  std::optional<std::size_t> previous_input;
  std::optional<std::size_t> previous_output;
  bool optional_seen = false;
  for (const auto& plan : plans) {
    if (!valid_argument_validation_plan(plan, input_count, output_count)) return false;
    if (plan.direction == ArgumentDirection::input) {
      if (previous_output.has_value() ||
          (previous_input.has_value() && plan.ordinal <= *previous_input) ||
          (optional_seen && !plan.has_default))
        return false;
      previous_input = plan.ordinal;
      optional_seen = optional_seen || plan.has_default;
    } else {
      if (previous_output.has_value() && plan.ordinal <= *previous_output) return false;
      previous_output = plan.ordinal;
    }
  }
  return true;
}

// Each IR supplies its own formal type/shape evidence; resolved ordinals alone are not proof
// that a threshold has a scalar numeric/logical ABI.
template <typename ScalarNumericFormal>
[[nodiscard]] bool valid_argument_validator_references(
    const std::vector<ArgumentValidationPlan>& plans, ScalarNumericFormal scalar_numeric_formal) {
  for (const auto& plan : plans) {
    for (const auto& validator : plan.validators) {
      for (const auto& operand : validator.operands) {
        if (operand.kind == ArgumentValidatorOperandKind::input_argument &&
            !scalar_numeric_formal(operand.input_ordinal)) {
          return false;
        }
      }
    }
  }
  return true;
}

[[nodiscard]] inline bool scalar_argument_validator_formal(
    const std::vector<ArgumentValidationPlan>& plans, const std::size_t ordinal,
    const ValueType type, const bool scalar_shape) noexcept {
  if (!scalar_shape) return false;
  if (type == ValueType::real || type == ValueType::integer || type == ValueType::boolean)
    return true;
  if (type != ValueType::unknown) return false;
  // Unknown type is permitted only for an explicitly sized scalar, not an unknown-rank value.
  // Runtime validation preserves numeric/logical and complex storage checks for this ABI.
  for (const auto& plan : plans) {
    if (plan.direction != ArgumentDirection::input || plan.ordinal != ordinal ||
        !plan.dimensions_declared || plan.dimensions.empty())
      continue;
    for (const auto dimension : plan.dimensions) {
      if (dimension.any || dimension.extent != 1U) return false;
    }
    return true;
  }
  return false;
}

[[nodiscard]] inline bool valid_argument_call_boundary(
    const ArgumentCallBoundary& boundary) noexcept {
  constexpr auto known = static_cast<std::uint8_t>(ArgumentBoundaryConversion::matlab_class) |
                         static_cast<std::uint8_t>(ArgumentBoundaryConversion::matlab_size);
  const auto conversion = static_cast<std::uint8_t>(boundary.conversion);
  if ((conversion & static_cast<std::uint8_t>(~known)) != 0U ||
      !valid_argument_dimensions(boundary.dimensions_declared, boundary.dimensions)) {
    return false;
  }
  if (has_argument_boundary_conversion(boundary.conversion,
                                       ArgumentBoundaryConversion::matlab_class) &&
      boundary.class_constraint == ArgumentClassConstraint::none) {
    return false;
  }
  if (has_argument_boundary_conversion(boundary.conversion,
                                       ArgumentBoundaryConversion::matlab_size) &&
      !boundary.dimensions_declared) {
    return false;
  }
  if (!boundary.dimensions_declared && boundary.validated_rank != 0U) return false;
  return true;
}

}  // namespace mpf::detail
