#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
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
  valid_variable_name
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
  std::vector<ArgumentValidator> validators;
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
  std::vector<ArgumentValidator> validators;
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

[[nodiscard]] inline bool valid_argument_declaration_syntax(
    const ArgumentDeclarationSyntax& declaration) noexcept {
  return !declaration.name.empty() && declaration.line != 0U &&
         valid_argument_dimensions(declaration.dimensions_declared, declaration.dimensions) &&
         (!declaration.has_default || declaration.direction == ArgumentDirection::input);
}

[[nodiscard]] inline bool valid_argument_validation_plan(const ArgumentValidationPlan& plan,
                                                         const std::size_t input_count,
                                                         const std::size_t output_count) noexcept {
  const auto count = plan.direction == ArgumentDirection::input ? input_count : output_count;
  return plan.ordinal < count && plan.line != 0U &&
         valid_argument_dimensions(plan.dimensions_declared, plan.dimensions) &&
         (!plan.has_default || plan.direction == ArgumentDirection::input);
}

[[nodiscard]] inline bool valid_argument_validation_inventory(
    const std::vector<ArgumentValidationPlan>& plans, const std::size_t input_count,
    const std::size_t output_count) {
  std::vector<bool> inputs(input_count, false);
  std::vector<bool> outputs(output_count, false);
  for (const auto& plan : plans) {
    if (!valid_argument_validation_plan(plan, input_count, output_count)) return false;
    auto& seen = plan.direction == ArgumentDirection::input ? inputs : outputs;
    if (seen[plan.ordinal]) return false;
    seen[plan.ordinal] = true;
  }
  return true;
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
