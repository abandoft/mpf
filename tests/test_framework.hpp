#pragma once

#include <functional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace test {

struct Case {
  std::string name;
  std::function<void()> function;
};

inline std::vector<Case>& registry() {
  static std::vector<Case> cases;
  return cases;
}

class Registration final {
 public:
  Registration(std::string name, std::function<void()> function) {
    registry().push_back({std::move(name), std::move(function)});
  }
};

inline void require(const bool condition, const char* expression, const char* file,
                    const int line) {
  if (!condition) {
    std::ostringstream message;
    message << file << ':' << line << ": requirement failed: " << expression;
    throw std::runtime_error(message.str());
  }
}

}  // namespace test

#define MPF_TEST_JOIN_INNER(a, b) a##b
#define MPF_TEST_JOIN(a, b) MPF_TEST_JOIN_INNER(a, b)
#define TEST_CASE(name)                                                        \
  static void MPF_TEST_JOIN(test_function_, __LINE__)();                       \
  static const test::Registration MPF_TEST_JOIN(test_registration_, __LINE__)( \
      name, MPF_TEST_JOIN(test_function_, __LINE__));                          \
  static void MPF_TEST_JOIN(test_function_, __LINE__)()
#define REQUIRE(expression) test::require((expression), #expression, __FILE__, __LINE__)
