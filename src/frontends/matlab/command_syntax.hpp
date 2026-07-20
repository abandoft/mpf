#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mpf::detail {

enum class MatlabCommandArgumentForm { unquoted, single_quoted };

struct MatlabCommandArgument {
  std::string value;
  std::size_t begin{0};
  std::size_t end{0};
  MatlabCommandArgumentForm form{MatlabCommandArgumentForm::unquoted};
};

struct MatlabCommandSyntax {
  std::string callee;
  std::size_t callee_begin{0};
  std::size_t callee_end{0};
  std::vector<MatlabCommandArgument> arguments;
  bool terminated{true};
};

// Recognizes Matlab command syntax from one already-normalized logical statement. The scanner
// preserves the source spans needed by the statement lexer, strips only grouping single quotes,
// and deliberately leaves double quotes in the command text value.
[[nodiscard]] std::optional<MatlabCommandSyntax> scan_matlab_command_syntax(
    std::string_view source);

}  // namespace mpf::detail
