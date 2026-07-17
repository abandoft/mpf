#pragma once

#include <cstddef>
#include <memory_resource>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "../compiler/assignment_pattern.hpp"
#include "../compiler/expression_ast.hpp"
#include "../compiler/statement_kind.hpp"
#include "../ir/hir_lowering.hpp"
#include "../ir/ids.hpp"

namespace mpf::detail {

enum class AstNodeKind { expression, statement };

struct AstNodeRecord {
  AstNodeKind kind{AstNodeKind::expression};
  std::size_t index{0};
  SourceLocation origin{};
};

// LanguageTag participates in the concrete node type, preventing accidental
// cross-language node exchange while retaining a common arena policy.
template <typename LanguageTag>
struct ArenaExpression {
  AstNodeId id{};
  SourceLocation location{};
  ExpressionKind kind{ExpressionKind::invalid};
  std::string value;
  UnaryOperator unary_operation{UnaryOperator::none};
  BinaryOperator operation{BinaryOperator::none};
  ComparisonOperator comparison{ComparisonOperator::none};
  std::vector<ComparisonOperator> comparisons;
  std::vector<AstNodeId> children;
  ValueType inferred_type{ValueType::unknown};
  BindingKind binding{BindingKind::unresolved};
  IntrinsicId intrinsic{IntrinsicId::none};
  ValueType element_type{ValueType::unknown};
  std::vector<std::size_t> shape;
  std::vector<ValueType> tuple_types;
  std::vector<ValueType> tuple_element_types;
  std::vector<std::vector<std::size_t>> tuple_shapes;
  bool sequence_is_list{false};
  std::vector<ValueMetadata> sequence_elements;
  std::size_t requested_outputs{1};
  bool multi_output_call{false};
  std::vector<ParameterIntent> argument_intents;
  std::vector<std::string> argument_names;
  std::vector<bool> argument_optional_forward;
  bool procedure_has_result{false};
  std::size_t index_base{0};
  bool allow_negative_index{false};
  bool column_major{false};
  bool slice_stop_inclusive{false};
};

template <typename LanguageTag>
struct ArenaCaseSelector {
  AstNodeId lower{};
  bool has_lower{false};
  AstNodeId upper{};
  bool has_upper{false};
  bool range{false};
};

template <typename LanguageTag>
struct ArenaStatement {
  AstNodeId id{};
  StatementKind kind{StatementKind::expression};
  std::size_t line{1};
  std::string name;
  AstNodeId expression{};
  bool has_expression{false};
  bool procedure_call{false};
  AstNodeId secondary_expression{};
  bool has_secondary_expression{false};
  AstNodeId tertiary_expression{};
  bool has_tertiary_expression{false};
  bool inclusive_stop{false};
  bool retain_last_loop_value{true};
  bool exported{false};
  ValueType declared_type{ValueType::unknown};
  ValueType element_type{ValueType::unknown};
  ValueType previous_type{ValueType::unknown};
  ValueType previous_element_type{ValueType::unknown};
  ParameterIntent parameter_intent{ParameterIntent::none};
  bool optional_parameter{false};
  bool dummy_parameter{false};
  std::vector<std::size_t> shape;
  std::size_t index_base{0};
  bool allow_negative_index{false};
  AstNodeId target_expression{};
  bool has_target_expression{false};
  std::vector<std::string> parameters;
  std::vector<ParameterKind> parameter_kinds;
  std::vector<AstNodeId> parameter_defaults;
  std::vector<ParameterIntent> parameter_intents;
  std::vector<bool> parameter_optional;
  std::vector<ValueType> parameter_types;
  std::vector<ValueType> parameter_element_types;
  std::vector<std::vector<std::size_t>> parameter_shapes;
  std::vector<std::string> return_names;
  bool has_value_return{false};
  std::vector<ValueType> return_types;
  std::vector<ValueType> return_element_types;
  std::vector<std::vector<std::size_t>> return_shapes;
  bool return_sequence_is_list{false};
  std::vector<ValueMetadata> return_sequence_elements;
  std::vector<std::string> target_names;
  AssignmentPattern target_pattern;
  bool has_target_pattern{false};
  std::vector<ValueType> target_types;
  std::vector<ValueType> target_element_types;
  std::vector<std::vector<std::size_t>> target_shapes;
  std::vector<ValueType> target_previous_types;
  std::vector<ValueType> target_previous_element_types;
  std::vector<ArenaCaseSelector<LanguageTag>> case_selectors;
  bool default_case{false};
  std::vector<AstNodeId> body;
  std::vector<AstNodeId> alternative;
};

template <typename LanguageTag>
class ArenaProgram {
 public:
  explicit ArenaProgram(std::pmr::memory_resource* resource = std::pmr::get_default_resource())
      : records(resource), expressions(resource), statements(resource), roots(resource) {
    records.push_back({});
  }

  ArenaProgram(const ArenaProgram&) = delete;
  ArenaProgram& operator=(const ArenaProgram&) = delete;
  ArenaProgram(ArenaProgram&&) noexcept = default;
  ArenaProgram& operator=(ArenaProgram&&) noexcept = default;

  [[nodiscard]] std::size_t node_count() const noexcept { return records.size() - 1U; }
  [[nodiscard]] std::pmr::memory_resource* resource() const noexcept {
    return records.get_allocator().resource();
  }

  SourceLanguage language{SourceLanguage::automatic};
  std::pmr::vector<AstNodeRecord> records;
  std::pmr::vector<ArenaExpression<LanguageTag>> expressions;
  std::pmr::vector<ArenaStatement<LanguageTag>> statements;
  std::pmr::vector<AstNodeId> roots;
};

template <typename LanguageTag>
struct ArenaParseResult {
  ArenaProgram<LanguageTag> program;
  std::vector<Diagnostic> diagnostics;
};

namespace python::ast {
struct LanguageTag;
using Expression = ArenaExpression<LanguageTag>;
using Statement = ArenaStatement<LanguageTag>;
using CaseSelector = ArenaCaseSelector<LanguageTag>;
using Program = ArenaProgram<LanguageTag>;
using ParseResult = ArenaParseResult<LanguageTag>;
}  // namespace python::ast

namespace matlab::ast {
struct LanguageTag;
using Expression = ArenaExpression<LanguageTag>;
using Statement = ArenaStatement<LanguageTag>;
using CaseSelector = ArenaCaseSelector<LanguageTag>;
using Program = ArenaProgram<LanguageTag>;
using ParseResult = ArenaParseResult<LanguageTag>;
}  // namespace matlab::ast

namespace fortran::ast {
struct LanguageTag;
using Expression = ArenaExpression<LanguageTag>;
using Statement = ArenaStatement<LanguageTag>;
using CaseSelector = ArenaCaseSelector<LanguageTag>;
using Program = ArenaProgram<LanguageTag>;
using ParseResult = ArenaParseResult<LanguageTag>;
}  // namespace fortran::ast

namespace typescript::ast {
struct LanguageTag;
using Expression = ArenaExpression<LanguageTag>;
using Statement = ArenaStatement<LanguageTag>;
using CaseSelector = ArenaCaseSelector<LanguageTag>;
using Program = ArenaProgram<LanguageTag>;
using ParseResult = ArenaParseResult<LanguageTag>;
}  // namespace typescript::ast

using FrontendAst = std::variant<std::monostate, python::ast::Program, matlab::ast::Program,
                                 fortran::ast::Program, typescript::ast::Program>;

struct FrontendParseResult {
  FrontendAst ast;
  std::vector<Diagnostic> diagnostics;
};

[[nodiscard]] hir::LoweringResult lower_python_ast(python::ast::Program&& program);
[[nodiscard]] hir::LoweringResult lower_matlab_ast(matlab::ast::Program&& program);
[[nodiscard]] hir::LoweringResult lower_fortran_ast(fortran::ast::Program&& program);
[[nodiscard]] hir::LoweringResult lower_typescript_ast(typescript::ast::Program&& program);

[[nodiscard]] std::size_t frontend_ast_node_count(const FrontendAst& ast) noexcept;
[[nodiscard]] std::size_t frontend_ast_arena_bytes(const FrontendAst& ast) noexcept;
[[nodiscard]] std::string dump_frontend_ast(const FrontendAst& ast);
[[nodiscard]] std::vector<Diagnostic> verify_frontend_ast(const FrontendAst& ast,
                                                          SourceLanguage expected_language);

}  // namespace mpf::detail
