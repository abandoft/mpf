#pragma once

#include "argument_validation.hpp"
#include "mpf/transpiler.hpp"

namespace mpf::detail {

struct ArgumentValidatorDefinition {
  ArgumentValidator validator;
  std::string_view name;
  LanguageVersion minimum_version;
  std::string_view minimum_release;
};

// An allocation-free, immutable source-semantic catalog. Name resolution consults it only after
// failing to resolve a lexical symbol; version/arity checks belong to subsequent semantic analysis.
[[nodiscard]] const ArgumentValidatorDefinition* find_argument_validator(
    std::string_view name) noexcept;
[[nodiscard]] const ArgumentValidatorDefinition* find_argument_validator(
    ArgumentValidator validator) noexcept;

}  // namespace mpf::detail
