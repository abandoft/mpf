#include "compiler/argument_validation.hpp"

#include <locale.h>

#include <array>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <system_error>

#if defined(__APPLE__)
#include <xlocale.h>
#endif

namespace mpf::detail {
namespace {

// Older macOS standard libraries lack floating from_chars. A process-local numeric locale
// gives correctly rounded C conversion without changing global/thread locale or rejecting
// representable subnormals merely because the C library sets ERANGE.
class NumericLocale {
 public:
  NumericLocale() = default;
  NumericLocale(const NumericLocale&) = delete;
  NumericLocale& operator=(const NumericLocale&) = delete;

  ~NumericLocale() {
    if (!valid()) return;
#if defined(_WIN32)
    _free_locale(value_);
#else
    freelocale(value_);
#endif
  }

  [[nodiscard]] bool valid() const noexcept { return value_ != nullptr; }

  [[nodiscard]] double parse(const char* token, char** end) const {
#if defined(_WIN32)
    return _strtod_l(token, end, value_);
#else
    return strtod_l(token, end, value_);
#endif
  }

 private:
#if defined(_WIN32)
  _locale_t value_{_create_locale(LC_NUMERIC, "C")};
#else
  locale_t value_{newlocale(LC_NUMERIC_MASK, "C", nullptr)};
#endif
};

}  // namespace

std::optional<std::string> normalize_argument_numeric_literal(const std::string_view value) {
  if (!valid_argument_numeric_literal(value)) return std::nullopt;
  static const NumericLocale locale;
  if (!locale.valid()) return std::nullopt;
  const std::string token(value);
  char* end = nullptr;
  const double number = locale.parse(token.c_str(), &end);
  if (end != token.data() + token.size() || !std::isfinite(number)) return std::nullopt;

  std::array<char, 128> output{};
  const auto formatted =
      std::to_chars(output.data(), output.data() + output.size(), number,
                    std::chars_format::general, std::numeric_limits<double>::max_digits10);
  if (formatted.ec != std::errc{}) return std::nullopt;
  std::string normalized(output.data(), formatted.ptr);
  if (normalized.find_first_of(".eE") == std::string::npos) normalized += ".0";
  return normalized;
}

}  // namespace mpf::detail
