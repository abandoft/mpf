#pragma once

#include <cstdint>

namespace mpf::detail {

// Numeric class and complexity are independent source-semantic axes.  ValueType answers the
// structural question (scalar/list/tuple/etc.); NumericType records the scalar representation
// carried by that value or by a container element.  Keeping complexity orthogonal is required
// for Matlab, where double and single values can both be real or complex.
enum class NumericClass : std::uint8_t { none, unknown, logical, signed_integer, binary64 };

enum class NumericComplexity : std::uint8_t { none, unknown, real, complex };

struct NumericType {
  NumericClass value_class{NumericClass::none};
  NumericComplexity complexity{NumericComplexity::none};

  [[nodiscard]] constexpr bool present() const noexcept {
    return value_class != NumericClass::none;
  }

  [[nodiscard]] constexpr bool known() const noexcept {
    return value_class != NumericClass::none && value_class != NumericClass::unknown &&
           complexity != NumericComplexity::none && complexity != NumericComplexity::unknown;
  }

  [[nodiscard]] constexpr bool valid() const noexcept {
    if (value_class == NumericClass::none) return complexity == NumericComplexity::none;
    if (value_class == NumericClass::unknown) {
      return complexity == NumericComplexity::unknown;
    }
    if (complexity != NumericComplexity::real && complexity != NumericComplexity::complex) {
      return false;
    }
    // Matlab complex storage is currently defined only for its floating numeric classes.
    return complexity != NumericComplexity::complex || value_class == NumericClass::binary64;
  }
};

[[nodiscard]] constexpr bool operator==(const NumericType left, const NumericType right) noexcept {
  return left.value_class == right.value_class && left.complexity == right.complexity;
}

[[nodiscard]] constexpr bool operator!=(const NumericType left, const NumericType right) noexcept {
  return !(left == right);
}

inline constexpr NumericType no_numeric_type{};
inline constexpr NumericType unknown_numeric_type{NumericClass::unknown,
                                                  NumericComplexity::unknown};
inline constexpr NumericType logical_numeric_type{NumericClass::logical, NumericComplexity::real};
inline constexpr NumericType integer_numeric_type{NumericClass::signed_integer,
                                                  NumericComplexity::real};
inline constexpr NumericType real_numeric_type{NumericClass::binary64, NumericComplexity::real};
inline constexpr NumericType complex_numeric_type{NumericClass::binary64,
                                                  NumericComplexity::complex};

[[nodiscard]] constexpr NumericType join_numeric_types(const NumericType left,
                                                       const NumericType right) noexcept {
  if (left == right) return left;
  if (!left.present()) return right;
  if (!right.present()) return left;
  if (left == unknown_numeric_type) return right;
  if (right == unknown_numeric_type) return left;

  NumericClass value_class = NumericClass::unknown;
  if (left.value_class == right.value_class) {
    value_class = left.value_class;
  } else if (left.value_class == NumericClass::binary64 ||
             right.value_class == NumericClass::binary64) {
    value_class = NumericClass::binary64;
  } else if (left.value_class == NumericClass::signed_integer ||
             right.value_class == NumericClass::signed_integer) {
    value_class = NumericClass::signed_integer;
  } else if (left.value_class == NumericClass::logical &&
             right.value_class == NumericClass::logical) {
    value_class = NumericClass::logical;
  }

  const auto complexity = left.complexity == NumericComplexity::complex ||
                                  right.complexity == NumericComplexity::complex
                              ? NumericComplexity::complex
                              : (left.complexity == NumericComplexity::real &&
                                         right.complexity == NumericComplexity::real
                                     ? NumericComplexity::real
                                     : NumericComplexity::unknown);
  NumericType result{value_class, complexity};
  return result.valid() ? result : unknown_numeric_type;
}

}  // namespace mpf::detail
