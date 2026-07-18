#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include "analyzer.hpp"

namespace mpf::detail::semantic_internal {

using Expression = hir::Expression;
using Statement = hir::Statement;
using Program = hir::Program;

[[nodiscard]] hir::ExpressionFacts& semantic(hir::SemanticTable& table,
                                             const Expression& expression);
[[nodiscard]] const hir::ExpressionFacts& semantic(const hir::SemanticTable& table,
                                                   const Expression& expression);
[[nodiscard]] hir::StatementFacts& semantic(hir::SemanticTable& table, const Statement& statement);

struct Symbol {
  Symbol() = default;
  Symbol(ValueType symbol_type, BindingKind symbol_binding, bool symbol_assigned,
         ValueType symbol_element_type, std::vector<std::size_t> symbol_shape);

  ValueType type{ValueType::unknown};
  BindingKind binding{BindingKind::variable};
  bool assigned{false};
  ValueType element_type{ValueType::unknown};
  std::vector<std::size_t> shape;
  std::vector<ValueType> tuple_types;
  std::vector<ValueType> tuple_element_types;
  std::vector<std::vector<std::size_t>> tuple_shapes;
  bool sequence_is_list{false};
  std::vector<ValueMetadata> sequence_elements;
};

struct ScopeState {
  ScopeId id{};
  std::vector<Symbol> symbols;
};

[[nodiscard]] bool numeric(ValueType type) noexcept;
[[nodiscard]] ValueType join_types(ValueType left, ValueType right) noexcept;
[[nodiscard]] bool same_metadata(const ValueMetadata& left, const ValueMetadata& right);
[[nodiscard]] bool same_metadata(const std::vector<ValueMetadata>& left,
                                 const std::vector<ValueMetadata>& right);
[[nodiscard]] ValueMetadata expression_metadata(const Expression& expression,
                                                const hir::SemanticTable& table);
[[nodiscard]] std::optional<double> numeric_constant(const Expression& expression);
[[nodiscard]] bool contains_slice(const Expression& expression);
[[nodiscard]] bool has_direct_slice(const Expression& expression);
[[nodiscard]] bool safe_python_default(const Expression& expression);
[[nodiscard]] bool safe_fortran_case_constant(const Expression& expression);
[[nodiscard]] std::optional<std::string> fortran_string_constant(const Expression& expression);
[[nodiscard]] int fortran_string_compare(const std::string& left, const std::string& right);
[[nodiscard]] const Expression* root_container(const Expression& expression);
[[nodiscard]] const Statement* find_statement(const std::vector<Statement>& statements,
                                              HirNodeId id);
[[nodiscard]] std::vector<std::size_t> assignment_shape(const std::vector<std::size_t>& shape,
                                                        std::size_t target_rank);
[[nodiscard]] bool known_shape(const std::vector<std::size_t>& shape);

class Analyzer final {
 public:
  Analyzer(Program& program, hir::SemanticTable& semantics, const NameTable& names,
           const FlowTable& flow);

  [[nodiscard]] std::vector<Diagnostic> analyze();
  [[nodiscard]] bool structure_changed() const noexcept;

 private:
  void register_expression(Expression& expression, hir::ExpressionFacts facts = {});
  [[nodiscard]] Expression clone_expression(const Expression& source);
  void diagnose(std::size_t line, std::string code, std::string message);
  [[nodiscard]] ScopeState make_scope_state(ScopeId id) const;
  void push_scope(ScopeId id);
  [[nodiscard]] ScopeState& current();
  [[nodiscard]] const ScopeState& current() const;
  [[nodiscard]] Symbol* lookup(SymbolId id);
  [[nodiscard]] const Symbol* lookup(SymbolId id) const;
  [[nodiscard]] Symbol* lookup(const Expression& expression);
  [[nodiscard]] const NameUse* definition(const Statement& statement, NameRole role,
                                          std::size_t ordinal = 0) const;
  [[nodiscard]] Symbol& definition_state(const Statement& statement, NameRole role,
                                         std::size_t ordinal = 0);
  [[nodiscard]] bool belongs_to_current_scope(SymbolId id) const;
  bool analyze_statements(std::vector<Statement>& statements);
  bool analyze_statements_in_scope(std::vector<Statement>& statements, ScopeId scope);
  [[nodiscard]] ValueMetadata materialize_sequence(ValueMetadata metadata) const;
  void bind_assignment_leaf(Statement& statement, AssignmentPattern& leaf,
                            const ValueMetadata& metadata, std::size_t target_ordinal);
  [[nodiscard]] ValueMetadata captured_metadata(const std::vector<ValueMetadata>& elements) const;
  [[nodiscard]] bool associate_assignment_pattern(Statement& statement, AssignmentPattern& pattern,
                                                  ValueMetadata metadata,
                                                  std::vector<AssignmentAccess> path,
                                                  std::size_t& target_ordinal);
  bool analyze_statement(Statement& statement);
  [[nodiscard]] bool analyze_branches(Statement& statement);
  void merge_select_flows(const ScopeState& before, const std::vector<ScopeState>& flows);
  [[nodiscard]] bool analyze_select_case(Statement& statement);
  void diagnose_fortran_parameter_write(SymbolId symbol, std::size_t line);
  void mark_fortran_parameter_read(SymbolId symbol);
  void collect_value_returns(const std::vector<Statement>& statements,
                             std::vector<const Expression*>& returns) const;
  void infer_python_tuple_returns(Statement& function) const;
  void infer_python_sequence_metadata(Statement& function) const;
  void analyze_function(Statement& function);
  [[nodiscard]] ValueType collect_return_type(const std::vector<Statement>& statements,
                                              bool& has_value, bool& has_empty,
                                              bool& incompatible) const;
  void annotate_types(std::vector<Statement>& statements);
  void refresh_expression_call_intents(Expression& expression);
  void refresh_call_intents(std::vector<Statement>& statements);

  ValueType analyze_expression(Expression& expression, bool condition_context = false);
  [[nodiscard]] ValueType analyze_binary(Expression& expression, bool condition_context);
  void validate_python_ordering(ValueType left, ValueType right, std::size_t line);
  void validate_python_comparison(ComparisonOperator operation, const Expression& left,
                                  ValueType left_type, const Expression& right,
                                  ValueType right_type);
  [[nodiscard]] ValueType analyze_comparison_chain(Expression& expression);
  [[nodiscard]] ValueType analyze_conditional(Expression& expression);
  void normalize_fortran_arguments(Expression& expression, const Statement& function);
  void normalize_python_arguments(Expression& expression, const Statement& function);
  [[nodiscard]] ValueType analyze_call(Expression& expression);
  [[nodiscard]] ValueType analyze_index(Expression& expression,
                                        bool container_already_analyzed = false,
                                        bool allow_matlab_growth = false);
  void analyze_section_assignment(Statement& statement, ValueType value_type);
  void analyze_indexed_mutation(Statement& statement, ValueType value_type, ValueType target_type);
  [[nodiscard]] std::size_t analyze_slice(Expression& slice, std::size_t extent,
                                          bool allow_matlab_growth = false);
  void validate_static_index(std::size_t line, const Expression& index, std::size_t extent,
                             std::size_t base, bool allow_negative,
                             bool allow_matlab_growth = false);
  [[nodiscard]] ValueType analyze_reshape(Expression& expression);
  [[nodiscard]] ValueType analyze_logical_reduction(Expression& expression,
                                                    semantic::ReductionOperation operation);

  Program& program_;
  hir::SemanticTable& semantics_;
  const NameTable& names_;
  const FlowTable& flow_;
  std::vector<ScopeState> scopes_;
  std::vector<Diagnostic> diagnostics_;
  std::size_t loop_depth_{0};
  std::size_t function_depth_{0};
  std::vector<std::vector<SymbolId>> function_parameters_;
  std::vector<std::vector<bool>> function_parameter_reads_;
  std::vector<std::vector<bool>> function_parameter_writes_;
  std::vector<std::vector<ParameterIntent>*> function_parameter_intents_;
  std::vector<std::unordered_set<SymbolId>> function_optional_parameters_;
  const Expression* fortran_call_expression_{nullptr};
  bool structure_changed_{false};
};

}  // namespace mpf::detail::semantic_internal
