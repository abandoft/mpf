#include <exception>
#include <iostream>

#include "test_framework.hpp"

int main() {
  int failures = 0;
  for (const auto& test_case : test::registry()) {
    try {
      test_case.function();
      std::cout << "[pass] " << test_case.name << '\n';
    } catch (const std::exception& exception) {
      ++failures;
      std::cerr << "[fail] " << test_case.name << ": " << exception.what() << '\n';
    } catch (...) {
      ++failures;
      std::cerr << "[fail] " << test_case.name << ": unknown exception\n";
    }
  }
  std::cout << test::registry().size() << " test(s), " << failures << " failure(s)\n";
  return failures == 0 ? 0 : 1;
}
