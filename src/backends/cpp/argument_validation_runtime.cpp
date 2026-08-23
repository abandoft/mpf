#include "backends/cpp/argument_validation_runtime.hpp"

#include <ostream>

namespace mpf::detail {

void emit_cpp_argument_validation_runtime(std::ostream& output) {
  output << R"mpf(template <typename T>
struct argument_is_vector : std::false_type {};
template <typename T, typename A>
struct argument_is_vector<std::vector<T, A>> : std::true_type {};
template <typename T>
struct argument_is_complex : std::false_type {};
template <typename T>
struct argument_is_complex<std::complex<T>> : std::true_type {};

template <typename T>
struct argument_vector_depth : std::integral_constant<std::size_t, 0U> {};
template <typename T, typename A>
struct argument_vector_depth<std::vector<T, A>>
    : std::integral_constant<std::size_t, 1U + argument_vector_depth<T>::value> {};

template <typename T>
struct argument_scalar {
  using type = std::decay_t<T>;
};
template <typename T, typename A>
struct argument_scalar<std::vector<T, A>> : argument_scalar<T> {};
template <typename T>
using argument_scalar_t = typename argument_scalar<std::decay_t<T>>::type;

template <typename T, std::size_t Rank>
struct argument_nested {
  using type = std::vector<typename argument_nested<T, Rank - 1U>::type>;
};
template <typename T>
struct argument_nested<T, 0U> {
  using type = T;
};
template <typename T, std::size_t Rank>
using argument_nested_t = typename argument_nested<T, Rank>::type;

template <typename T>
void argument_structural_shape(const T&, std::vector<std::size_t>&) {}
template <typename T>
void argument_structural_shape(const std::vector<T>& values,
                               std::vector<std::size_t>& shape) {
  shape.push_back(values.size());
  if (values.empty()) {
    shape.insert(shape.end(), argument_vector_depth<T>::value, 0U);
    return;
  }
  std::vector<std::size_t> child;
  if constexpr (argument_is_vector<T>::value) {
    argument_structural_shape(values.front(), child);
    for (std::size_t index = 1U; index < values.size(); ++index) {
      std::vector<std::size_t> actual;
      argument_structural_shape(values[index], actual);
      if (actual != child) throw std::invalid_argument("MPF Matlab argument must be "
                                                       "rectangular");
    }
  }
  shape.insert(shape.end(), child.begin(), child.end());
}

template <typename T>
void argument_shape(const T&, std::vector<std::size_t>& shape) {
  shape = {1U, 1U};
}
inline void argument_shape(const std::string& value, std::vector<std::size_t>& shape) {
  shape = {1U, value.size()};
}
template <typename T>
void argument_shape(const std::vector<T>& values, std::vector<std::size_t>& shape) {
  argument_structural_shape(values, shape);
  if (shape.size() == 1U) shape.insert(shape.begin(), 1U);
}

template <typename T, typename Predicate>
bool argument_all(const T& value, const Predicate& predicate) {
  if constexpr (argument_is_vector<T>::value) {
    return std::all_of(value.begin(), value.end(), [&](const auto& item) {
      return argument_all(item, predicate);
    });
  } else {
    return predicate(value);
  }
}

inline std::size_t argument_size(const std::vector<std::size_t>& shape) {
  std::size_t result = 1U;
  for (const auto extent : shape) {
    if (extent != 0U && result > std::numeric_limits<std::size_t>::max() / extent)
      throw std::length_error("MPF Matlab argument shape exceeds size limits");
    result *= extent;
  }
  return result;
}

inline std::vector<std::size_t> argument_coordinates(
    std::size_t linear, const std::vector<std::size_t>& shape) {
  std::vector<std::size_t> result(shape.size(), 0U);
  for (std::size_t axis = 0U; axis < shape.size(); ++axis) {
    if (shape[axis] == 0U)
      throw std::invalid_argument("MPF Matlab argument has an invalid empty "
                                  "coordinate");
    result[axis] = linear % shape[axis];
    linear /= shape[axis];
  }
  return result;
}

template <typename T>
argument_scalar_t<T> argument_at(const T& value,
                                 const std::vector<std::size_t>& coordinates,
                                 const std::size_t axis) {
  if constexpr (argument_is_vector<std::decay_t<T>>::value) {
    return argument_at(value.at(coordinates.at(axis)), coordinates, axis + 1U);
  } else {
    return value;
  }
}

template <typename T>
std::vector<argument_scalar_t<T>> argument_flatten_column_major(
    const T& value, const std::vector<std::size_t>& shape) {
  const auto size = argument_size(shape);
  std::vector<argument_scalar_t<T>> result;
  result.reserve(size);
  if constexpr (argument_is_vector<std::decay_t<T>>::value) {
    constexpr auto depth = argument_vector_depth<std::decay_t<T>>::value;
    if (shape.size() < depth)
      throw std::invalid_argument("MPF Matlab argument rank is inconsistent");
    const auto first_axis = shape.size() - depth;
    for (std::size_t linear = 0U; linear < size; ++linear) {
      result.push_back(argument_at(value, argument_coordinates(linear, shape), first_axis));
    }
  } else {
    if (size != 1U) throw std::invalid_argument("MPF Matlab scalar argument has an "
                                                "invalid shape");
    result.push_back(value);
  }
  return result;
}

inline std::vector<std::size_t> resolve_argument_shape(
    const std::vector<std::size_t>& source,
    const std::vector<std::int64_t>& dimensions) {
  if (dimensions.empty()) return source;
  const auto source_size = argument_size(source);
  std::vector<std::size_t> target(dimensions.size(), 0U);
  std::vector<std::size_t> variable_axes;
  bool same_rank_match = source.size() == dimensions.size();
  for (std::size_t axis = 0U; axis < dimensions.size(); ++axis) {
    if (dimensions[axis] < 0) {
      variable_axes.push_back(axis);
      if (same_rank_match) target[axis] = source[axis];
    } else {
      target[axis] = static_cast<std::size_t>(dimensions[axis]);
      same_rank_match = same_rank_match && source[axis] == target[axis];
    }
  }
  if (same_rank_match) return target;
  if (variable_axes.size() == 1U) {
    std::size_t fixed = 1U;
    for (std::size_t axis = 0U; axis < target.size(); ++axis) {
      if (axis == variable_axes.front()) continue;
      if (target[axis] != 0U && fixed > std::numeric_limits<std::size_t>::max() / target[axis])
        throw std::length_error("MPF Matlab argument shape exceeds size limits");
      fixed *= target[axis];
    }
    if (fixed == 0U) {
      if (source_size != 0U)
        throw std::invalid_argument("MPF Matlab argument size is incompatible");
      target[variable_axes.front()] = 0U;
    } else if (source_size % fixed == 0U) {
      target[variable_axes.front()] = source_size / fixed;
    }
  } else if (!variable_axes.empty()) {
    throw std::invalid_argument("MPF Matlab argument size is underdetermined");
  }
  const auto target_size = argument_size(target);
  if (target_size != source_size && source_size != 1U)
    throw std::invalid_argument("MPF Matlab argument size is incompatible");
  return target;
}

template <typename Target>
Target make_argument_target(const std::vector<std::size_t>& shape,
                            const std::size_t axis = 0U) {
  if constexpr (argument_is_vector<Target>::value) {
    Target result(shape.at(axis));
    if constexpr (argument_is_vector<typename Target::value_type>::value) {
      for (auto& item : result)
        item = make_argument_target<typename Target::value_type>(shape, axis + 1U);
    }
    return result;
  } else {
    return Target{};
  }
}

template <typename Target, typename Scalar>
void set_argument_target(Target& target, const std::vector<std::size_t>& coordinates,
                         const std::size_t axis, const Scalar& value) {
  if constexpr (argument_is_vector<Target>::value) {
    using Child = typename Target::value_type;
    if constexpr (argument_is_vector<Child>::value) {
      set_argument_target(target.at(coordinates.at(axis)), coordinates, axis + 1U, value);
    } else {
      target.at(coordinates.at(axis)) = static_cast<Child>(value);
    }
  } else {
    target = static_cast<Target>(value);
  }
}

template <typename Scalar, std::size_t Rank, typename Source, typename Convert>
argument_nested_t<Scalar, Rank> convert_argument_impl(
    const Source& value, const std::vector<std::int64_t>& dimensions,
    const Convert& convert) {
  std::vector<std::size_t> source_shape;
  argument_shape(value, source_shape);
  const auto target_shape = resolve_argument_shape(source_shape, dimensions);
  const auto source_values = argument_flatten_column_major(value, source_shape);
  const auto target_size = argument_size(target_shape);
  if constexpr (Rank == 0U) {
    if (target_size != 1U || source_values.size() != 1U)
      throw std::invalid_argument("MPF Matlab scalar argument size is incompatible");
    return convert(source_values.front());
  } else {
    if (target_shape.size() != Rank)
      throw std::invalid_argument("MPF Matlab argument rank is incompatible");
    auto result = make_argument_target<argument_nested_t<Scalar, Rank>>(target_shape);
    for (std::size_t linear = 0U; linear < target_size; ++linear) {
      const auto source_index = source_values.size() == 1U ? 0U : linear;
      set_argument_target(result, argument_coordinates(linear, target_shape), 0U,
                          convert(source_values.at(source_index)));
    }
    return result;
  }
}

template <std::size_t Rank, typename Source>
argument_nested_t<double, Rank> convert_argument_double(
    const Source& value, const std::vector<std::int64_t>& dimensions) {
  return convert_argument_impl<double, Rank>(
      value, dimensions, [](const auto& item) {
        using Item = std::decay_t<decltype(item)>;
        if constexpr (std::is_arithmetic_v<Item>) return static_cast<double>(item);
        throw std::invalid_argument(
            "MPF Matlab argument failed double class conversion");
      });
}

template <std::size_t Rank, typename Source>
argument_nested_t<bool, Rank> convert_argument_logical(
    const Source& value, const std::vector<std::int64_t>& dimensions) {
  return convert_argument_impl<bool, Rank>(value, dimensions, [](const auto& item) {
    using Item = std::decay_t<decltype(item)>;
    if constexpr (std::is_arithmetic_v<Item>) return static_cast<bool>(item);
    throw std::invalid_argument(
        "MPF Matlab argument failed logical class conversion");
  });
}

template <std::size_t Rank, typename Source>
argument_nested_t<argument_scalar_t<Source>, Rank> convert_argument_size(
    const Source& value, const std::vector<std::int64_t>& dimensions) {
  using Scalar = argument_scalar_t<Source>;
  return convert_argument_impl<Scalar, Rank>(
      value, dimensions, [](const auto& item) { return Scalar{item}; });
}

template <std::size_t Rank, typename Target>
void normalize_argument_size(Target& value,
                             const std::vector<std::int64_t>& dimensions) {
  static_assert(Rank > 0U, "MPF Matlab array argument rank must be positive");
  if constexpr (argument_vector_depth<std::decay_t<Target>>::value == Rank) {
    std::vector<std::size_t> source_shape;
    argument_shape(value, source_shape);
    const auto target_shape = resolve_argument_shape(source_shape, dimensions);
    if (source_shape != target_shape) value = convert_argument_size<Rank>(value, dimensions);
  } else {
    throw std::invalid_argument("MPF Matlab argument rank is incompatible");
  }
}

inline std::string_view argument_validator_name(const std::uint8_t validator) noexcept {
  switch (validator) {
    case 0U: return "mustBeNumeric";
    case 1U: return "mustBeNumericOrLogical";
    case 2U: return "mustBeFloat";
    case 3U: return "mustBeReal";
    case 4U: return "mustBeFinite";
    case 5U: return "mustBeNonNan";
    case 6U: return "mustBePositive";
    case 7U: return "mustBeNonpositive";
    case 8U: return "mustBeNonnegative";
    case 9U: return "mustBeNegative";
    case 10U: return "mustBeNonzero";
    case 11U: return "mustBeInteger";
    case 12U: return "mustBeNonempty";
    case 13U: return "mustBeScalarOrEmpty";
    case 14U: return "mustBeVector";
    case 15U: return "mustBeRow";
    case 16U: return "mustBeColumn";
    case 17U: return "mustBeMatrix";
    case 18U: return "mustBeNonmissing";
    case 19U: return "mustBeNonzeroLengthText";
    case 20U: return "mustBeText";
    case 21U: return "mustBeTextScalar";
    case 22U: return "mustBeValidVariableName";
    default: return "unknown validator";
  }
}

inline bool argument_is_matlab_keyword(const std::string_view value) noexcept {
  static constexpr std::array<std::string_view, 20> keywords{
      "break",      "case",       "catch",  "classdef", "continue",
      "else",       "elseif",     "end",    "for",      "function",
      "global",     "if",         "otherwise", "parfor", "persistent",
      "return",     "spmd",       "switch", "try",      "while"};
  return std::find(keywords.begin(), keywords.end(), value) != keywords.end();
}

template <typename T>
void validate_argument(const T& value, const std::string_view name,
                       const std::string_view direction,
                       const std::vector<std::int64_t>& dimensions,
                       const std::uint8_t class_constraint,
                       const std::vector<std::uint8_t>& validators) {
  std::vector<std::size_t> shape;
  argument_shape(value, shape);
  const auto fail = [&](const std::string_view requirement) {
    throw std::invalid_argument("MPF Matlab argument '" + std::string(name) +
                                "' failed " + std::string(requirement));
  };
  if (!dimensions.empty()) {
    const auto size_requirement =
        direction == "input" ? "input size validation" : "output size validation";
    if (shape.size() != dimensions.size()) fail(size_requirement);
    bool matches = shape.size() == dimensions.size();
    for (std::size_t axis = 0U; axis < dimensions.size() && axis < shape.size(); ++axis) {
      if (dimensions[axis] >= 0 && shape[axis] != static_cast<std::size_t>(dimensions[axis])) {
        matches = false;
      }
    }
    if (!matches) {
      const bool empty = argument_size(shape) == 0U;
      const bool target_allows_empty =
          std::any_of(dimensions.begin(), dimensions.end(),
                      [](const std::int64_t extent) { return extent <= 0; });
      if (!empty || !target_allows_empty) fail(size_requirement);
    }
  }
  const auto numeric = [&](const auto& item) {
    using Item = std::decay_t<decltype(item)>;
    return (std::is_arithmetic_v<Item> && !std::is_same_v<Item, bool>) ||
           argument_is_complex<Item>::value;
  };
  const auto numeric_or_logical = [&](const auto& item) {
    using Item = std::decay_t<decltype(item)>;
    return std::is_arithmetic_v<Item> || argument_is_complex<Item>::value;
  };
  const bool empty = argument_size(shape) == 0U;
  constexpr bool character = std::is_same_v<std::decay_t<T>, std::string>;
  if (class_constraint == 1U && !argument_all(value, numeric)) fail("double class "
                                                                    "validation");
  if (class_constraint == 2U && !argument_all(value, [](const auto& item) {
        return std::is_same_v<std::decay_t<decltype(item)>, bool>;
      })) {
    fail("logical class validation");
  }
  if (class_constraint == 3U && !std::is_same_v<std::decay_t<T>, std::string>)
    fail("char class validation");
  for (const auto validator : validators) {
    bool valid = true;
    switch (validator) {
      case 0U: valid = empty || argument_all(value, numeric); break;
      case 1U: valid = empty || argument_all(value, numeric_or_logical); break;
      case 2U:
        valid = empty || argument_all(value, [](const auto& item) {
          using Item = std::decay_t<decltype(item)>;
          return std::is_floating_point_v<Item> || argument_is_complex<Item>::value;
        });
        break;
      case 3U:
        valid = empty || character || argument_all(value, [](const auto& item) {
          using Item = std::decay_t<decltype(item)>;
          return std::is_arithmetic_v<Item> && !argument_is_complex<Item>::value;
        });
        break;
      case 4U:
        valid = empty || character || argument_all(value, [](const auto& item) {
          using Item = std::decay_t<decltype(item)>;
          if constexpr (argument_is_complex<Item>::value)
            return std::isfinite(item.real()) && std::isfinite(item.imag());
          if constexpr (std::is_arithmetic_v<Item>)
            return std::isfinite(static_cast<double>(item));
          return false;
        });
        break;
      case 5U:
        valid = empty || character || argument_all(value, [](const auto& item) {
          using Item = std::decay_t<decltype(item)>;
          if constexpr (argument_is_complex<Item>::value)
            return !std::isnan(item.real()) && !std::isnan(item.imag());
          if constexpr (std::is_arithmetic_v<Item>)
            return !std::isnan(static_cast<double>(item));
          return false;
        });
        break;
      case 6U:
        valid = empty || argument_all(value, [](const auto& item) {
          using Item = std::decay_t<decltype(item)>;
          if constexpr (std::is_arithmetic_v<Item>) return static_cast<double>(item) > 0.0;
          return false;
        });
        break;
      case 7U:
        valid = empty || argument_all(value, [](const auto& item) {
          using Item = std::decay_t<decltype(item)>;
          if constexpr (std::is_arithmetic_v<Item>) return static_cast<double>(item) <= 0.0;
          return false;
        });
        break;
      case 8U:
        valid = empty || argument_all(value, [](const auto& item) {
          using Item = std::decay_t<decltype(item)>;
          if constexpr (std::is_arithmetic_v<Item>) return static_cast<double>(item) >= 0.0;
          return false;
        });
        break;
      case 9U:
        valid = empty || argument_all(value, [](const auto& item) {
          using Item = std::decay_t<decltype(item)>;
          if constexpr (std::is_arithmetic_v<Item>) return static_cast<double>(item) < 0.0;
          return false;
        });
        break;
      case 10U:
        valid = empty || argument_all(value, [](const auto& item) {
          using Item = std::decay_t<decltype(item)>;
          if constexpr (argument_is_complex<Item>::value)
            return item.real() != 0 || item.imag() != 0;
          if constexpr (std::is_arithmetic_v<Item>) return item != 0;
          return false;
        });
        break;
      case 11U:
        valid = empty || argument_all(value, [](const auto& item) {
          using Item = std::decay_t<decltype(item)>;
          if constexpr (std::is_arithmetic_v<Item>) {
            return std::isfinite(static_cast<double>(item)) &&
                   static_cast<double>(item) ==
                       std::floor(static_cast<double>(item));
          } else {
            return false;
          }
        });
        break;
      case 12U: valid = argument_size(shape) != 0U; break;
      case 13U: valid = argument_size(shape) <= 1U; break;
      case 14U: valid = shape.size() == 2U && (shape[0] == 1U || shape[1] == 1U); break;
      case 15U: valid = shape.size() == 2U && shape[0] == 1U; break;
      case 16U: valid = shape.size() == 2U && shape[1] == 1U; break;
      case 17U: valid = shape.size() == 2U; break;
      case 18U:
        valid = argument_all(value, [](const auto& item) {
          using Item = std::decay_t<decltype(item)>;
          if constexpr (argument_is_complex<Item>::value)
            return !std::isnan(item.real()) && !std::isnan(item.imag());
          if constexpr (std::is_floating_point_v<Item>) return !std::isnan(item);
          return true;
        });
        break;
      case 19U:
        if constexpr (std::is_same_v<std::decay_t<T>, std::string>)
          valid = !value.empty();
        else
          valid = false;
        break;
      case 20U:
      case 21U: valid = std::is_same_v<std::decay_t<T>, std::string>; break;
      case 22U:
        if constexpr (std::is_same_v<std::decay_t<T>, std::string>) {
          valid = !value.empty() && value.size() <= 63U && !argument_is_matlab_keyword(value) &&
                  ((value.front() >= 'A' && value.front() <= 'Z') ||
                   (value.front() >= 'a' && value.front() <= 'z')) &&
                  std::all_of(value.begin() + 1, value.end(),
                              [](const unsigned char character) {
                                return (character >= 'A' && character <= 'Z') ||
                                       (character >= 'a' && character <= 'z') ||
                                       (character >= '0' && character <= '9') ||
                                       character == '_';
                              });
        } else {
          valid = false;
        }
        break;
      default: valid = false; break;
    }
    if (!valid) fail(argument_validator_name(validator));
  }
}
)mpf";
}

}  // namespace mpf::detail
