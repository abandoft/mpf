#include <algorithm>
#include <string>
#include <utility>

#include "frontends/common/registry.hpp"
#include "semantic/name_analysis.hpp"
#include "test_framework.hpp"

namespace {

mpf::detail::hir::LoweringResult lower(const std::string& source) {
  const auto& frontend = mpf::detail::python_frontend();
  auto parsed =
      mpf::detail::parse_with_frontend(frontend, mpf::detail::SourceText(source, "names.py"));
  REQUIRE(parsed.diagnostics.empty());
  auto result = frontend.lower(std::move(parsed.ast));
  REQUIRE(result.diagnostics.empty());
  return result;
}

mpf::detail::NameUse& reference(mpf::detail::NameTable& names,
                                const mpf::detail::HirNodeId origin) {
  const auto found = std::find_if(names.uses.begin(), names.uses.end(), [&](const auto& use) {
    return use.origin == origin && use.role == mpf::detail::NameRole::reference;
  });
  REQUIRE(found != names.uses.end());
  return *found;
}

constexpr auto source =
    "def first(value):\n    return second(value)\n"
    "def second(value):\n    return value\n"
    "def third(value):\n    return value\n";

}  // namespace

TEST_CASE("name verifier rejects cross-function rebinding despite valid ancestor symbol identity") {
  auto lowered = lower(source);
  auto analysis = mpf::detail::analyze_names(lowered.program);
  REQUIRE(analysis.diagnostics.empty());
  const auto& callee = lowered.program.statements.front().body.front().expression.children.front();
  REQUIRE(callee.value == "second");
  const auto* third =
      analysis.names.use(lowered.program.statements.back().id, mpf::detail::NameRole::declaration);
  REQUIRE(third != nullptr);
  reference(analysis.names, callee.id).symbol = third->symbol;
  REQUIRE(!mpf::detail::verify_names(lowered.program, analysis.names, "cross-function").empty());
}

TEST_CASE("name verifier rejects bypassing nearest formal shadowing with an ancestor function") {
  auto lowered = lower(
      "def first(second):\n    return second(1)\n"
      "def second(value):\n    return value\n");
  auto analysis = mpf::detail::analyze_names(lowered.program);
  REQUIRE(analysis.diagnostics.empty());
  const auto& callee = lowered.program.statements.front().body.front().expression.children.front();
  const auto* global =
      analysis.names.use(lowered.program.statements.back().id, mpf::detail::NameRole::declaration);
  REQUIRE(global != nullptr);
  auto& use = reference(analysis.names, callee.id);
  REQUIRE(use.binding == mpf::detail::BindingKind::variable);
  use.symbol = global->symbol;
  use.binding = mpf::detail::BindingKind::function;
  REQUIRE(!mpf::detail::verify_names(lowered.program, analysis.names, "shadowing").empty());
}

TEST_CASE("name verifier checks builtin spelling instead of trusting a valid intrinsic ordinal") {
  auto lowered = lower("def first(value):\n    return abs(value)\n");
  auto analysis = mpf::detail::analyze_names(lowered.program);
  REQUIRE(analysis.diagnostics.empty());
  const auto& callee = lowered.program.statements.front().body.front().expression.children.front();
  reference(analysis.names, callee.id).intrinsic = mpf::detail::IntrinsicId::square_root;
  REQUIRE(!mpf::detail::verify_names(lowered.program, analysis.names, "builtin-spelling").empty());
  auto& use = reference(analysis.names, callee.id);
  use.binding = mpf::detail::BindingKind::unresolved;
  use.intrinsic = mpf::detail::IntrinsicId::none;
  REQUIRE(!mpf::detail::verify_names(lowered.program, analysis.names, "hidden-builtin").empty());
}

TEST_CASE("name verifier safely rejects foreign symbols nodes and cyclic scope parents") {
  auto lowered = lower(source);
  const auto analysis = mpf::detail::analyze_names(lowered.program);
  REQUIRE(analysis.diagnostics.empty());
  auto& callee = lowered.program.statements.front().body.front().expression.children.front();
  auto foreign_symbol = analysis.names;
  reference(foreign_symbol, callee.id).symbol = mpf::detail::SymbolId{1000000U};
  REQUIRE(!mpf::detail::verify_names(lowered.program, foreign_symbol, "foreign-symbol").empty());
  auto cyclic = analysis.names;
  const auto scope = cyclic.function_scope(lowered.program.statements.front().id);
  cyclic.scopes[scope.value()].parent = scope;
  REQUIRE(!mpf::detail::verify_names(lowered.program, cyclic, "cyclic-parent").empty());
  auto root_cycle = analysis.names;
  root_cycle.scopes[root_cycle.global_scope.value()].parent = scope;
  REQUIRE(!mpf::detail::verify_names(lowered.program, root_cycle, "root-parent").empty());
  callee.id = mpf::detail::HirNodeId{1000000U};
  REQUIRE(!mpf::detail::verify_names(lowered.program, analysis.names, "foreign-node").empty());
}
