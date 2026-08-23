#include "exception_runtime.hpp"

#include <ostream>

namespace mpf::detail {

void emit_cpp_exception_runtime(std::ostream& output) {
  output << R"MPFCPP(struct matlab_stack_frame {
  std::string file;
  std::string name;
  std::size_t line{0};
};

inline bool matlab_valid_exception_identifier(const std::string& value) {
  if (value.empty()) return false;
  bool field_start = true;
  bool separator = false;
  for (const char source_character : value) {
    const auto character = static_cast<unsigned char>(source_character);
    if (character == ':') {
      if (field_start) return false;
      field_start = true;
      separator = true;
      continue;
    }
    const bool letter = (character >= 'A' && character <= 'Z') ||
                        (character >= 'a' && character <= 'z');
    const bool digit = character >= '0' && character <= '9';
    if ((field_start && !letter) || (!field_start && !letter && !digit && character != '_')) {
      return false;
    }
    field_start = false;
  }
  return separator && !field_start;
}

class matlab_exception final : public std::exception {
 public:
  matlab_exception() = default;
  matlab_exception(std::string identifier, std::string message,
                   std::vector<matlab_exception> causes = {},
                   std::vector<matlab_stack_frame> stack = {},
                   std::exception_ptr original = {})
      : identifier_(std::move(identifier)), message_(std::move(message)),
        causes_(std::move(causes)), stack_(std::move(stack)), original_(std::move(original)) {
    if (!matlab_valid_exception_identifier(identifier_)) {
      throw std::invalid_argument("MPF Matlab exception identifier is invalid");
    }
  }
  const std::string& identifier() const noexcept { return identifier_; }
  const std::string& message() const noexcept { return message_; }
  const std::vector<matlab_exception>& cause() const noexcept { return causes_; }
  const std::vector<matlab_stack_frame>& stack() const noexcept { return stack_; }
  const char* what() const noexcept override { return message_.c_str(); }
  matlab_exception with_cause(const matlab_exception& cause_value) const {
    auto result = *this;
    result.causes_.push_back(cause_value);
    return result;
  }
  matlab_exception with_original(std::exception_ptr original) const {
    auto result = *this;
    result.original_ = std::move(original);
    return result;
  }
  [[noreturn]] void rethrow() const {
    if (original_) std::rethrow_exception(original_);
    throw *this;
  }
 private:
  std::string identifier_;
  std::string message_;
  std::vector<matlab_exception> causes_;
  std::vector<matlab_stack_frame> stack_;
  std::exception_ptr original_;
};

struct matlab_format_argument {
  bool textual{false};
  std::string text;
  long double number{0};
};

inline matlab_format_argument matlab_make_format_argument(const std::string& value) {
  return {true, value, 0};
}
inline matlab_format_argument matlab_make_format_argument(const char* value) {
  return {true, value == nullptr ? std::string{} : std::string{value}, 0};
}
template <std::size_t Size>
matlab_format_argument matlab_make_format_argument(const char (&value)[Size]) {
  return {true, std::string{value}, 0};
}
inline matlab_format_argument matlab_make_format_argument(const bool value) {
  return {false, {}, value ? 1.0L : 0.0L};
}
template <typename Value,
          typename std::enable_if<std::is_arithmetic<Value>::value &&
                                      !std::is_same<typename std::decay<Value>::type, bool>::value,
                                  int>::type = 0>
matlab_format_argument matlab_make_format_argument(const Value value) {
  return {false, {}, static_cast<long double>(value)};
}

inline std::string matlab_decode_format_escapes(const std::string& value) {
  std::string result;
  result.reserve(value.size());
  for (std::size_t index = 0; index < value.size(); ++index) {
    if (value[index] != '\\' || index + 1U >= value.size()) {
      result.push_back(value[index]);
      continue;
    }
    const auto escape = value[++index];
    if (escape == 'n') result.push_back('\n');
    else if (escape == 'r') result.push_back('\r');
    else if (escape == 't') result.push_back('\t');
    else if (escape == 'b') result.push_back('\b');
    else if (escape == 'f') result.push_back('\f');
    else if (escape == '\\') result.push_back('\\');
    else { result.push_back('\\'); result.push_back(escape); }
  }
  return result;
}

inline std::string matlab_format_exception_message(
    const std::string& format, const std::vector<matlab_format_argument>& values) {
  std::string result;
  std::size_t argument = 0;
  for (std::size_t index = 0; index < format.size();) {
    if (format[index] != '%') { result.push_back(format[index++]); continue; }
    if (index + 1U < format.size() && format[index + 1U] == '%') {
      result.push_back('%'); index += 2U; continue;
    }
    ++index;
    bool left = false;
    bool show_sign = false;
    bool space_sign = false;
    bool zero = false;
    bool alternate = false;
    while (index < format.size()) {
      if (format[index] == '-') left = true;
      else if (format[index] == '+') show_sign = true;
      else if (format[index] == ' ') space_sign = true;
      else if (format[index] == '0') zero = true;
      else if (format[index] == '#') alternate = true;
      else break;
      ++index;
    }
    std::size_t width = 0;
    while (index < format.size() && format[index] >= '0' && format[index] <= '9') {
      if (width > (65536U - static_cast<std::size_t>(format[index] - '0')) / 10U) {
        throw std::length_error("MPF Matlab format width exceeds the runtime limit");
      }
      width = width * 10U + static_cast<std::size_t>(format[index++] - '0');
    }
    int precision = -1;
    if (index < format.size() && format[index] == '.') {
      ++index; precision = 0;
      while (index < format.size() && format[index] >= '0' && format[index] <= '9') {
        if (precision > (65536 - static_cast<int>(format[index] - '0')) / 10) {
          throw std::length_error("MPF Matlab format precision exceeds the runtime limit");
        }
        precision = precision * 10 + static_cast<int>(format[index++] - '0');
      }
    }
    if (index >= format.size()) throw std::invalid_argument("MPF Matlab format is incomplete");
    const auto conversion = format[index++];
    const std::string supported{"diuoxXfFeEgGcs"};
    if (supported.find(conversion) == std::string::npos) {
      throw std::invalid_argument("MPF Matlab format specifier is unsupported");
    }
    if (argument >= values.size()) {
      throw std::invalid_argument("MPF Matlab format argument is missing");
    }
    const auto& value = values[argument++];
    std::ostringstream stream;
    if (left) stream << std::left;
    if (show_sign || space_sign) stream << std::showpos;
    if (alternate) stream << std::showbase << std::showpoint;
    if (zero && !left) stream << std::internal << std::setfill('0');
    if (width != 0U) stream << std::setw(static_cast<int>(width));
    if (conversion == 's') {
      if (!value.textual) {
        throw std::invalid_argument("MPF Matlab string format requires text");
      }
      const auto& text = value.text;
      stream << (precision < 0 ? text : text.substr(0, static_cast<std::size_t>(precision)));
    } else if (conversion == 'c') {
      if (value.textual) {
        if (value.text.empty()) {
          throw std::invalid_argument("MPF Matlab character format requires non-empty text");
        }
        stream << value.text.front();
      } else {
        if (!std::isfinite(value.number) || std::trunc(value.number) != value.number ||
            value.number < 0.0L || value.number > 255.0L) {
          throw std::invalid_argument(
              "MPF Matlab character format requires text or an unsigned byte");
        }
        stream << static_cast<char>(static_cast<unsigned char>(value.number));
      }
    } else {
      if (value.textual) throw std::invalid_argument("MPF Matlab numeric format requires a number");
      const bool integral = conversion == 'd' || conversion == 'i' || conversion == 'u' ||
                            conversion == 'o' || conversion == 'x' || conversion == 'X';
      if (integral &&
          (!std::isfinite(value.number) || std::trunc(value.number) < -9007199254740991.0L ||
           std::trunc(value.number) > 9007199254740991.0L ||
           ((conversion == 'u' || conversion == 'o' || conversion == 'x' || conversion == 'X') &&
            value.number < 0.0L))) {
        throw std::range_error("MPF Matlab integer format requires a representable scalar");
      }
      if (conversion == 'd' || conversion == 'i') stream << static_cast<long long>(value.number);
      else if (conversion == 'u') stream << static_cast<unsigned long long>(value.number);
      else if (conversion == 'o') stream << std::oct << static_cast<unsigned long long>(value.number);
      else if (conversion == 'x' || conversion == 'X') {
        if (conversion == 'X') stream << std::uppercase;
        stream << std::hex << static_cast<unsigned long long>(value.number);
      } else if (!std::isfinite(value.number)) {
        if (std::isnan(value.number)) stream << "NaN";
        else {
          if (value.number > 0.0L && (show_sign || space_sign)) stream << '+';
          stream << (value.number < 0.0L ? "-Inf" : "Inf");
        }
      } else {
        stream << std::setprecision(precision < 0 ? 6 : precision);
        if (conversion == 'f' || conversion == 'F') stream << std::fixed;
        else if (conversion == 'e' || conversion == 'E') stream << std::scientific;
        if (conversion == 'E' || conversion == 'G' || conversion == 'F') stream << std::uppercase;
        stream << static_cast<double>(value.number);
      }
    }
    auto rendered = stream.str();
    if (space_sign && !show_sign && conversion != 's' && conversion != 'c' &&
        value.number >= 0.0L) {
      const auto sign = rendered.find('+');
      if (sign != std::string::npos) rendered[sign] = ' ';
    }
    result += rendered;
  }
  if (argument != values.size()) {
    throw std::invalid_argument("MPF Matlab format has unused arguments");
  }
  return matlab_decode_format_escapes(result);
}

template <typename... Values>
std::vector<matlab_format_argument> matlab_format_arguments(Values&&... values) {
  return {matlab_make_format_argument(std::forward<Values>(values))...};
}

template <typename... Values>
matlab_exception matlab_make_exception(const std::string& identifier,
                                       const std::string& message, Values&&... values) {
  const auto arguments = matlab_format_arguments(std::forward<Values>(values)...);
  const auto formatted = matlab_format_exception_message(message, arguments);
  return matlab_exception{identifier, formatted};
}

inline matlab_exception capture_exception(std::exception_ptr original) {
  try {
    if (original) std::rethrow_exception(original);
  } catch (const matlab_exception& error) {
    return error.with_original(std::move(original));
  } catch (const std::exception& error) {
    return matlab_exception{"MPF:RuntimeError", error.what(), {}, {}, std::move(original)};
  } catch (...) {
    return matlab_exception{"MPF:UnknownException", "unknown non-standard exception", {}, {},
                            std::move(original)};
  }
  return matlab_exception{"MPF:RuntimeError", "empty exception", {}, {}, std::move(original)};
}

template <typename... Values>
[[noreturn]] void matlab_error(const std::string& first, Values&&... values) {
  auto arguments = matlab_format_arguments(std::forward<Values>(values)...);
  if (arguments.empty()) throw matlab_exception{"MATLAB:error", first};
  if (matlab_valid_exception_identifier(first) && arguments.front().textual) {
    const auto message = arguments.front().text;
    arguments.erase(arguments.begin());
    throw matlab_exception{first, matlab_format_exception_message(message, arguments)};
  }
  throw matlab_exception{"MATLAB:error", matlab_format_exception_message(first, arguments)};
}

[[noreturn]] inline void matlab_throw(const matlab_exception& exception) { throw exception; }
[[noreturn]] inline void matlab_throw_as_caller(const matlab_exception& exception) {
  throw exception;
}
[[noreturn]] inline void matlab_rethrow(const matlab_exception& exception) {
  exception.rethrow();
}
inline matlab_exception matlab_add_cause(const matlab_exception& base,
                                         const matlab_exception& cause) {
  return base.with_cause(cause);
}
inline void matlab_append_exception_report(std::ostringstream& output,
                                           const matlab_exception& exception,
                                           const std::size_t indent) {
  output << std::string(indent, ' ') << exception.message();
  for (const auto& cause : exception.cause()) {
    output << '\n' << std::string(indent, ' ') << "Caused by:\n";
    matlab_append_exception_report(output, cause, indent + 2U);
  }
  for (const auto& frame : exception.stack()) {
    output << '\n' << std::string(indent, ' ') << frame.name << " (" << frame.file << ':'
           << frame.line << ')';
  }
}
inline std::string matlab_get_report(const matlab_exception& exception,
                                     const std::string& detail = "extended",
                                     const std::string& option = {},
                                     const std::string& hyperlink = {}) {
  if (detail != "basic" && detail != "extended") {
    throw std::invalid_argument("MPF Matlab report detail must be basic or extended");
  }
  if ((!option.empty() || !hyperlink.empty()) &&
      (option != "hyperlinks" ||
       (hyperlink != "default" && hyperlink != "on" && hyperlink != "off"))) {
    throw std::invalid_argument("MPF Matlab getReport hyperlink option is invalid");
  }
  if (detail == "basic") return exception.message();
  std::ostringstream output;
  matlab_append_exception_report(output, exception, 0U);
  return output.str();
}

)MPFCPP";
}

}  // namespace mpf::detail
