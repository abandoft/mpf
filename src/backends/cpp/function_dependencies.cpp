#include "function_dependencies.hpp"

#include <algorithm>
#include <optional>
#include <unordered_map>

#include "compiler/function_graph_generic.hpp"

namespace mpf::detail::cpp {

FunctionDependencyGraph analyze_function_dependencies(
    const std::vector<lir::Statement>& statements) {
  std::unordered_map<SymbolId::value_type, std::size_t> definitions;
  const auto count = static_cast<std::size_t>(std::count_if(
      statements.begin(), statements.end(),
      [](const auto& statement) { return statement.kind == StatementKind::function; }));
  definitions.reserve(count);
  for (std::size_t index = 0U; index < statements.size(); ++index) {
    const auto& statement = statements[index];
    if (statement.kind == StatementKind::function && statement.symbol_id.valid()) {
      definitions.insert_or_assign(statement.symbol_id.value(), index);
    }
  }
  const auto resolve = [&](const lir::Expression& callee) -> std::optional<std::size_t> {
    if (callee.binding != BindingKind::function || !callee.symbol_id.valid()) return std::nullopt;
    const auto found = definitions.find(callee.symbol_id.value());
    return found == definitions.end() ? std::nullopt : std::optional<std::size_t>{found->second};
  };
  return build_function_dependency_graph_generic<lir::Expression, lir::Statement>(statements,
                                                                                  resolve);
}

}  // namespace mpf::detail::cpp
