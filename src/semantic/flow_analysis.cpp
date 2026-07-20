#include "flow_analysis.hpp"

#include <limits>
#include <string>
#include <utility>

namespace mpf::detail {
namespace {

void add_error(std::vector<Diagnostic>& diagnostics, const SourceLocation location,
               const std::string_view stage, std::string message) {
  diagnostics.push_back(
      {DiagnosticSeverity::error, "MPF0005",
       "invalid HIR flow table at '" + std::string(stage) + "': " + std::move(message), location});
}

class FlowAnalyzer final {
 public:
  explicit FlowAnalyzer(const hir::Program& program) : program_(program) {
    result_.flow.hir_revision = program.revision;
    result_.flow.hir_node_count = program.node_count;
    result_.flow.nodes.resize(program.node_count + 1U);
    result_.flow.statements.reserve(program.node_count);
  }

  FlowAnalysisResult run() {
    analyze_statements(program_.statements, true);
    return std::move(result_);
  }

 private:
  FlowStatementFacts& register_statement(const hir::Statement& statement, const bool reachable) {
    const auto offset = result_.flow.statements.size();
    if (statement.id.valid() && statement.id.value() < result_.flow.nodes.size() &&
        offset <= std::numeric_limits<std::uint32_t>::max()) {
      result_.flow.nodes[statement.id.value()] = {FlowNodeKind::statement,
                                                  static_cast<std::uint32_t>(offset)};
    }
    result_.flow.statements.push_back(
        {statement.id, reachable, false, false, false, loop_depth_, function_depth_});
    return result_.flow.statements.back();
  }

  bool analyze_statements(const std::vector<hir::Statement>& statements,
                          const bool entry_reachable) {
    bool terminated = false;
    for (const auto& statement : statements) {
      const bool hoisted_matlab_function = program_.language == SourceLanguage::matlab &&
                                           function_depth_ == 0U &&
                                           statement.kind == StatementKind::function;
      const auto reachable = entry_reachable && (!terminated || hoisted_matlab_function);
      if (entry_reachable && terminated && !hoisted_matlab_function) {
        result_.diagnostics.push_back(
            {DiagnosticSeverity::warning,
             "MPF2101",
             "statement is unreachable because control flow already terminates",
             {statement.line, 1}});
      }
      const auto statement_terminates = analyze_statement(statement, reachable);
      terminated = terminated || statement_terminates;
    }
    return terminated;
  }

  bool analyze_statement(const hir::Statement& statement, const bool reachable) {
    auto& facts = register_statement(statement, reachable);
    switch (statement.kind) {
      case StatementKind::return_statement:
      case StatementKind::break_statement:
      case StatementKind::continue_statement: facts.terminates = true; break;
      case StatementKind::if_statement:
        facts.body_terminates = analyze_statements(statement.body, reachable);
        facts.alternative_terminates =
            !statement.alternative.empty() && analyze_statements(statement.alternative, reachable);
        facts.terminates = facts.body_terminates && facts.alternative_terminates;
        break;
      case StatementKind::try_statement:
        facts.body_terminates = analyze_statements(statement.body, reachable);
        facts.alternative_terminates = analyze_statements(statement.alternative, reachable);
        facts.terminates = facts.body_terminates && facts.alternative_terminates;
        break;
      case StatementKind::select_case: analyze_select(statement, reachable, facts); break;
      case StatementKind::case_clause:
        facts.body_terminates = analyze_statements(statement.body, reachable);
        facts.alternative_terminates =
            !statement.alternative.empty() && analyze_statements(statement.alternative, reachable);
        facts.terminates = facts.body_terminates &&
                           (statement.alternative.empty() || facts.alternative_terminates);
        break;
      case StatementKind::while_loop:
      case StatementKind::range_loop:
      case StatementKind::for_loop:
        ++loop_depth_;
        facts.body_terminates = analyze_statements(statement.body, reachable);
        --loop_depth_;
        facts.alternative_terminates =
            !statement.alternative.empty() && analyze_statements(statement.alternative, reachable);
        break;
      case StatementKind::function: {
        ++function_depth_;
        const auto saved_loop_depth = loop_depth_;
        loop_depth_ = 0;
        // A function body has its own entry point even when its definition is unreachable.
        facts.body_terminates = analyze_statements(statement.body, true);
        loop_depth_ = saved_loop_depth;
        --function_depth_;
        break;
      }
      case StatementKind::declaration:
      case StatementKind::assignment:
      case StatementKind::multi_assignment:
      case StatementKind::indexed_assignment:
      case StatementKind::print:
      case StatementKind::expression: break;
    }
    return facts.terminates;
  }

  void analyze_select(const hir::Statement& statement, const bool reachable,
                      FlowStatementFacts& facts) {
    bool all_terminate = !statement.body.empty();
    bool has_default = false;
    for (const auto& clause : statement.body) {
      const auto terminates = analyze_statement(clause, reachable);
      all_terminate = all_terminate && terminates;
      has_default =
          has_default || (clause.kind == StatementKind::case_clause && clause.default_case);
    }
    facts.body_terminates = all_terminate;
    facts.alternative_terminates =
        !statement.alternative.empty() && analyze_statements(statement.alternative, reachable);
    facts.terminates = has_default && all_terminate;
  }

  const hir::Program& program_;
  FlowAnalysisResult result_;
  std::size_t loop_depth_{0};
  std::size_t function_depth_{0};
};

void verify_statements(const std::vector<hir::Statement>& statements, const FlowTable& flow,
                       std::vector<bool>& seen, const std::string_view stage,
                       std::vector<Diagnostic>& diagnostics) {
  for (const auto& statement : statements) {
    const auto* facts = flow.statement(statement.id);
    if (facts == nullptr || facts->origin != statement.id) {
      add_error(diagnostics, {statement.line, 1}, stage,
                "statement fact is missing or has the wrong origin");
    } else {
      seen[statement.id.value()] = true;
    }
    verify_statements(statement.body, flow, seen, stage, diagnostics);
    verify_statements(statement.alternative, flow, seen, stage, diagnostics);
  }
}

}  // namespace

const FlowStatementFacts* FlowTable::statement(const HirNodeId id) const noexcept {
  if (!id.valid() || id.value() >= nodes.size()) return nullptr;
  const auto slot = nodes[id.value()];
  if (slot.kind != FlowNodeKind::statement ||
      static_cast<std::size_t>(slot.offset) >= statements.size()) {
    return nullptr;
  }
  return &statements[slot.offset];
}

FlowAnalysisResult analyze_flow(const hir::Program& program) {
  auto result = FlowAnalyzer(program).run();
  auto verifier_diagnostics = verify_flow(program, result.flow, "flow-analysis");
  result.diagnostics.insert(result.diagnostics.end(),
                            std::make_move_iterator(verifier_diagnostics.begin()),
                            std::make_move_iterator(verifier_diagnostics.end()));
  return result;
}

std::vector<Diagnostic> verify_flow(const hir::Program& program, const FlowTable& flow,
                                    const std::string_view stage) {
  std::vector<Diagnostic> diagnostics;
  if (flow.hir_revision != program.revision) {
    add_error(diagnostics, {1, 1}, stage, "HIR revision is stale");
  }
  if (flow.hir_node_count != program.node_count || flow.nodes.size() != program.node_count + 1U) {
    add_error(diagnostics, {1, 1}, stage, "dense node index does not cover the HIR ID space");
    return diagnostics;
  }
  std::vector<bool> seen(flow.nodes.size(), false);
  verify_statements(program.statements, flow, seen, stage, diagnostics);
  std::size_t statement_slots = 0;
  for (std::size_t id = 1; id < flow.nodes.size(); ++id) {
    if (flow.nodes[id].kind != FlowNodeKind::statement) continue;
    ++statement_slots;
    if (!seen[id]) {
      add_error(diagnostics, {1, 1}, stage, "table contains a fact for a non-resident statement");
    }
  }
  if (statement_slots != flow.statements.size()) {
    add_error(diagnostics, {1, 1}, stage, "statement slot and fact counts disagree");
  }
  return diagnostics;
}

}  // namespace mpf::detail
