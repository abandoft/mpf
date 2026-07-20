#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "compiler/expression_ast.hpp"
#include "compiler/statement_kind.hpp"
#include "ids.hpp"
#include "semantics.hpp"

namespace mpf::detail::hir {

struct Expression {
  HirNodeId id{};
  SourceLocation location{};
  ExpressionKind kind{ExpressionKind::invalid};
  std::string value;
  UnaryOperator unary_operation{UnaryOperator::none};
  BinaryOperator operation{BinaryOperator::none};
  ComparisonOperator comparison{ComparisonOperator::none};
  std::vector<ComparisonOperator> comparisons;
  std::vector<Expression> children;

  [[nodiscard]] bool valid() const noexcept { return kind != ExpressionKind::invalid; }
};

struct CaseSelector {
  Expression lower;
  bool has_lower{false};
  Expression upper;
  bool has_upper{false};
  bool range{false};
};

struct Statement {
  HirNodeId id{};
  StatementKind kind{StatementKind::expression};
  std::size_t line{1};
  std::string name;
  Expression expression;
  bool has_expression{false};
  bool procedure_call{false};
  semantic::ImplicitResultPolicy implicit_result{semantic::ImplicitResultPolicy::none};
  Expression secondary_expression;
  bool has_secondary_expression{false};
  Expression tertiary_expression;
  bool has_tertiary_expression{false};
  bool inclusive_stop{false};
  bool retain_last_loop_value{true};
  Expression target_expression;
  bool has_target_expression{false};
  std::vector<std::string> parameters;
  std::vector<ParameterKind> parameter_kinds;
  std::vector<Expression> parameter_defaults;
  std::vector<std::string> return_names;
  std::vector<std::string> target_names;
  bool has_target_pattern{false};
  std::vector<CaseSelector> case_selectors;
  bool default_case{false};
  std::vector<Statement> body;
  std::vector<Statement> alternative;
};

struct Program {
  SourceLanguage language{SourceLanguage::automatic};
  semantic::Profile semantics{};
  std::vector<Statement> statements;
  std::size_t node_count{0};
  std::uint64_t revision{0};
};

[[nodiscard]] std::vector<Diagnostic> verify(const Program& program, std::string_view stage);

}  // namespace mpf::detail::hir
