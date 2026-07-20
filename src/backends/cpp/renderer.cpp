#include "renderer.hpp"

#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "backends/common/identifier_mangler.hpp"
#include "lir.hpp"
#include "runtime.hpp"

namespace mpf::detail {
namespace {

using Expression = cpp::lir::Expression;
using Statement = cpp::lir::Statement;
using Program = cpp::lir::SemanticProgram;

class Renderer final {
 public:
  RenderedOutput render(const Program& program) {
    temporaries_ = &program.temporaries;
    source_segments_ = &program.source_segments;
    expressions_.assign(program.node_count + 1U, nullptr);
    index_statements(program.statements);
    mangler_ = std::make_unique<IdentifierMangler>(program.identifiers);
    const auto& unit = program.translation_unit;
    output_ << unit.banner;
    for (const auto& header : unit.standard_headers) output_ << "#include " << header << '\n';
    output_ << '\n';
    emit_cpp_runtime(output_, unit.runtime_namespace, unit.runtime_fragments);
    output_ << "namespace " << unit.generated_namespace << " {\n";
    indent_ = 1;
    if (unit.emit_module_scope) {
      emit_scope_declarations(program.program_scope, "[[maybe_unused]] static ");
      if (unit.emit_entry_function) output_ << '\n';
    }
    bool emitted_declaration = false;
    for (const auto index : unit.forward_declarations) {
      emit_function_declaration(program.statements[index]);
      emitted_declaration = true;
    }
    if (emitted_declaration) output_ << '\n';
    for (const auto index : unit.definitions) {
      emit_function(program.statements[index]);
      output_ << '\n';
    }
    if (unit.emit_entry_function) {
      indentation();
      output_ << "int run() {\n";
      indent_ = 2;
      if (unit.entry_owns_program_scope) {
        emit_scope_declarations(program.program_scope);
      }
      for (const auto index : unit.entry_statements) {
        emit_statement(program.statements[index]);
      }
      indentation();
      output_ << "return 0;\n";
      indent_ = 1;
      indentation();
      output_ << "}\n";
    }
    indent_ = 0;
    output_ << "}  // namespace " << unit.generated_namespace << "\n";
    if (unit.emit_main) {
      if (unit.entry_error_policy != cpp::lir::EntryErrorPolicy::report_standard_exception) {
        throw std::logic_error("verified cpp entry error policy is missing");
      }
      output_ << "\nint main() {\n"
                 "  try {\n"
                 "    return "
              << unit.generated_namespace
              << "::run();\n"
                 "  } catch (const std::exception& error) {\n"
                 "    std::cerr << \"MPF runtime error: \" << error.what() << '\\n';\n"
                 "    return 1;\n"
                 "  }\n"
                 "}\n";
    }
    return {output_.str(), std::move(markers_)};
  }

 private:
  void index_expression(const Expression& expression) {
    if (!expression.valid()) return;
    expressions_[expression.id.value()] = &expression;
    for (const auto& child : expression.children) index_expression(child);
  }

  void index_statements(const std::vector<Statement>& statements) {
    for (const auto& statement : statements) {
      index_expression(statement.expression);
      index_expression(statement.secondary_expression);
      index_expression(statement.tertiary_expression);
      index_expression(statement.target_expression);
      for (const auto& expression : statement.parameter_defaults) index_expression(expression);
      for (const auto& selector : statement.case_selectors) {
        index_expression(selector.lower);
        index_expression(selector.upper);
      }
      index_statements(statement.body);
      index_statements(statement.alternative);
    }
  }

  const Expression& expression(const LirNodeId id) const {
    const auto* result = expressions_[id.value()];
    if (result == nullptr) throw std::logic_error("verified cpp LIR type probe is missing");
    return *result;
  }

  void indentation() {
    for (std::size_t level = 0; level < indent_; ++level) output_ << "  ";
  }

  static int precedence(const Expression& expression) noexcept {
    return expression.plan.precedence;
  }

  void emit_named_comparison(const std::string& left, const cpp::lir::ComparisonPlan& comparison,
                             const std::string& right) {
    switch (comparison.form) {
      case cpp::lir::ComparisonForm::dynamic_compare:
        output_ << "mpf_runtime::py_compare(" << left << ", " << right << ", " << comparison.token
                << ')';
        return;
      case cpp::lir::ComparisonForm::structural_equal:
      case cpp::lir::ComparisonForm::structural_not_equal:
        if (comparison.form == cpp::lir::ComparisonForm::structural_not_equal) output_ << '!';
        output_ << "mpf_runtime::py_equal(" << left << ", " << right << ')';
        return;
      case cpp::lir::ComparisonForm::identity:
      case cpp::lir::ComparisonForm::not_identity:
        if (comparison.form == cpp::lir::ComparisonForm::not_identity) output_ << '!';
        output_ << "mpf_runtime::py_is(" << left << ", " << right << ')';
        return;
      case cpp::lir::ComparisonForm::membership:
      case cpp::lir::ComparisonForm::not_membership:
        if (comparison.form == cpp::lir::ComparisonForm::not_membership) output_ << '!';
        output_ << "mpf_runtime::py_contains(" << right << ", " << left << ')';
        return;
      case cpp::lir::ComparisonForm::infix: break;
    }
    output_ << left << ' ' << comparison.token << ' ' << right;
  }

  void emit_binary_comparison(const Expression& expression) {
    if (expression.plan.evaluation !=
        cpp::lir::EvaluationForm::binary_comparison_reference_lambda_iife) {
      throw std::logic_error("verified cpp binary comparison evaluation plan is missing");
    }
    const auto& comparison = expression.plan.comparisons.front();
    const auto& left = temporary(expression.id, cpp::lir::TemporaryRole::comparison_operand, 0);
    const auto& right = temporary(expression.id, cpp::lir::TemporaryRole::comparison_operand, 1);
    output_ << "([&]() { auto&& " << left << " = ";
    emit_expression(expression.children[0]);
    output_ << "; auto&& " << right << " = ";
    emit_expression(expression.children[1]);
    output_ << "; return ";
    emit_named_comparison(left, comparison, right);
    output_ << "; })()";
  }

  void emit_comparison_chain(const Expression& expression) {
    if (expression.plan.evaluation != cpp::lir::EvaluationForm::comparison_reference_lambda_iife) {
      throw std::logic_error("verified cpp comparison evaluation plan is missing");
    }
    std::vector<std::string> operands;
    operands.reserve(expression.children.size());
    output_ << "([&]() { ";
    for (std::size_t index = 0; index < expression.children.size(); ++index) {
      operands.push_back(
          temporary(expression.id, cpp::lir::TemporaryRole::comparison_operand, index));
      output_ << "auto&& " << operands.back() << " = ";
      emit_expression(expression.children[index]);
      output_ << "; ";
      if (index == 0) continue;
      if (index < expression.children.size() - 1)
        output_ << "if (!(";
      else
        output_ << "return ";
      emit_named_comparison(operands[index - 1], expression.plan.comparisons[index - 1U],
                            operands[index]);
      if (index < expression.children.size() - 1)
        output_ << ")) return false; ";
      else
        output_ << "; ";
    }
    output_ << "})()";
  }

  void emit_optional_bound(const Expression& bound) {
    if (!bound.valid()) {
      output_ << "std::nullopt";
      return;
    }
    output_ << "std::optional<std::int64_t>{static_cast<std::int64_t>(";
    emit_expression(bound);
    output_ << ")}";
  }

  void emit_slice_bounds(const Expression& slice) {
    emit_optional_bound(slice.children[0]);
    output_ << ", ";
    emit_optional_bound(slice.children[1]);
    output_ << ", ";
    emit_optional_bound(slice.children[2]);
  }

  void emit_slice_parameters(const Expression& slice) {
    emit_slice_bounds(slice);
    output_ << ", " << slice.plan.index_base << ", "
            << (slice.plan.allow_negative_index ? "true" : "false") << ", "
            << (slice.plan.inclusive_slice_stop ? "true" : "false");
  }

  void emit_shape_array(const std::vector<std::size_t>& shape) {
    output_ << "std::array<std::size_t, " << shape.size() << ">{";
    for (std::size_t index = 0; index < shape.size(); ++index) {
      if (index != 0) output_ << ", ";
      output_ << shape[index];
    }
    output_ << '}';
  }

  void emit_shape_vector(const std::vector<std::size_t>& shape) {
    output_ << "std::vector<std::size_t>{";
    for (std::size_t axis = 0; axis < shape.size(); ++axis) {
      if (axis != 0U) output_ << ", ";
      output_ << shape[axis];
    }
    output_ << '}';
  }

  void emit_sparse_result_extent(const std::size_t extent) {
    if (extent == dynamic_extent)
      output_ << "std::nullopt";
    else
      output_ << "std::optional<std::size_t>{" << extent << '}';
  }

  void emit_sparse_index(const Expression& expression) {
    const auto kind = expression.plan.sparse_index.kind;
    switch (kind) {
      case semantic::SparseIndexKind::linear_element:
        output_ << "mpf_runtime::sparse_linear_element(";
        break;
      case semantic::SparseIndexKind::subscript_element:
        output_ << "mpf_runtime::sparse_subscript_element(";
        break;
      case semantic::SparseIndexKind::linear_selection:
        output_ << "mpf_runtime::sparse_linear_selection(";
        break;
      case semantic::SparseIndexKind::submatrix_selection:
        output_ << "mpf_runtime::sparse_submatrix_selection(";
        break;
      case semantic::SparseIndexKind::none:
        throw std::logic_error("verified cpp sparse index plan is missing");
    }
    emit_expression(expression.children.front());
    for (std::size_t index = 1U; index < expression.children.size(); ++index) {
      output_ << ", ";
      emit_selector(expression, index);
    }
    if (kind == semantic::SparseIndexKind::linear_selection ||
        kind == semantic::SparseIndexKind::submatrix_selection) {
      for (const auto extent : expression.plan.sparse_index.result_shape) {
        output_ << ", ";
        emit_sparse_result_extent(extent);
      }
    }
    output_ << ", " << expression.plan.index_base << ')';
  }

  static bool known_static_shape(const std::vector<std::size_t>& shape) noexcept {
    if (shape.empty()) return false;
    for (const auto extent : shape) {
      if (extent == dynamic_extent) return false;
    }
    return true;
  }

  void emit_selector_tuple(const Expression& expression) {
    output_ << "std::make_tuple(";
    for (std::size_t index = 1; index < expression.children.size(); ++index) {
      if (index != 1) output_ << ", ";
      emit_selector(expression, index);
    }
    output_ << ')';
  }

  void emit_selector(const Expression& expression, const std::size_t child_index) {
    const auto& selector = expression.children[child_index];
    const bool runtime_extent =
        !type_probe_ &&
        semantic::requires_runtime_extent(expression.plan.index_extents.at(child_index - 1U));
    if (runtime_extent) output_ << "[&](std::size_t __mpf_extent) { return ";
    switch (expression.plan.index_selectors.at(child_index - 1U)) {
      case semantic::IndexSelectorKind::slice:
        output_ << "mpf_runtime::slice_selector{";
        emit_slice_bounds(selector);
        output_ << ", " << expression.plan.index_base << ", "
                << (expression.plan.allow_negative_index ? "true" : "false") << ", "
                << (selector.plan.inclusive_slice_stop ? "true" : "false") << '}';
        break;
      case semantic::IndexSelectorKind::scalar:
        output_ << "mpf_runtime::scalar_selector{static_cast<std::int64_t>(";
        emit_expression(selector);
        output_ << ")}";
        break;
      case semantic::IndexSelectorKind::numeric:
        output_ << "mpf_runtime::numeric_selector{";
        emit_expression(selector);
        output_ << '}';
        break;
      case semantic::IndexSelectorKind::logical:
        output_ << "mpf_runtime::logical_selector{";
        emit_expression(selector);
        output_ << '}';
        break;
      case semantic::IndexSelectorKind::empty:
        output_ << "mpf_runtime::empty_selector{";
        emit_expression(selector);
        output_ << '}';
        break;
    }
    if (runtime_extent) output_ << "; }";
  }

  void emit_runtime_index(const Expression& container, const Expression& index_expression,
                          const Expression& index_metadata) {
    output_ << "mpf_runtime::index(";
    emit_expression(container);
    output_ << ", static_cast<std::int64_t>(";
    emit_expression(index_expression);
    output_ << "), " << index_metadata.plan.index_base << ", "
            << (index_metadata.plan.allow_negative_index ? "true" : "false") << ')';
  }

  void emit_row_slice(const Expression& index_expression, const Expression& slice) {
    output_ << "mpf_runtime::slice(";
    emit_expression(index_expression.children[0]);
    output_ << ", ";
    emit_slice_parameters(slice);
    output_ << ')';
  }

  void emit_section_replacement(const Expression& replacement, const bool flatten) {
    if (flatten) {
      output_ << "mpf_runtime::flatten_column_major(";
      emit_expression(replacement);
      output_ << ')';
    } else {
      emit_expression(replacement);
    }
  }

  void emit_section_assignment(const Expression& target, const Expression& replacement,
                               const bool flatten_replacement = false,
                               const bool resizable_section = false) {
    if (target.plan.index == cpp::lir::IndexForm::linear_section) {
      output_ << "mpf_runtime::assign_linear_section_column_major(";
      emit_expression(target.children[0]);
      output_ << ", ";
      emit_selector(target, 1U);
      output_ << ", " << target.plan.index_base << ", "
              << (target.plan.allow_negative_index ? "true" : "false") << ", ";
      emit_section_replacement(replacement, flatten_replacement);
      output_ << ')';
      return;
    }
    if (target.plan.index == cpp::lir::IndexForm::section_nd) {
      output_ << "mpf_runtime::assign_section_nd(";
      emit_expression(target.children[0]);
      output_ << ", ";
      emit_selector_tuple(target);
      output_ << ", " << target.plan.index_base << ", "
              << (target.plan.allow_negative_index ? "true" : "false") << ", ";
      emit_section_replacement(replacement, flatten_replacement);
      output_ << ')';
      return;
    }
    if (target.plan.index == cpp::lir::IndexForm::slice) {
      const auto& slice = target.children[1];
      if (target.plan.flatten_base) {
        output_ << "mpf_runtime::assign_linear_column_major(";
        emit_expression(target.children[0]);
        output_ << ", ";
        emit_slice_bounds(slice);
        output_ << ", " << target.plan.index_base << ", "
                << (slice.plan.inclusive_slice_stop ? "true" : "false") << ", ";
        emit_section_replacement(replacement, flatten_replacement);
        output_ << ')';
      } else {
        output_ << "mpf_runtime::assign_slice(";
        emit_expression(target.children[0]);
        output_ << ", ";
        emit_slice_parameters(slice);
        output_ << ", ";
        emit_section_replacement(replacement, flatten_replacement);
        output_ << ", " << (resizable_section ? "true" : "false") << ')';
      }
      return;
    }
    const auto& row = target.children[1];
    const auto& column = target.children[2];
    if (target.plan.index == cpp::lir::IndexForm::row_slice) {
      output_ << "mpf_runtime::assign_slice(";
      emit_runtime_index(target.children[0], row, target);
      output_ << ", ";
      emit_slice_parameters(column);
      output_ << ", ";
      emit_section_replacement(replacement, flatten_replacement);
      output_ << ", false)";
    } else if (target.plan.index == cpp::lir::IndexForm::column) {
      output_ << "mpf_runtime::assign_column(";
      emit_expression(target.children[0]);
      output_ << ", ";
      emit_slice_bounds(row);
      output_ << ", static_cast<std::int64_t>(";
      emit_expression(column);
      output_ << "), " << target.plan.index_base << ", "
              << (target.plan.allow_negative_index ? "true" : "false") << ", "
              << (row.plan.inclusive_slice_stop ? "true" : "false") << ", ";
      emit_section_replacement(replacement, flatten_replacement);
      output_ << ')';
    } else {
      output_ << "mpf_runtime::assign_block(";
      emit_expression(target.children[0]);
      output_ << ", ";
      emit_slice_bounds(row);
      output_ << ", ";
      emit_slice_bounds(column);
      output_ << ", " << target.plan.index_base << ", "
              << (target.plan.allow_negative_index ? "true" : "false") << ", "
              << (row.plan.inclusive_slice_stop ? "true" : "false") << ", ";
      emit_section_replacement(replacement, flatten_replacement);
      output_ << ')';
    }
  }

  void emit_direct_call(const Expression& expression,
                        const std::vector<std::string>* replacements = nullptr) {
    switch (expression.plan.call) {
      case cpp::lir::CallForm::python_float:
        output_ << "mpf_runtime::py_float(";
        emit_expression(expression.children[1]);
        output_ << ')';
        return;
      case cpp::lir::CallForm::python_length:
        output_ << "static_cast<std::int64_t>(";
        emit_expression(expression.children[1]);
        output_ << ".size())";
        return;
      case cpp::lir::CallForm::matlab_length:
        output_ << "static_cast<std::int64_t>(mpf_runtime::length(";
        emit_expression(expression.children[1]);
        if (known_static_shape(expression.plan.input_shape)) {
          output_ << ", ";
          emit_shape_array(expression.plan.input_shape);
        }
        output_ << "))";
        return;
      case cpp::lir::CallForm::element_count:
        output_ << "static_cast<std::int64_t>(mpf_runtime::numel(";
        emit_expression(expression.children[1]);
        output_ << "))";
        return;
      case cpp::lir::CallForm::matlab_all:
      case cpp::lir::CallForm::matlab_any: {
        output_ << expression.plan.token << '(';
        emit_expression(expression.children[1]);
        for (const auto& shape : expression.plan.runtime_shape_arguments) {
          output_ << ", ";
          emit_shape_array(shape);
        }
        for (const auto value : expression.plan.runtime_integer_arguments) {
          output_ << ", " << value;
        }
        output_ << ')';
        return;
      }
      case cpp::lir::CallForm::sum:
        output_ << "mpf_runtime::sum(";
        emit_expression(expression.children[1]);
        output_ << ')';
        return;
      case cpp::lir::CallForm::present:
        output_ << '('
                << mangler_->name(expression.children[1].symbol_id,
                                  expression.children[1].plan.token)
                << ".has_value())";
        return;
      case cpp::lir::CallForm::reshape:
        output_ << "mpf_runtime::reshape_column_major_nd(";
        emit_expression(expression.children[1]);
        output_ << ", ";
        emit_shape_array(expression.plan.input_shape);
        output_ << ", ";
        emit_shape_array(expression.plan.result_shape);
        output_ << ')';
        return;
      case cpp::lir::CallForm::matlab_sparse_reshape:
        output_ << "mpf_runtime::sparse_reshape(";
        emit_expression(expression.children[1]);
        output_ << ", ";
        emit_shape_array(expression.plan.sparse_reshape.input_shape);
        output_ << ", ";
        emit_shape_array(expression.plan.sparse_reshape.requested_shape);
        output_ << ", ";
        emit_shape_array(expression.plan.sparse_reshape.result_shape);
        output_ << ')';
        return;
      case cpp::lir::CallForm::matlab_sparse:
        if (!expression.plan.runtime_shape_arguments.empty()) {
          output_ << expression.plan.token << '(';
          emit_expression(expression.children[1]);
          for (const auto& shape : expression.plan.runtime_shape_arguments) {
            output_ << ", ";
            emit_shape_array(shape);
          }
          output_ << ')';
          return;
        }
        output_ << expression.plan.token << '(';
        for (std::size_t index = 1U; index < expression.children.size(); ++index) {
          if (index != 1U) output_ << ", ";
          emit_expression(expression.children[index]);
        }
        output_ << ')';
        return;
      case cpp::lir::CallForm::matlab_full:
        output_ << "mpf_runtime::full(";
        emit_expression(expression.children[1]);
        output_ << ')';
        return;
      case cpp::lir::CallForm::matlab_is_sparse:
        output_ << "mpf_runtime::issparse(";
        emit_expression(expression.children[1]);
        output_ << ')';
        return;
      case cpp::lir::CallForm::matlab_nonzero_count:
        output_ << "static_cast<std::int64_t>(mpf_runtime::nnz(";
        emit_expression(expression.children[1]);
        output_ << "))";
        return;
      case cpp::lir::CallForm::none:
      case cpp::lir::CallForm::direct: break;
    }
    if (!expression.children.empty()) {
      emit_expression(expression.children.front(), expression.plan.precedence);
    }
    output_ << '(';
    for (std::size_t index = 1; index < expression.children.size(); ++index) {
      if (index != 1) output_ << ", ";
      if (replacements != nullptr && index < replacements->size() &&
          !(*replacements)[index].empty()) {
        output_ << (*replacements)[index];
      } else if (index - 1U < expression.plan.call_arguments.size() &&
                 expression.plan.call_arguments[index - 1U].form ==
                     cpp::lir::CallArgumentForm::forward_optional) {
        output_ << mangler_->name(expression.children[index].symbol_id,
                                  expression.children[index].plan.token);
      } else {
        emit_expression(expression.children[index]);
      }
    }
    output_ << ')';
  }

  void emit_call_value(const Expression& expression,
                       const std::vector<std::string>* replacements = nullptr) {
    if (expression.plan.call_value == cpp::lir::CallValueForm::first_tuple_result) {
      output_ << "std::get<0>(";
    }
    emit_direct_call(expression, replacements);
    if (expression.plan.call_value == cpp::lir::CallValueForm::first_tuple_result) output_ << ')';
  }

  void emit_section_reference_call(const Expression& expression) {
    std::vector<std::string> temporaries(expression.children.size());
    output_ << "([&]() { ";
    for (std::size_t index = 1; index < expression.children.size(); ++index) {
      const auto intent_index = index - 1;
      if (intent_index >= expression.plan.call_arguments.size() ||
          expression.plan.call_arguments[intent_index].form !=
              cpp::lir::CallArgumentForm::copy_section) {
        continue;
      }
      temporaries[index] =
          temporary(expression.id, cpp::lir::TemporaryRole::section_argument, intent_index);
      output_ << "auto " << temporaries[index] << " = ";
      emit_expression(expression.children[index]);
      output_ << "; ";
    }
    std::string result;
    if (expression.plan.call_outcome == cpp::lir::CallOutcomeForm::value) {
      result = temporary(expression.id, cpp::lir::TemporaryRole::call_result);
      output_ << "auto " << result << " = ";
    }
    emit_call_value(expression, &temporaries);
    output_ << "; ";
    for (std::size_t index = 1; index < temporaries.size(); ++index) {
      if (temporaries[index].empty()) continue;
      if (expression.plan.call_arguments[index - 1U].writeback !=
          cpp::lir::WritebackForm::section) {
        throw std::logic_error("verified cpp copy call has no section writeback plan");
      }
      Expression replacement = expression.children[index];
      replacement.plan.form = cpp::lir::ExpressionForm::variable;
      replacement.plan.token = temporaries[index];
      replacement.plan.precedence = 10;
      replacement.children.clear();
      emit_section_assignment(expression.children[index], replacement);
      output_ << "; ";
    }
    if (!result.empty()) output_ << "return " << result << "; ";
    output_ << "})()";
  }

  void emit_expression(const Expression& expression, const int parent = 0) {
    mark(expression.id);
    if (expression.plan.form == cpp::lir::ExpressionForm::call) {
      if (expression.plan.evaluation == cpp::lir::EvaluationForm::copy_call_reference_lambda_iife) {
        emit_section_reference_call(expression);
      } else {
        emit_call_value(expression);
      }
      return;
    }
    const auto own = precedence(expression);
    const bool parentheses = own < parent;
    if (parentheses) output_ << '(';
    switch (expression.plan.form) {
      case cpp::lir::ExpressionForm::invalid: output_ << '0'; break;
      case cpp::lir::ExpressionForm::omitted:
      case cpp::lir::ExpressionForm::target_symbol:
      case cpp::lir::ExpressionForm::scalar_literal:
      case cpp::lir::ExpressionForm::null_literal: output_ << expression.plan.token; break;
      case cpp::lir::ExpressionForm::variable:
        output_ << mangler_->name(expression.symbol_id, expression.plan.token);
        if (expression.plan.variable_access == cpp::lir::VariableAccess::optional_value) {
          output_ << ".value()";
        }
        break;
      case cpp::lir::ExpressionForm::string_literal:
        output_ << "std::string{" << expression.plan.token << '}';
        break;
      case cpp::lir::ExpressionForm::unary_truthiness:
        output_ << "mpf_runtime::py_not(";
        if (!expression.children.empty()) emit_expression(expression.children.front());
        output_ << ')';
        break;
      case cpp::lir::ExpressionForm::matlab_logical_not:
        output_ << expression.plan.token << '(';
        if (!expression.children.empty()) emit_expression(expression.children.front());
        for (const auto& shape : expression.plan.runtime_shape_arguments) {
          output_ << ", ";
          emit_shape_array(shape);
        }
        for (const auto value : expression.plan.runtime_integer_arguments) {
          output_ << ", " << value;
        }
        output_ << ')';
        break;
      case cpp::lir::ExpressionForm::unary_operator:
        output_ << expression.plan.token;
        if (!expression.children.empty()) emit_expression(expression.children.front(), own);
        break;
      case cpp::lir::ExpressionForm::matlab_transpose:
        output_ << expression.plan.token << '(';
        emit_expression(expression.children.front());
        if (known_static_shape(expression.plan.input_shape) &&
            known_static_shape(expression.plan.result_shape)) {
          output_ << ", ";
          emit_shape_array(expression.plan.input_shape);
          output_ << ", ";
          emit_shape_array(expression.plan.result_shape);
        }
        output_ << ')';
        break;
      case cpp::lir::ExpressionForm::matlab_sparse_transpose:
        output_ << expression.plan.token << '(';
        emit_expression(expression.children.front());
        output_ << ')';
        break;
      case cpp::lir::ExpressionForm::binary_lazy_and:
      case cpp::lir::ExpressionForm::binary_lazy_or:
        if (expression.plan.evaluation != cpp::lir::EvaluationForm::lazy_reference_lambda_thunks) {
          throw std::logic_error("verified cpp lazy evaluation plan is missing");
        }
        output_ << (expression.plan.form == cpp::lir::ExpressionForm::binary_lazy_and
                        ? "mpf_runtime::py_and"
                        : "mpf_runtime::py_or")
                << "([&]() { return (";
        emit_expression(expression.children[0]);
        output_ << "); }, [&]() { return (";
        emit_expression(expression.children[1]);
        output_ << "); })";
        break;
      case cpp::lir::ExpressionForm::matlab_short_circuit_and:
      case cpp::lir::ExpressionForm::matlab_short_circuit_or:
        output_ << "mpf_runtime::matlab_scalar_logical(";
        emit_expression(expression.children[0]);
        output_ << ") "
                << (expression.plan.form == cpp::lir::ExpressionForm::matlab_short_circuit_and
                        ? "&&"
                        : "||")
                << " mpf_runtime::matlab_scalar_logical(";
        emit_expression(expression.children[1]);
        output_ << ')';
        break;
      case cpp::lir::ExpressionForm::binary_power:
        output_ << "std::pow(";
        emit_expression(expression.children[0]);
        output_ << ", ";
        emit_expression(expression.children[1]);
        output_ << ')';
        break;
      case cpp::lir::ExpressionForm::binary_runtime_call:
      case cpp::lir::ExpressionForm::binary_reverse_runtime_call: {
        const bool reverse =
            expression.plan.form == cpp::lir::ExpressionForm::binary_reverse_runtime_call;
        output_ << expression.plan.token << '(';
        emit_expression(expression.children[reverse ? 1U : 0U]);
        output_ << ", ";
        emit_expression(expression.children[reverse ? 0U : 1U]);
        output_ << ')';
        break;
      }
      case cpp::lir::ExpressionForm::binary_comparison: {
        emit_binary_comparison(expression);
        break;
      }
      case cpp::lir::ExpressionForm::binary_operator:
        emit_expression(expression.children[0], own);
        output_ << ' ' << expression.plan.token << ' ';
        emit_expression(expression.children[1], own + 1);
        break;
      case cpp::lir::ExpressionForm::matlab_logical_operation:
      case cpp::lir::ExpressionForm::matlab_array_operation:
        output_ << expression.plan.token;
        output_ << '(';
        emit_expression(expression.children[0]);
        output_ << ", ";
        emit_expression(expression.children[1]);
        for (const auto& shape : expression.plan.runtime_shape_arguments) {
          output_ << ", ";
          emit_shape_array(shape);
        }
        for (const auto value : expression.plan.runtime_integer_arguments) {
          output_ << ", " << value;
        }
        output_ << ')';
        break;
      case cpp::lir::ExpressionForm::comparison_chain: emit_comparison_chain(expression); break;
      case cpp::lir::ExpressionForm::conditional:
        output_ << "(mpf_runtime::truthy(";
        emit_expression(expression.children[0]);
        output_ << ") ? (";
        emit_expression(expression.children[1]);
        output_ << ") : (";
        emit_expression(expression.children[2]);
        output_ << "))";
        break;
      case cpp::lir::ExpressionForm::call: break;
      case cpp::lir::ExpressionForm::runtime_extent:
        if (type_probe_)
          output_ << '1';
        else
          output_ << "static_cast<std::int64_t>(" << expression.plan.token << ')';
        break;
      case cpp::lir::ExpressionForm::matlab_sparse_index: emit_sparse_index(expression); break;
      case cpp::lir::ExpressionForm::index:
        switch (expression.plan.index) {
          case cpp::lir::IndexForm::slice:
            output_ << "mpf_runtime::slice(";
            if (expression.plan.flatten_base) {
              output_ << "mpf_runtime::flatten_column_major(";
              emit_expression(expression.children[0]);
              output_ << ')';
            } else {
              emit_expression(expression.children[0]);
            }
            output_ << ", ";
            emit_slice_parameters(expression.children[1]);
            output_ << ')';
            break;
          case cpp::lir::IndexForm::row_slice:
            output_ << "mpf_runtime::slice(";
            emit_runtime_index(expression.children[0], expression.children[1], expression);
            output_ << ", ";
            emit_slice_parameters(expression.children[2]);
            output_ << ')';
            break;
          case cpp::lir::IndexForm::column:
            output_ << "mpf_runtime::column(";
            emit_row_slice(expression, expression.children[1]);
            output_ << ", static_cast<std::int64_t>(";
            emit_expression(expression.children[2]);
            output_ << "), " << expression.plan.index_base << ", "
                    << (expression.plan.allow_negative_index ? "true" : "false") << ')';
            break;
          case cpp::lir::IndexForm::block:
            output_ << "mpf_runtime::columns(";
            emit_row_slice(expression, expression.children[1]);
            output_ << ", ";
            emit_slice_parameters(expression.children[2]);
            output_ << ')';
            break;
          case cpp::lir::IndexForm::section_nd:
            output_ << "mpf_runtime::section_nd(";
            emit_expression(expression.children[0]);
            output_ << ", ";
            emit_selector_tuple(expression);
            output_ << ", " << expression.plan.index_base << ", "
                    << (expression.plan.allow_negative_index ? "true" : "false") << ')';
            break;
          case cpp::lir::IndexForm::linear_section:
            output_ << "mpf_runtime::linear_section_column_major(";
            emit_expression(expression.children[0]);
            output_ << ", ";
            emit_selector(expression, 1U);
            output_ << ", " << expression.plan.index_base << ", "
                    << (expression.plan.allow_negative_index ? "true" : "false") << ')';
            break;
          case cpp::lir::IndexForm::matrix_linear:
            output_ << "mpf_runtime::matrix_linear_index(";
            emit_expression(expression.children[0]);
            output_ << ", static_cast<std::int64_t>(";
            emit_expression(expression.children[1]);
            output_ << "), " << expression.plan.index_base << ')';
            break;
          case cpp::lir::IndexForm::linear_element:
            output_ << "mpf_runtime::linear_index_column_major(";
            emit_expression(expression.children[0]);
            output_ << ", ";
            emit_selector(expression, 1U);
            output_ << ", " << expression.plan.index_base << ", "
                    << (expression.plan.allow_negative_index ? "true" : "false") << ')';
            break;
          case cpp::lir::IndexForm::nested:
            for (std::size_t index = 1; index < expression.children.size(); ++index) {
              output_ << "mpf_runtime::index(";
            }
            emit_expression(expression.children[0]);
            for (std::size_t index = 1; index < expression.children.size(); ++index) {
              output_ << ", static_cast<std::int64_t>(";
              emit_expression(expression.children[index]);
              output_ << "), " << expression.plan.index_base << ", "
                      << (expression.plan.allow_negative_index ? "true" : "false") << ')';
            }
            break;
          case cpp::lir::IndexForm::none: output_ << '0'; break;
        }
        break;
      case cpp::lir::ExpressionForm::slice: output_ << '0'; break;
      case cpp::lir::ExpressionForm::member:
        if (!expression.children.empty()) emit_expression(expression.children.front(), own);
        output_ << '.' << expression.plan.token;
        break;
      case cpp::lir::ExpressionForm::list:
        output_ << expression.plan.concrete_type << '{';
        for (std::size_t index = 0; index < expression.children.size(); ++index) {
          if (index != 0) output_ << ", ";
          if (index < expression.plan.complex_children.size() &&
              expression.plan.complex_children[index]) {
            output_ << "mpf_runtime::as_complex(";
            emit_expression(expression.children[index]);
            output_ << ')';
          } else if (index < expression.plan.widen_children.size() &&
                     expression.plan.widen_children[index]) {
            output_ << "static_cast<double>(";
            emit_expression(expression.children[index]);
            output_ << ')';
          } else {
            emit_expression(expression.children[index]);
          }
        }
        output_ << '}';
        break;
      case cpp::lir::ExpressionForm::tuple:
        output_ << "std::make_tuple(";
        for (std::size_t index = 0; index < expression.children.size(); ++index) {
          if (index != 0) output_ << ", ";
          emit_expression(expression.children[index]);
        }
        output_ << ')';
        break;
    }
    if (parentheses) output_ << ')';
  }
  void emit_scope_declarations(const cpp::lir::ScopePlan& plan, const std::string& prefix = {}) {
    for (const auto& declaration : plan.declarations) {
      indentation();
      output_ << prefix;
      if (declaration.type_kind == cpp::lir::DeclarationTypeKind::decay_expression) {
        output_ << "std::decay_t<decltype(";
        emit_declaration_type_expression(declaration);
        output_ << ")>";
      } else {
        output_ << declaration.concrete_type;
      }
      output_ << ' ' << mangler_->name(declaration.symbol_id, declaration.name);
      if (!declaration.fixed_shape.empty()) {
        output_ << '(' << declaration.fixed_shape[0];
        for (std::size_t dimension = 1; dimension < declaration.fixed_shape.size(); ++dimension) {
          output_ << ", " << declaration.fixed_nested_types[dimension - 1U] << '('
                  << declaration.fixed_shape[dimension];
        }
        for (std::size_t dimension = 1; dimension < declaration.fixed_shape.size(); ++dimension) {
          output_ << ')';
        }
        output_ << ");\n";
      } else {
        output_ << "{};\n";
      }
    }
  }

  template <typename BaseEmitter>
  void emit_pattern_access(const std::vector<AssignmentAccess>& path, const BaseEmitter& emit_base,
                           const std::size_t depth) {
    if (depth == 0) {
      emit_base();
      return;
    }
    const auto& access = path[depth - 1];
    if (access.list) {
      output_ << '(';
      emit_pattern_access(path, emit_base, depth - 1);
      output_ << ").at(" << access.index << ')';
    } else {
      output_ << "std::get<" << access.index << ">(";
      emit_pattern_access(path, emit_base, depth - 1);
      output_ << ')';
    }
  }

  template <typename BaseEmitter>
  void emit_pattern_access(const std::vector<AssignmentAccess>& path,
                           const BaseEmitter& emit_base) {
    emit_pattern_access(path, emit_base, path.size());
  }

  void emit_declaration_type_expression(const cpp::lir::DeclarationPlan& declaration) {
    const auto& probe = expression(declaration.type_probe);
    const bool previous_type_probe = type_probe_;
    type_probe_ = true;
    if (!declaration.probe_path.empty()) {
      emit_pattern_access(declaration.probe_path, [&] { emit_expression(probe); });
    } else if (declaration.tuple_index != dynamic_extent) {
      if (declaration.probe_sequence_list) {
        output_ << '(';
        emit_expression(probe);
        output_ << ").at(" << declaration.tuple_index << ')';
      } else {
        output_ << "std::get<" << declaration.tuple_index << ">(";
        emit_expression(probe);
        output_ << ')';
      }
    } else {
      emit_expression(probe);
    }
    type_probe_ = previous_type_probe;
  }

  void emit_function_signature(const Statement& statement) {
    std::size_t templated_parameters = 0;
    for (const auto& parameter : statement.function_abi.parameters) {
      if (!parameter.template_parameter.empty()) ++templated_parameters;
    }
    if (templated_parameters != 0) {
      indentation();
      output_ << "template <";
      bool first = true;
      for (std::size_t index = 0; index < statement.function_abi.parameters.size(); ++index) {
        const auto& parameter = statement.function_abi.parameters[index];
        if (parameter.template_parameter.empty()) continue;
        if (!first) output_ << ", ";
        output_ << "typename " << parameter.template_parameter;
        first = false;
      }
      output_ << ">\n";
    }
    indentation();
    output_ << statement.function_abi.return_type << ' '
            << mangler_->name(statement.symbol_id, statement.name) << '(';
    for (std::size_t index = 0; index < statement.parameters.size(); ++index) {
      if (index != 0) output_ << ", ";
      const auto& parameter = statement.function_abi.parameters[index];
      if (parameter.passing == cpp::lir::ParameterPassing::optional_reference) {
        output_ << "mpf_runtime::optional_argument<" << parameter.concrete_type << "> "
                << mangler_->name(index < statement.parameter_symbols.size()
                                      ? statement.parameter_symbols[index]
                                      : SymbolId{},
                                  statement.parameters[index]);
        continue;
      }
      if (parameter.passing == cpp::lir::ParameterPassing::const_reference) output_ << "const ";
      output_ << parameter.template_parameter;
      if (parameter.passing == cpp::lir::ParameterPassing::const_reference ||
          parameter.passing == cpp::lir::ParameterPassing::mutable_reference) {
        output_ << '&';
      }
      output_ << ' '
              << mangler_->name(index < statement.parameter_symbols.size()
                                    ? statement.parameter_symbols[index]
                                    : SymbolId{},
                                statement.parameters[index]);
    }
    output_ << ')';
  }

  void emit_function_declaration(const Statement& statement) {
    emit_function_signature(statement);
    output_ << ";\n";
  }

  void emit_function(const Statement& statement) {
    const auto function_indent = indent_;
    emit_function_signature(statement);
    output_ << " {\n";
    indent_ = function_indent + 1;
    emit_scope_declarations(statement.function_scope);
    for (const auto& child : statement.body) emit_statement(child);
    if (!statement.plan.return_names.empty()) {
      indentation();
      if (statement.plan.return_names.size() == 1) {
        output_ << "return "
                << mangler_->name(statement.return_symbols.empty()
                                      ? SymbolId{}
                                      : statement.return_symbols.front(),
                                  statement.plan.return_names.front())
                << ";\n";
      } else {
        output_ << "return std::make_tuple(";
        for (std::size_t index = 0; index < statement.plan.return_names.size(); ++index) {
          if (index != 0) output_ << ", ";
          output_ << mangler_->name(index < statement.return_symbols.size()
                                        ? statement.return_symbols[index]
                                        : SymbolId{},
                                    statement.plan.return_names[index]);
        }
        output_ << ");\n";
      }
    }
    indent_ = function_indent;
    indentation();
    output_ << "}\n";
  }

  void emit_pattern_leaf_value(const cpp::lir::AssignmentLeafPlan& leaf,
                               const std::string& temporary) {
    if (!leaf.captured_sequence) {
      emit_pattern_access(leaf.access_path, [&] { output_ << temporary; });
      return;
    }
    output_ << leaf.concrete_type << '{';
    for (std::size_t index = 0; index < leaf.captured_paths.size(); ++index) {
      if (index != 0) output_ << ", ";
      if (leaf.widen_elements) output_ << "static_cast<double>(";
      emit_pattern_access(leaf.captured_paths[index], [&] { output_ << temporary; });
      if (leaf.widen_elements) output_ << ')';
    }
    output_ << '}';
  }

  void emit_python_assignment_pattern(const std::vector<cpp::lir::AssignmentLeafPlan>& leaves,
                                      const std::string& temporary) {
    for (const auto& leaf : leaves) {
      indentation();
      output_ << mangler_->name(leaf.name);
      if (leaf.access == cpp::lir::VariableAccess::optional_value) output_ << ".value()";
      output_ << " = ";
      emit_pattern_leaf_value(leaf, temporary);
      output_ << ";\n";
    }
  }

  void emit_case_condition(const Statement& clause, const std::string& selector,
                           const bool character) {
    for (std::size_t index = 0; index < clause.plan.selectors.size(); ++index) {
      if (index != 0) output_ << " || ";
      const auto& value = clause.case_selectors[index];
      const auto form = clause.plan.selectors[index];
      output_ << '(';
      if (form == cpp::lir::SelectorForm::value) {
        if (character) {
          output_ << "mpf_runtime::fortran_compare(" << selector << ", ";
          emit_expression(value.lower);
          output_ << ") == 0";
        } else {
          output_ << selector << " == ";
          emit_expression(value.lower);
        }
      } else {
        const bool has_lower = form == cpp::lir::SelectorForm::closed_range ||
                               form == cpp::lir::SelectorForm::lower_bound;
        const bool has_upper = form == cpp::lir::SelectorForm::closed_range ||
                               form == cpp::lir::SelectorForm::upper_bound;
        if (has_lower) {
          if (character) {
            output_ << "mpf_runtime::fortran_compare(" << selector << ", ";
            emit_expression(value.lower);
            output_ << ") >= 0";
          } else {
            output_ << selector << " >= ";
            emit_expression(value.lower);
          }
        }
        if (has_lower && has_upper) output_ << " && ";
        if (has_upper) {
          if (character) {
            output_ << "mpf_runtime::fortran_compare(" << selector << ", ";
            emit_expression(value.upper);
            output_ << ") <= 0";
          } else {
            output_ << selector << " <= ";
            emit_expression(value.upper);
          }
        }
      }
      output_ << ')';
    }
  }

  void emit_select_case(const Statement& statement) {
    const auto selector = temporary(statement.id, cpp::lir::TemporaryRole::select_value);
    indentation();
    output_ << "{\n";
    ++indent_;
    indentation();
    output_ << "const auto " << selector << " = ";
    emit_expression(statement.expression);
    output_ << ";\n";

    const Statement* default_clause = nullptr;
    bool emitted_condition = false;
    for (const auto& clause : statement.body) {
      if (clause.plan.selectors.empty()) {
        default_clause = &clause;
        continue;
      }
      indentation();
      output_ << (emitted_condition ? "else if (" : "if (");
      emit_case_condition(clause, selector, statement.plan.character_selector);
      output_ << ") {\n";
      ++indent_;
      emit_scope_declarations(clause.body_scope);
      for (const auto& child : clause.body) emit_statement(child);
      --indent_;
      indentation();
      output_ << "}\n";
      emitted_condition = true;
    }
    if (default_clause != nullptr) {
      if (emitted_condition) {
        indentation();
        output_ << "else {\n";
        ++indent_;
        emit_scope_declarations(default_clause->body_scope);
        for (const auto& child : default_clause->body) emit_statement(child);
        --indent_;
        indentation();
        output_ << "}\n";
      } else {
        emit_scope_declarations(default_clause->body_scope);
        for (const auto& child : default_clause->body) emit_statement(child);
      }
    }
    --indent_;
    indentation();
    output_ << "}\n";
  }

  void emit_statement(const Statement& statement) {
    mark(statement.id);
    switch (statement.plan.form) {
      case cpp::lir::StatementForm::discard: break;
      case cpp::lir::StatementForm::declaration_initializer:
        indentation();
        output_ << mangler_->name(statement.symbol_id, statement.name) << " = ";
        emit_expression(statement.expression);
        output_ << ";\n";
        break;
      case cpp::lir::StatementForm::assignment:
        indentation();
        output_ << mangler_->name(statement.symbol_id, statement.name);
        if (statement.plan.target_access == cpp::lir::VariableAccess::optional_value) {
          output_ << ".value()";
        }
        output_ << " = ";
        emit_expression(statement.expression);
        output_ << ";\n";
        break;
      case cpp::lir::StatementForm::multi_pattern:
      case cpp::lir::StatementForm::multi_tuple: {
        const auto temporary =
            this->temporary(statement.id, cpp::lir::TemporaryRole::assignment_value);
        indentation();
        output_ << "const auto " << temporary << " = ";
        emit_expression(statement.expression);
        output_ << ";\n";
        if (statement.plan.form == cpp::lir::StatementForm::multi_pattern) {
          emit_python_assignment_pattern(statement.plan.assignment_leaves, temporary);
        } else {
          for (std::size_t index = 0; index < statement.plan.targets.size(); ++index) {
            indentation();
            output_ << mangler_->name(index < statement.target_symbols.size()
                                          ? statement.target_symbols[index]
                                          : SymbolId{},
                                      statement.plan.targets[index]);
            if (statement.plan.target_accesses[index] == cpp::lir::VariableAccess::optional_value) {
              output_ << ".value()";
            }
            output_ << " = ";
            output_ << "std::get<" << index << ">(" << temporary << ");\n";
          }
        }
        break;
      }
      case cpp::lir::StatementForm::indexed_element_assignment:
      case cpp::lir::StatementForm::indexed_section_assignment:
        indentation();
        if (statement.plan.sparse_mutation.valid()) {
          const auto& sparse = statement.plan.sparse_mutation;
          const auto deletion = semantic::sparse_mutation_is_deletion(sparse.kind);
          if (deletion) {
            output_ << "mpf_runtime::sparse_erase_indexed(";
            emit_expression(statement.target_expression.children[0]);
            output_ << ", ";
            emit_selector_tuple(statement.target_expression);
            output_ << ", " << statement.target_expression.plan.index_base << ", "
                    << (semantic::sparse_mutation_is_linear(sparse.kind) ? "true" : "false") << ", "
                    << statement.plan.indexed_mutation.axis << ", ";
            if (known_static_shape(sparse.result_shape))
              emit_shape_vector(sparse.result_shape);
            else
              output_ << "std::vector<std::size_t>{}";
            output_ << ')';
          } else {
            const auto linear = semantic::sparse_mutation_is_linear(sparse.kind);
            output_ << (linear ? "mpf_runtime::sparse_assign_linear("
                               : "mpf_runtime::sparse_assign_subscripts(");
            emit_expression(statement.target_expression.children[0]);
            output_ << ", ";
            emit_selector(statement.target_expression, 1U);
            if (!linear) {
              output_ << ", ";
              emit_selector(statement.target_expression, 2U);
            }
            output_ << ", ";
            emit_expression(statement.expression);
            output_ << ", " << statement.target_expression.plan.index_base << ", "
                    << (sparse.replacement == semantic::SparseReplacementKind::scalar_expansion
                            ? "true"
                            : "false")
                    << ", ";
            if (sparse.selection_shape.empty()) {
              output_ << "std::nullopt, std::nullopt";
            } else {
              emit_sparse_result_extent(sparse.selection_shape[0]);
              output_ << ", ";
              emit_sparse_result_extent(sparse.selection_shape[1]);
            }
            output_ << ", ";
            if (known_static_shape(sparse.result_shape))
              emit_shape_vector(sparse.result_shape);
            else
              output_ << "std::vector<std::size_t>{}";
            output_ << ')';
          }
        } else if (statement.plan.indexed_mutation.kind == semantic::IndexedMutationKind::grow) {
          output_ << (statement.plan.indexed_mutation.linear
                          ? "mpf_runtime::assign_growing_linear_column_major("
                          : "mpf_runtime::assign_growing_section_nd(");
          emit_expression(statement.target_expression.children[0]);
          output_ << ", ";
          if (statement.plan.indexed_mutation.linear)
            emit_selector(statement.target_expression, 1U);
          else
            emit_selector_tuple(statement.target_expression);
          output_ << ", " << statement.target_expression.plan.index_base << ", "
                  << (statement.target_expression.plan.allow_negative_index ? "true" : "false")
                  << ", ";
          emit_section_replacement(statement.expression, statement.plan.flatten_replacement);
          if (known_static_shape(statement.plan.mutation_input_shape) &&
              known_static_shape(statement.plan.mutation_result_shape)) {
            output_ << ", ";
            emit_shape_vector(statement.plan.mutation_input_shape);
            output_ << ", ";
            emit_shape_vector(statement.plan.mutation_result_shape);
          }
          output_ << ')';
        } else if (statement.plan.indexed_mutation.kind == semantic::IndexedMutationKind::erase) {
          output_ << "mpf_runtime::erase_indexed(";
          emit_expression(statement.target_expression.children[0]);
          output_ << ", ";
          emit_selector_tuple(statement.target_expression);
          output_ << ", " << statement.target_expression.plan.index_base << ", "
                  << (statement.target_expression.plan.allow_negative_index ? "true" : "false")
                  << ", " << (statement.plan.indexed_mutation.linear ? "true" : "false") << ", "
                  << statement.plan.indexed_mutation.axis << ')';
        } else if (statement.plan.form == cpp::lir::StatementForm::indexed_section_assignment) {
          emit_section_assignment(statement.target_expression, statement.expression,
                                  statement.plan.flatten_replacement,
                                  statement.plan.resizable_section);
        } else {
          emit_expression(statement.target_expression);
          output_ << " = ";
          emit_expression(statement.expression);
        }
        output_ << ";\n";
        break;
      case cpp::lir::StatementForm::print_empty:
      case cpp::lir::StatementForm::print_value:
      case cpp::lir::StatementForm::print_tuple:
        indentation();
        output_ << "mpf_runtime::print(";
        if (statement.plan.form == cpp::lir::StatementForm::print_tuple) {
          for (std::size_t index = 0; index < statement.expression.children.size(); ++index) {
            if (index != 0) output_ << ", ";
            emit_expression(statement.expression.children[index]);
          }
        } else if (statement.plan.form == cpp::lir::StatementForm::print_value) {
          emit_expression(statement.expression);
        }
        output_ << ");\n";
        break;
      case cpp::lir::StatementForm::return_void:
      case cpp::lir::StatementForm::return_value:
        indentation();
        output_ << "return";
        if (statement.plan.form == cpp::lir::StatementForm::return_value) {
          output_ << ' ';
          emit_expression(statement.expression);
        }
        output_ << ";\n";
        break;
      case cpp::lir::StatementForm::return_outputs:
        indentation();
        if (statement.plan.return_names.size() == 1U) {
          output_ << "return "
                  << mangler_->name(statement.return_symbols.empty()
                                        ? SymbolId{}
                                        : statement.return_symbols.front(),
                                    statement.plan.return_names.front())
                  << ";\n";
        } else {
          output_ << "return std::make_tuple(";
          for (std::size_t index = 0; index < statement.plan.return_names.size(); ++index) {
            if (index != 0U) output_ << ", ";
            output_ << mangler_->name(index < statement.return_symbols.size()
                                          ? statement.return_symbols[index]
                                          : SymbolId{},
                                      statement.plan.return_names[index]);
          }
          output_ << ");\n";
        }
        break;
      case cpp::lir::StatementForm::return_program:
        indentation();
        output_ << "return 0;\n";
        break;
      case cpp::lir::StatementForm::break_loop:
        indentation();
        if (!loop_completion_flags_.empty() && !loop_completion_flags_.back().empty()) {
          output_ << loop_completion_flags_.back() << " = false;\n";
          indentation();
        }
        output_ << "break;\n";
        break;
      case cpp::lir::StatementForm::continue_loop:
        indentation();
        output_ << "continue;\n";
        break;
      case cpp::lir::StatementForm::expression:
        indentation();
        emit_expression(statement.expression);
        output_ << ";\n";
        break;
      case cpp::lir::StatementForm::implicit_result_value:
        indentation();
        output_ << mangler_->name(statement.symbol_id, statement.name);
        if (statement.plan.target_access == cpp::lir::VariableAccess::optional_value) {
          output_ << ".value()";
        }
        output_ << " = ";
        emit_expression(statement.expression);
        output_ << ";\n";
        break;
      case cpp::lir::StatementForm::implicit_result_discard:
        indentation();
        emit_expression(statement.expression);
        output_ << ";\n";
        break;
      case cpp::lir::StatementForm::conditional:
        indentation();
        output_ << "if (";
        emit_condition(statement.expression, statement.plan.condition);
        output_ << ") {\n";
        ++indent_;
        emit_scope_declarations(statement.body_scope);
        for (const auto& child : statement.body) emit_statement(child);
        --indent_;
        indentation();
        output_ << '}';
        if (statement.plan.has_alternative) {
          output_ << " else {\n";
          ++indent_;
          emit_scope_declarations(statement.alternative_scope);
          for (const auto& child : statement.alternative) emit_statement(child);
          --indent_;
          indentation();
          output_ << '}';
        }
        output_ << '\n';
        break;
      case cpp::lir::StatementForm::selection: emit_select_case(statement); break;
      case cpp::lir::StatementForm::case_clause: break;
      case cpp::lir::StatementForm::while_loop: {
        const auto completion =
            statement.plan.has_alternative
                ? temporary(statement.id, cpp::lir::TemporaryRole::loop_completed)
                : std::string{};
        if (!completion.empty()) {
          indentation();
          output_ << "bool " << completion << " = true;\n";
        }
        indentation();
        output_ << "while (";
        emit_condition(statement.expression, statement.plan.condition);
        output_ << ") {\n";
        ++indent_;
        emit_scope_declarations(statement.body_scope);
        loop_completion_flags_.push_back(completion);
        for (const auto& child : statement.body) emit_statement(child);
        loop_completion_flags_.pop_back();
        --indent_;
        indentation();
        output_ << "}\n";
        if (!completion.empty()) {
          indentation();
          output_ << "if (" << completion << ") {\n";
          ++indent_;
          emit_scope_declarations(statement.alternative_scope);
          for (const auto& child : statement.alternative) emit_statement(child);
          --indent_;
          indentation();
          output_ << "}\n";
        }
        break;
      }
      case cpp::lir::StatementForm::range_loop: {
        const auto start = temporary(statement.id, cpp::lir::TemporaryRole::range_start);
        const auto stop = temporary(statement.id, cpp::lir::TemporaryRole::range_stop);
        const auto step = temporary(statement.id, cpp::lir::TemporaryRole::range_step);
        const auto first = temporary(statement.id, cpp::lir::TemporaryRole::range_first);
        auto variable = mangler_->name(statement.symbol_id, statement.name);
        if (statement.plan.target_access == cpp::lir::VariableAccess::optional_value) {
          variable += ".value()";
        }
        const auto cursor = statement.plan.retain_loop_value
                                ? temporary(statement.id, cpp::lir::TemporaryRole::range_cursor)
                                : variable;
        indentation();
        output_ << "{\n";
        ++indent_;
        emit_scope_declarations(statement.statement_scope);
        indentation();
        output_ << "const auto " << start << " = ";
        emit_expression(statement.expression);
        output_ << ";\n";
        indentation();
        output_ << "const auto " << stop << " = ";
        emit_expression(statement.secondary_expression);
        output_ << ";\n";
        indentation();
        output_ << "const auto " << step << " = ";
        if (statement.plan.range_has_step)
          emit_expression(statement.tertiary_expression);
        else
          output_ << '1';
        output_ << ";\n";
        indentation();
        output_ << "if (" << step
                << " == 0) throw std::invalid_argument(\"MPF range step cannot be zero\");\n";
        const auto completion =
            statement.plan.has_alternative
                ? temporary(statement.id, cpp::lir::TemporaryRole::loop_completed)
                : std::string{};
        if (!completion.empty()) {
          indentation();
          output_ << "bool " << completion << " = true;\n";
        }
        indentation();
        if (statement.plan.retain_loop_value) output_ << "auto ";
        output_ << cursor << " = " << start << ";\n";
        indentation();
        output_ << "bool " << first << " = true;\n";
        indentation();
        output_ << "while (mpf_runtime::range_next(" << cursor << ", " << stop << ", " << step
                << ", " << (statement.plan.inclusive_stop ? "true" : "false") << ", " << first
                << ")) {\n";
        ++indent_;
        emit_scope_declarations(statement.body_scope);
        loop_completion_flags_.push_back(completion);
        if (statement.plan.retain_loop_value) {
          indentation();
          output_ << variable << " = " << cursor << ";\n";
        }
        for (const auto& child : statement.body) emit_statement(child);
        loop_completion_flags_.pop_back();
        --indent_;
        indentation();
        output_ << "}\n";
        if (!completion.empty()) {
          indentation();
          output_ << "if (" << completion << ") {\n";
          ++indent_;
          emit_scope_declarations(statement.alternative_scope);
          for (const auto& child : statement.alternative) emit_statement(child);
          --indent_;
          indentation();
          output_ << "}\n";
        }
        --indent_;
        indentation();
        output_ << "}\n";
        break;
      }
      case cpp::lir::StatementForm::for_loop: {
        auto variable = mangler_->name(statement.symbol_id, statement.name);
        if (statement.plan.target_access == cpp::lir::VariableAccess::optional_value) {
          variable += ".value()";
        }
        indentation();
        output_ << "{\n";
        ++indent_;
        emit_scope_declarations(statement.statement_scope);
        indentation();
        output_ << "for (" << variable << " = ";
        emit_expression(statement.expression);
        output_ << "; ";
        emit_condition(statement.secondary_expression, statement.plan.condition);
        output_ << "; " << variable << " = ";
        emit_expression(statement.tertiary_expression);
        output_ << ") {\n";
        ++indent_;
        emit_scope_declarations(statement.body_scope);
        loop_completion_flags_.push_back({});
        for (const auto& child : statement.body) emit_statement(child);
        loop_completion_flags_.pop_back();
        --indent_;
        indentation();
        output_ << "}\n";
        --indent_;
        indentation();
        output_ << "}\n";
        break;
      }
      case cpp::lir::StatementForm::function: break;
    }
  }

  void emit_condition(const Expression& expression, const cpp::lir::ConditionForm condition) {
    if (condition == cpp::lir::ConditionForm::runtime_truthy) {
      output_ << "mpf_runtime::truthy(";
      emit_expression(expression);
      output_ << ')';
    } else if (condition == cpp::lir::ConditionForm::matlab_all_nonzero) {
      output_ << "mpf_runtime::matlab_truthy(";
      emit_expression(expression);
      output_ << ')';
    } else {
      emit_expression(expression);
    }
  }

  void mark(const LirNodeId node) {
    const auto* segment = source_segments_->find(node);
    if (segment == nullptr) throw std::logic_error("verified cpp LIR source segment is missing");
    if (segment->source.line == 0) return;
    const auto position = output_.tellp();
    if (position < 0) return;
    markers_.push_back({static_cast<std::size_t>(position), segment->source, segment->origin});
  }

  const std::string& temporary(const LirNodeId node, const cpp::lir::TemporaryRole role,
                               const std::size_t ordinal = 0) const {
    const auto* name = temporaries_->find(node, role, ordinal);
    if (name == nullptr) throw std::logic_error("verified cpp LIR temporary is missing");
    return *name;
  }

  std::ostringstream output_;
  std::size_t indent_{0};
  const cpp::lir::TemporaryPlan* temporaries_{nullptr};
  const SourceSegmentPlan* source_segments_{nullptr};
  std::vector<const Expression*> expressions_;
  std::unique_ptr<IdentifierMangler> mangler_;
  std::vector<std::string> loop_completion_flags_;
  std::vector<RenderMarker> markers_;
  bool type_probe_{false};
};

}  // namespace

RenderedOutput render_cpp(const cpp::lir::SemanticProgram& program) {
  return Renderer().render(program);
}

}  // namespace mpf::detail
