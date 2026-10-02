#include "function_dependencies.hpp"

#include <limits>
#include <optional>

#include "compiler/function_graph_generic.hpp"

namespace mpf::detail {

FunctionDependencyGraph analyze_function_dependencies(const hir::Program& program,
                                                      const NameTable& names) {
  constexpr auto missing = std::numeric_limits<std::size_t>::max();
  std::vector<std::size_t> declarations(names.symbols.size(), missing);
  for (std::size_t index = 0U; index < program.statements.size(); ++index) {
    const auto& statement = program.statements[index];
    if (statement.kind != StatementKind::function) continue;
    const auto* definition = names.use(statement.id, NameRole::declaration);
    if (definition != nullptr && definition->binding == BindingKind::function &&
        definition->symbol.valid() && definition->symbol.value() < declarations.size()) {
      declarations[definition->symbol.value()] = index;
    }
  }
  const auto resolve = [&](const hir::Expression& callee) -> std::optional<std::size_t> {
    const auto* use = names.reference(callee.id);
    if (use == nullptr || use->binding != BindingKind::function) return std::nullopt;
    if (!use->symbol.valid() || use->symbol.value() >= declarations.size()) {
      return std::nullopt;
    }
    const auto index = declarations[use->symbol.value()];
    return index == missing ? std::nullopt : std::optional<std::size_t>{index};
  };
  return build_function_dependency_graph_generic<hir::Expression, hir::Statement>(
      program.statements, resolve);
}

}  // namespace mpf::detail
