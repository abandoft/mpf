#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <string>
#include <utility>
#include <vector>

#include "backends/cpp/function_dependencies.hpp"
#include "backends/cpp/lir_planning.hpp"
#include "compiler/function_graph.hpp"
#include "frontends/common/registry.hpp"
#include "mpf/transpiler.hpp"
#include "semantic/function_dependencies.hpp"
#include "test_framework.hpp"

namespace {

using mpf::detail::FunctionDependencyGraph;

FunctionDependencyGraph source_dependencies(const mpf::SourceLanguage language,
                                            const std::string& source) {
  const auto* frontend = mpf::detail::find_frontend(language);
  REQUIRE(frontend != nullptr);
  auto parsed =
      mpf::detail::parse_with_frontend(*frontend, mpf::detail::SourceText(source, "graph"));
  REQUIRE(parsed.diagnostics.empty());
  auto lowered = frontend->lower(std::move(parsed.ast));
  REQUIRE(lowered.diagnostics.empty());
  const auto names = mpf::detail::analyze_names(lowered.program);
  REQUIRE(names.diagnostics.empty());
  return mpf::detail::analyze_function_dependencies(lowered.program, names.names);
}

void complete(FunctionDependencyGraph& graph) {
  std::vector<std::size_t> functions(graph.dependencies.size());
  std::iota(functions.begin(), functions.end(), 0U);
  mpf::detail::analyze_function_dependencies(graph, functions);
}

bool reference_recursive(const FunctionDependencyGraph& graph, const std::size_t start) {
  std::vector<bool> seen(graph.dependencies.size(), false);
  std::vector<std::size_t> pending{start};
  while (!pending.empty()) {
    const auto current = pending.back();
    pending.pop_back();
    if (seen[current]) continue;
    seen[current] = true;
    for (const auto next : graph.dependencies[current]) {
      if (next == start) return true;
      pending.push_back(next);
    }
  }
  return false;
}

mpf::detail::cpp::lir::Expression target_call(const mpf::detail::SymbolId symbol,
                                              const mpf::detail::BindingKind binding,
                                              const std::string& spelling) {
  mpf::detail::cpp::lir::Expression call;
  call.kind = mpf::detail::ExpressionKind::call;
  mpf::detail::cpp::lir::Expression callee;
  callee.kind = mpf::detail::ExpressionKind::identifier;
  callee.symbol_id = symbol;
  callee.binding = binding;
  callee.value = spelling;
  call.children.push_back(std::move(callee));
  return call;
}

}  // namespace

TEST_CASE("Matlab default calls participate in resolved callee-first dependency order") {
  const auto graph = source_dependencies(mpf::SourceLanguage::matlab,
                                         "function output = checked(value)\n"
                                         "arguments\nvalue (1,1) double = seed()\nend\n"
                                         "output = value\nend\n"
                                         "function output = seed()\noutput = leaf()\nend\n"
                                         "function output = leaf()\noutput = 42\nend\n");
  REQUIRE((graph.dependencies == std::vector<std::vector<std::size_t>>{{1U}, {2U}, {}}));
  REQUIRE((graph.definition_order == std::vector<std::size_t>{2U, 1U, 0U}));
  REQUIRE((graph.recursive == std::vector<bool>{false, false, false}));
}

TEST_CASE("default dependency binding follows Python definition scope and Matlab formal scope") {
  const auto python = source_dependencies(mpf::SourceLanguage::python,
                                          "def checked(seed=seed()):\n    return seed\n"
                                          "def seed():\n    return 42\n");
  REQUIRE((python.dependencies[0] == std::vector<std::size_t>{1U}));
  const auto matlab = source_dependencies(mpf::SourceLanguage::matlab,
                                          "function output = checked(seed,value)\narguments\n"
                                          "seed (1,1) double = 1\n"
                                          "value (1,1) double = seed()\nend\n"
                                          "output = value\nend\n"
                                          "function output = seed()\noutput = 42\nend\n");
  REQUIRE(matlab.dependencies[0].empty());
}

TEST_CASE("cpp dependency reconstruction uses function SymbolId instead of equal spellings") {
  namespace cpp = mpf::detail::cpp;
  using mpf::detail::BindingKind;
  using mpf::detail::StatementKind;
  using mpf::detail::SymbolId;
  std::vector<cpp::lir::Statement> statements(3U);
  for (std::size_t index = 0U; index < statements.size(); ++index) {
    statements[index].kind = StatementKind::function;
    statements[index].name = "same_spelling";
    statements[index].symbol_id = SymbolId{static_cast<SymbolId::value_type>(index + 1U)};
  }
  statements[0].parameter_defaults.push_back(
      target_call(SymbolId{3U}, BindingKind::function, "unrelated_mangled_name"));
  cpp::lir::Statement body;
  body.kind = StatementKind::expression;
  body.has_expression = true;
  body.expression = target_call(SymbolId{2U}, BindingKind::variable, "same_spelling");
  statements[0].body.push_back(std::move(body));
  const auto graph = cpp::analyze_function_dependencies(statements);
  REQUIRE((graph.dependencies[0] == std::vector<std::size_t>{2U}));
  REQUIRE((graph.definition_order == std::vector<std::size_t>{2U, 0U, 1U}));
}

TEST_CASE("iterative dependency analysis handles long chains and large strongly connected sets") {
  constexpr auto count = 50000U;
  FunctionDependencyGraph graph;
  graph.dependencies.resize(count);
  for (std::size_t index = 0U; index + 1U < count; ++index) {
    graph.dependencies[index].push_back(index + 1U);
  }
  complete(graph);
  REQUIRE(graph.definition_order.size() == count);
  REQUIRE(graph.definition_order.front() == count - 1U);
  REQUIRE(graph.definition_order.back() == 0U);
  REQUIRE(
      std::none_of(graph.recursive.begin(), graph.recursive.end(), [](const bool v) { return v; }));
  graph.dependencies.back().push_back(0U);
  complete(graph);
  REQUIRE(
      std::all_of(graph.recursive.begin(), graph.recursive.end(), [](const bool v) { return v; }));
  graph.dependencies.back().clear();
  complete(graph);
  REQUIRE(
      std::none_of(graph.recursive.begin(), graph.recursive.end(), [](const bool v) { return v; }));
}

TEST_CASE("SCC recursion agrees with independent reachability over varied directed graphs") {
  std::uint32_t state = 0x3918a72fU;
  for (std::size_t trial = 0U; trial < 256U; ++trial) {
    FunctionDependencyGraph graph;
    graph.dependencies.resize(12U);
    for (std::size_t source = 0U; source < graph.dependencies.size(); ++source) {
      for (std::size_t target = 0U; target < graph.dependencies.size(); ++target) {
        state = state * 1664525U + 1013904223U;
        if ((state >> 24U) < 32U) graph.dependencies[source].push_back(target);
      }
    }
    complete(graph);
    auto inventory = graph.definition_order;
    std::sort(inventory.begin(), inventory.end());
    for (std::size_t index = 0U; index < graph.dependencies.size(); ++index) {
      REQUIRE(inventory[index] == index);
      REQUIRE(graph.recursive[index] == reference_recursive(graph, index));
    }
  }
}

TEST_CASE("cpp resource verification rejects damaged dependency facts before graph indexing") {
  namespace cpp = mpf::detail::cpp;
  cpp::lir::SemanticProgram program;
  cpp::lir::Statement statement;
  statement.kind = mpf::detail::StatementKind::function;
  program.statements.push_back(std::move(statement));
  program.function_graph = cpp::analyze_function_dependencies(program.statements);
  program.function_graph.definition_order.front() = 1000000U;
  std::vector<mpf::Diagnostic> diagnostics;
  cpp::verify_lir_resources(program, diagnostics);
  REQUIRE(!diagnostics.empty());
  REQUIRE(diagnostics.front().message.find("dependency graph") != std::string::npos);
}

TEST_CASE("Matlab validated defaults calling forward local functions lower in both targets") {
  constexpr auto source =
      "function output = checked(value)\narguments\n"
      "value (1,1) double {mustBePositive} = seed()\nend\n"
      "output = value\nend\n"
      "function output = seed()\noutput = leaf()\nend\n"
      "function output = leaf()\noutput = 42\nend\ndisp(checked())\n";
  for (const auto target : {mpf::TargetLanguage::javascript, mpf::TargetLanguage::cpp}) {
    mpf::TranspileOptions options;
    options.language = mpf::SourceLanguage::matlab;
    options.target = target;
    options.emit_source_banner = false;
    const auto result = mpf::Transpiler{}.transpile(source, options);
    REQUIRE(result.success());
    if (target == mpf::TargetLanguage::cpp) {
      REQUIRE(result.code.find("auto leaf(") < result.code.find("auto seed("));
      REQUIRE(result.code.find("auto seed(") < result.code.find("auto checked("));
    }
  }
}
