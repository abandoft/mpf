#include "renderer.hpp"

#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "backends/common/identifier_mangler.hpp"
#include "lir.hpp"
#include "runtime.hpp"

namespace mpf::detail {
namespace {

using Expression = javascript::lir::Expression;
using Statement = javascript::lir::Statement;
using Program = javascript::lir::SemanticProgram;

class Renderer final {
 public:
  RenderedOutput render(const Program& program) {
    temporaries_ = &program.temporaries;
    source_segments_ = &program.source_segments;
    mangler_ = std::make_unique<IdentifierMangler>(program.identifiers);
    output_ << program.module.banner;
    for (const auto& directive : program.module.directives) output_ << directive << '\n';
    for (const auto fragment : program.module.runtime_fragments) emit_runtime(fragment);
    emit_scope_declarations(program.program_scope);
    for (const auto index : program.module.body_order) emit_statement(program.statements[index]);
    return {output_.str(), std::move(markers_)};
  }

 private:
  void emit_runtime(const javascript::lir::RuntimeFragment fragment) {
    emit_javascript_runtime_fragment(output_, fragment);
  }

  static int expression_precedence(const Expression& expression) noexcept {
    return expression.plan.precedence;
  }

  static bool binary_form(const javascript::lir::ExpressionForm form) noexcept {
    return form == javascript::lir::ExpressionForm::binary_operator ||
           form == javascript::lir::ExpressionForm::binary_lazy_and ||
           form == javascript::lir::ExpressionForm::binary_lazy_or ||
           form == javascript::lir::ExpressionForm::matlab_short_circuit_and ||
           form == javascript::lir::ExpressionForm::matlab_short_circuit_or ||
           form == javascript::lir::ExpressionForm::binary_comparison ||
           form == javascript::lir::ExpressionForm::binary_reverse_divide;
  }

  void emit_shape(const std::vector<std::size_t>& shape) {
    output_ << '[';
    for (std::size_t axis = 0; axis < shape.size(); ++axis) {
      if (axis != 0U) output_ << ", ";
      output_ << shape[axis];
    }
    output_ << ']';
  }

  void emit_optional_shape(const std::vector<std::size_t>& shape) {
    for (const auto extent : shape) {
      if (extent == dynamic_extent) {
        output_ << "undefined";
        return;
      }
    }
    emit_shape(shape);
  }

  void emit_sparse_result_shape(const std::vector<std::size_t>& shape) {
    output_ << '[';
    for (std::size_t axis = 0; axis < shape.size(); ++axis) {
      if (axis != 0U) output_ << ", ";
      if (shape[axis] == dynamic_extent)
        output_ << "null";
      else
        output_ << shape[axis];
    }
    output_ << ']';
  }

  void emit_sparse_index(const Expression& expression) {
    const auto kind = expression.plan.sparse_index.kind;
    switch (kind) {
      case semantic::SparseIndexKind::linear_element:
        output_ << "__mpf_sparse_linear_element(";
        break;
      case semantic::SparseIndexKind::subscript_element:
        output_ << "__mpf_sparse_subscript_element(";
        break;
      case semantic::SparseIndexKind::linear_selection:
        output_ << "__mpf_sparse_linear_selection(";
        break;
      case semantic::SparseIndexKind::submatrix_selection:
        output_ << "__mpf_sparse_submatrix_selection(";
        break;
      case semantic::SparseIndexKind::none:
        throw std::logic_error("verified JavaScript sparse index plan is missing");
    }
    emit_expression(expression.children.front());
    for (std::size_t index = 1U; index < expression.children.size(); ++index) {
      output_ << ", ";
      emit_selector_descriptor(expression, index);
    }
    if (kind == semantic::SparseIndexKind::linear_selection ||
        kind == semantic::SparseIndexKind::submatrix_selection) {
      output_ << ", ";
      emit_sparse_result_shape(expression.plan.sparse_index.result_shape);
    }
    output_ << ')';
  }

  void emit_comparison(const javascript::lir::ComparisonPlan& comparison, const std::string& left,
                       const std::string& right) {
    switch (comparison.form) {
      case javascript::lir::ComparisonForm::structural_equal:
      case javascript::lir::ComparisonForm::structural_not_equal:
        if (comparison.form == javascript::lir::ComparisonForm::structural_not_equal) {
          output_ << '!';
        }
        output_ << "__mpf_py_equal(" << left << ", " << right << ')';
        return;
      case javascript::lir::ComparisonForm::identity:
      case javascript::lir::ComparisonForm::not_identity:
        if (comparison.form == javascript::lir::ComparisonForm::not_identity) output_ << '!';
        output_ << "__mpf_py_is(" << left << ", " << right << ')';
        return;
      case javascript::lir::ComparisonForm::membership:
      case javascript::lir::ComparisonForm::not_membership:
        if (comparison.form == javascript::lir::ComparisonForm::not_membership) output_ << '!';
        output_ << "__mpf_py_contains(" << right << ", " << left << ')';
        return;
      case javascript::lir::ComparisonForm::infix: break;
    }
    output_ << left << ' ' << comparison.token << ' ' << right;
  }

  void emit_binary_comparison(const Expression& expression) {
    const auto& comparison = expression.plan.comparisons.front();
    const bool negate = comparison.form == javascript::lir::ComparisonForm::structural_not_equal ||
                        comparison.form == javascript::lir::ComparisonForm::not_identity ||
                        comparison.form == javascript::lir::ComparisonForm::not_membership;
    if (negate) output_ << '!';
    if (comparison.form == javascript::lir::ComparisonForm::structural_equal ||
        comparison.form == javascript::lir::ComparisonForm::structural_not_equal) {
      output_ << "__mpf_py_equal(";
    } else if (comparison.form == javascript::lir::ComparisonForm::identity ||
               comparison.form == javascript::lir::ComparisonForm::not_identity) {
      output_ << "__mpf_py_is(";
    } else if (comparison.form == javascript::lir::ComparisonForm::membership ||
               comparison.form == javascript::lir::ComparisonForm::not_membership) {
      output_ << "__mpf_py_contains(";
      emit_expression(expression.children[1]);
      output_ << ", ";
      emit_expression(expression.children[0]);
      output_ << ')';
      return;
    } else {
      emit_expression(expression.children[0], expression.plan.precedence);
      output_ << ' ' << comparison.token << ' ';
      emit_expression(expression.children[1], expression.plan.precedence + 1);
      return;
    }
    emit_expression(expression.children[0]);
    output_ << ", ";
    emit_expression(expression.children[1]);
    output_ << ')';
  }

  void emit_comparison_chain(const Expression& expression) {
    if (expression.plan.evaluation != javascript::lir::EvaluationForm::comparison_arrow_iife) {
      throw std::logic_error("verified JavaScript comparison evaluation plan is missing");
    }
    std::vector<std::string> operands;
    operands.reserve(expression.children.size());
    output_ << "(() => { ";
    for (std::size_t index = 0; index < expression.children.size(); ++index) {
      operands.push_back(
          temporary(expression.id, javascript::lir::TemporaryRole::comparison_operand, index));
      output_ << "const " << operands.back() << " = ";
      emit_expression(expression.children[index]);
      output_ << "; ";
      if (index == 0) continue;
      if (index < expression.children.size() - 1)
        output_ << "if (!(";
      else
        output_ << "return ";
      emit_comparison(expression.plan.comparisons[index - 1U], operands[index - 1U],
                      operands[index]);
      if (index < expression.children.size() - 1)
        output_ << ")) return false; ";
      else
        output_ << "; ";
    }
    output_ << "})()";
  }

  void emit_direct_call(const Expression& expression,
                        const std::vector<std::string>* replacements = nullptr) {
    const auto& plan = expression.plan;
    switch (plan.call) {
      case javascript::lir::CallForm::complex_absolute:
        output_ << "__mpf_abs(";
        emit_expression(expression.children[1]);
        output_ << ')';
        return;
      case javascript::lir::CallForm::python_float:
        output_ << "__mpf_py_float(";
        emit_expression(expression.children[1]);
        output_ << ')';
        return;
      case javascript::lir::CallForm::python_length:
        output_ << '(';
        emit_expression(expression.children[1]);
        output_ << ").length";
        return;
      case javascript::lir::CallForm::matlab_length:
        output_ << "__mpf_length(";
        emit_expression(expression.children[1]);
        output_ << ')';
        return;
      case javascript::lir::CallForm::element_count:
        output_ << "__mpf_numel(";
        emit_expression(expression.children[1]);
        output_ << ')';
        return;
      case javascript::lir::CallForm::matlab_all:
      case javascript::lir::CallForm::matlab_any: {
        const auto& reduction = plan.reduction;
        const bool all = plan.call == javascript::lir::CallForm::matlab_all;
        if (reduction.shape_source == semantic::ReductionShapeSource::runtime_operand) {
          output_ << "__mpf_matlab_logical_total(";
          emit_expression(expression.children[1]);
          output_ << ", " << (all ? "true" : "false") << ')';
          return;
        }
        output_ << "__mpf_matlab_logical_reduce(";
        emit_expression(expression.children[1]);
        output_ << ", " << (all ? "true" : "false") << ", ";
        emit_shape(reduction.axes);
        output_ << ", ";
        emit_shape(reduction.input_shape);
        output_ << ", ";
        emit_shape(reduction.result_shape);
        output_ << ", ";
        emit_shape(reduction.output_shape);
        output_ << ", " << (reduction.scalar_result ? "true" : "false") << ')';
        return;
      }
      case javascript::lir::CallForm::sum:
        output_ << "__mpf_sum(";
        emit_expression(expression.children[1]);
        output_ << ')';
        return;
      case javascript::lir::CallForm::present:
        output_ << '('
                << mangler_->name(expression.children[1].symbol_id,
                                  expression.children[1].plan.token)
                << " !== undefined)";
        return;
      case javascript::lir::CallForm::reshape:
        output_ << "__mpf_reshape(";
        emit_expression(expression.children[1]);
        output_ << ", ";
        emit_shape(plan.result_shape);
        output_ << ')';
        return;
      case javascript::lir::CallForm::matlab_sparse_reshape:
        output_ << "__mpf_sparse_reshape(";
        emit_expression(expression.children[1]);
        output_ << ", ";
        emit_shape(plan.sparse_reshape.input_shape);
        output_ << ", ";
        emit_shape(plan.sparse_reshape.requested_shape);
        output_ << ", ";
        emit_shape(plan.sparse_reshape.result_shape);
        output_ << ')';
        return;
      case javascript::lir::CallForm::matlab_sparse:
        output_ << "__mpf_sparse(";
        for (std::size_t index = 1U; index < expression.children.size(); ++index) {
          if (index != 1U) output_ << ", ";
          emit_expression(expression.children[index]);
        }
        output_ << ')';
        return;
      case javascript::lir::CallForm::matlab_full:
        output_ << "__mpf_full(";
        emit_expression(expression.children[1]);
        output_ << ')';
        return;
      case javascript::lir::CallForm::matlab_is_sparse:
        output_ << "__mpf_issparse(";
        emit_expression(expression.children[1]);
        output_ << ')';
        return;
      case javascript::lir::CallForm::matlab_nonzero_count:
        output_ << "__mpf_nnz(";
        emit_expression(expression.children[1]);
        output_ << ')';
        return;
      case javascript::lir::CallForm::none:
      case javascript::lir::CallForm::direct: break;
    }
    if (!expression.children.empty()) emit_expression(expression.children.front(), plan.precedence);
    output_ << '(';
    for (std::size_t index = 1; index < expression.children.size(); ++index) {
      if (index != 1) output_ << ", ";
      if (replacements != nullptr && index < replacements->size() &&
          !(*replacements)[index].empty()) {
        output_ << (*replacements)[index];
      } else if (index - 1U < plan.call_arguments.size() &&
                 plan.call_arguments[index - 1U].form ==
                     javascript::lir::CallArgumentForm::forward_optional) {
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
    if (expression.plan.call_value == javascript::lir::CallValueForm::first_result) output_ << '(';
    emit_direct_call(expression, replacements);
    if (expression.plan.call_value == javascript::lir::CallValueForm::first_result)
      output_ << ")[0]";
  }

  void emit_expression(const Expression& expression, const int parent_precedence = 0) {
    mark(expression.id);
    if (expression.plan.form == javascript::lir::ExpressionForm::call) {
      if (expression.plan.evaluation == javascript::lir::EvaluationForm::writable_call_arrow_iife) {
        emit_reference_call(expression);
      } else {
        emit_call_value(expression);
      }
      return;
    }
    const auto precedence = expression_precedence(expression);
    const bool parenthesize = precedence < parent_precedence;
    if (parenthesize) output_ << '(';
    switch (expression.plan.form) {
      case javascript::lir::ExpressionForm::invalid:
      case javascript::lir::ExpressionForm::omitted: output_ << expression.plan.token; break;
      case javascript::lir::ExpressionForm::variable:
        emit_variable_name(expression.symbol_id, expression.plan.token,
                           expression.plan.variable_access);
        break;
      case javascript::lir::ExpressionForm::target_symbol:
      case javascript::lir::ExpressionForm::literal: output_ << expression.plan.token; break;
      case javascript::lir::ExpressionForm::unary_truthiness:
        output_ << "__mpf_py_not(";
        if (!expression.children.empty()) emit_expression(expression.children.front());
        output_ << ')';
        break;
      case javascript::lir::ExpressionForm::matlab_logical_not:
        output_ << expression.plan.token << '(';
        if (!expression.children.empty()) emit_expression(expression.children.front());
        output_ << ')';
        break;
      case javascript::lir::ExpressionForm::unary_operator:
        output_ << expression.plan.token;
        if (!expression.children.empty() && binary_form(expression.children.front().plan.form)) {
          output_ << '(';
          emit_expression(expression.children.front());
          output_ << ')';
        } else if (!expression.children.empty()) {
          emit_expression(expression.children.front(), precedence);
        }
        break;
      case javascript::lir::ExpressionForm::matlab_transpose:
      case javascript::lir::ExpressionForm::matlab_sparse_transpose:
      case javascript::lir::ExpressionForm::unary_runtime_call:
        output_ << expression.plan.token << '(';
        emit_expression(expression.children.front());
        output_ << ')';
        break;
      case javascript::lir::ExpressionForm::binary_lazy_and:
      case javascript::lir::ExpressionForm::binary_lazy_or:
        if (expression.plan.evaluation != javascript::lir::EvaluationForm::lazy_arrow_thunks) {
          throw std::logic_error("verified JavaScript lazy evaluation plan is missing");
        }
        output_ << (expression.plan.form == javascript::lir::ExpressionForm::binary_lazy_and
                        ? "__mpf_py_and"
                        : "__mpf_py_or")
                << "(() => (";
        emit_expression(expression.children[0]);
        output_ << "), () => (";
        emit_expression(expression.children[1]);
        output_ << "))";
        break;
      case javascript::lir::ExpressionForm::matlab_short_circuit_and:
      case javascript::lir::ExpressionForm::matlab_short_circuit_or:
        output_ << "__mpf_matlab_scalar_logical(";
        emit_expression(expression.children[0]);
        output_ << ") "
                << (expression.plan.form ==
                            javascript::lir::ExpressionForm::matlab_short_circuit_and
                        ? "&&"
                        : "||")
                << " __mpf_matlab_scalar_logical(";
        emit_expression(expression.children[1]);
        output_ << ')';
        break;
      case javascript::lir::ExpressionForm::binary_comparison: {
        emit_binary_comparison(expression);
        break;
      }
      case javascript::lir::ExpressionForm::binary_reverse_divide:
        emit_expression(expression.children[1], precedence);
        output_ << " / ";
        emit_expression(expression.children[0], precedence + 1);
        break;
      case javascript::lir::ExpressionForm::binary_runtime_call: {
        output_ << expression.plan.token << '(';
        emit_expression(expression.children[0]);
        output_ << ", ";
        emit_expression(expression.children[1]);
        output_ << ')';
        break;
      }
      case javascript::lir::ExpressionForm::matlab_logical_operation:
      case javascript::lir::ExpressionForm::matlab_array_operation:
        output_ << expression.plan.token << '(';
        emit_expression(expression.children[0]);
        output_ << ", ";
        emit_expression(expression.children[1]);
        if (expression.plan.broadcast.valid && expression.plan.broadcast.shape_source ==
                                                   semantic::BroadcastShapeSource::static_extents) {
          output_ << ", ";
          emit_shape(expression.plan.broadcast.left_shape);
          output_ << ", ";
          emit_shape(expression.plan.broadcast.right_shape);
          output_ << ", ";
          emit_shape(expression.plan.broadcast.result_shape);
        }
        output_ << ')';
        break;
      case javascript::lir::ExpressionForm::binary_operator: {
        const bool power = expression.plan.token == "**";
        if (power &&
            expression.children[0].plan.form == javascript::lir::ExpressionForm::unary_operator) {
          output_ << '(';
          emit_expression(expression.children[0]);
          output_ << ')';
        } else {
          emit_expression(expression.children[0], precedence + (power ? 1 : 0));
        }
        output_ << ' ' << expression.plan.token << ' ';
        emit_expression(expression.children[1], precedence + (power ? 0 : 1));
        break;
      }
      case javascript::lir::ExpressionForm::comparison_chain:
        emit_comparison_chain(expression);
        break;
      case javascript::lir::ExpressionForm::conditional:
        output_ << "(__mpf_truthy(";
        emit_expression(expression.children[0]);
        output_ << ") ? (";
        emit_expression(expression.children[1]);
        output_ << ") : (";
        emit_expression(expression.children[2]);
        output_ << "))";
        break;
      case javascript::lir::ExpressionForm::call: break;
      case javascript::lir::ExpressionForm::runtime_extent: output_ << expression.plan.token; break;
      case javascript::lir::ExpressionForm::matlab_sparse_index:
        emit_sparse_index(expression);
        break;
      case javascript::lir::ExpressionForm::index:
        if (expression.plan.index == javascript::lir::IndexForm::section) {
          output_ << "__mpf_section(";
          emit_expression(expression.children[0]);
          output_ << ", [";
          for (std::size_t index = 1; index < expression.children.size(); ++index) {
            if (index != 1) output_ << ", ";
            emit_selector_descriptor(expression, index);
          }
          output_ << "], " << expression.plan.index_base << ", "
                  << (expression.plan.allow_negative_index ? "true" : "false") << ", "
                  << (expression.plan.column_major ? "true" : "false") << ", ";
          emit_optional_shape(expression.plan.result_shape);
          output_ << ')';
        } else {
          output_ << "__mpf_get(";
          emit_expression(expression.children[0]);
          output_ << ", [";
          for (std::size_t index = 1; index < expression.children.size(); ++index) {
            if (index != 1) output_ << ", ";
            emit_index_value(expression, index);
          }
          output_ << "], " << expression.plan.index_base << ", "
                  << (expression.plan.allow_negative_index ? "true" : "false") << ", "
                  << (expression.plan.column_major ? "true" : "false") << ')';
        }
        break;
      case javascript::lir::ExpressionForm::slice: output_ << "undefined"; break;
      case javascript::lir::ExpressionForm::member:
        if (!expression.children.empty()) emit_expression(expression.children.front(), precedence);
        output_ << '.' << expression.plan.token;
        break;
      case javascript::lir::ExpressionForm::array:
        if (expression.plan.array_literal.form == javascript::lir::ArrayLiteralForm::shaped_empty) {
          output_ << "__mpf_empty_array(";
          emit_shape(expression.plan.array_literal.shape);
          output_ << ')';
          break;
        }
        output_ << '[';
        for (std::size_t index = 0; index < expression.children.size(); ++index) {
          if (index != 0) output_ << ", ";
          emit_expression(expression.children[index]);
        }
        output_ << ']';
        break;
      case javascript::lir::ExpressionForm::tuple:
        output_ << "__mpf_tuple([";
        for (std::size_t index = 0; index < expression.children.size(); ++index) {
          if (index != 0) output_ << ", ";
          emit_expression(expression.children[index]);
        }
        output_ << "])";
        break;
    }
    if (parenthesize) output_ << ')';
  }
  void emit_variable_name(const std::string& name, const javascript::lir::VariableAccess access =
                                                       javascript::lir::VariableAccess::direct) {
    output_ << mangler_->name(name);
    if (access == javascript::lir::VariableAccess::reference_box_value) output_ << ".value";
  }

  void emit_variable_name(
      const SymbolId symbol, const std::string& name,
      const javascript::lir::VariableAccess access = javascript::lir::VariableAccess::direct) {
    output_ << mangler_->name(symbol, name);
    if (access == javascript::lir::VariableAccess::reference_box_value) output_ << ".value";
  }

  void emit_pattern_access(const std::string& temporary,
                           const std::vector<AssignmentAccess>& path) {
    output_ << temporary;
    for (const auto& access : path) output_ << '[' << access.index << ']';
  }

  void emit_python_assignment_pattern(
      const std::vector<javascript::lir::AssignmentLeafPlan>& leaves,
      const std::string& temporary) {
    for (const auto& leaf : leaves) {
      indentation();
      emit_variable_name(leaf.name, leaf.access);
      output_ << " = ";
      if (!leaf.captured_sequence) {
        emit_pattern_access(temporary, leaf.access_path);
      } else {
        output_ << '[';
        for (std::size_t index = 0; index < leaf.captured_paths.size(); ++index) {
          if (index != 0) output_ << ", ";
          emit_pattern_access(temporary, leaf.captured_paths[index]);
        }
        output_ << ']';
      }
      output_ << ";\n";
    }
  }

  void emit_reference_call(const Expression& expression) {
    std::vector<std::string> references(expression.children.size());
    output_ << "(() => { ";
    for (std::size_t index = 1; index < expression.children.size(); ++index) {
      const auto intent_index = index - 1;
      const auto argument = intent_index < expression.plan.call_arguments.size()
                                ? expression.plan.call_arguments[intent_index]
                                : javascript::lir::CallArgumentPlan{};
      const auto form = argument.form;
      if (form != javascript::lir::CallArgumentForm::reference_box &&
          form != javascript::lir::CallArgumentForm::reference_box_uninitialized) {
        continue;
      }
      references[index] = temporary(
          expression.id, javascript::lir::TemporaryRole::reference_argument, intent_index);
      output_ << "const " << references[index] << " = { value: ";
      if (form == javascript::lir::CallArgumentForm::reference_box_uninitialized) {
        output_ << "undefined";
      } else {
        emit_expression(expression.children[index]);
      }
      output_ << " }; ";
    }
    const auto result = temporary(expression.id, javascript::lir::TemporaryRole::call_result);
    output_ << "const " << result << " = ";
    emit_call_value(expression, &references);
    output_ << "; ";
    for (std::size_t index = 1; index < references.size(); ++index) {
      if (references[index].empty()) continue;
      emit_reference_writeback(expression.children[index], references[index],
                               expression.plan.call_arguments[index - 1U].writeback);
      output_ << "; ";
    }
    output_ << "return " << result << "; })()";
  }

  void emit_reference_writeback(const Expression& target, const std::string& reference,
                                const javascript::lir::WritebackForm writeback) {
    if (writeback == javascript::lir::WritebackForm::section) {
      output_ << "__mpf_set_section(";
      emit_expression(target.children[0]);
      output_ << ", [";
      for (std::size_t index = 1; index < target.children.size(); ++index) {
        if (index != 1) output_ << ", ";
        emit_selector_descriptor(target, index);
      }
      output_ << "], " << reference << ".value, " << target.plan.index_base << ", "
              << (target.plan.allow_negative_index ? "true" : "false") << ", "
              << (target.plan.column_major ? "true" : "false") << ", false)";
      return;
    }
    if (writeback == javascript::lir::WritebackForm::element) {
      output_ << "__mpf_set(";
      emit_expression(target.children[0]);
      output_ << ", [";
      for (std::size_t index = 1; index < target.children.size(); ++index) {
        if (index != 1) output_ << ", ";
        emit_index_value(target, index);
      }
      output_ << "], " << reference << ".value, " << target.plan.index_base << ", "
              << (target.plan.allow_negative_index ? "true" : "false") << ", "
              << (target.plan.column_major ? "true" : "false") << ')';
      return;
    }
    if (writeback == javascript::lir::WritebackForm::direct) {
      emit_expression(target);
      output_ << " = " << reference << ".value";
      return;
    }
    throw std::logic_error("verified JavaScript writable call has no writeback plan");
  }

  void emit_slice_descriptor(const Expression& slice) {
    output_ << "{ kind: \"slice\", start: ";
    if (slice.children[0].valid())
      emit_expression(slice.children[0]);
    else
      output_ << "null";
    output_ << ", stop: ";
    if (slice.children[1].valid())
      emit_expression(slice.children[1]);
    else
      output_ << "null";
    output_ << ", step: ";
    if (slice.children[2].valid())
      emit_expression(slice.children[2]);
    else
      output_ << "null";
    output_ << ", inclusive: " << (slice.plan.inclusive_slice_stop ? "true" : "false") << " }";
  }

  void emit_selector_descriptor(const Expression& index_expression, const std::size_t child_index) {
    const bool runtime_extent =
        semantic::requires_runtime_extent(index_expression.plan.index_extents.at(child_index - 1U));
    if (runtime_extent) output_ << "(__mpf_extent) => (";
    const auto selector = index_expression.plan.index_selectors.at(child_index - 1U);
    if (selector == semantic::IndexSelectorKind::slice) {
      emit_slice_descriptor(index_expression.children[child_index]);
    } else {
      output_ << "{ kind: \"";
      switch (selector) {
        case semantic::IndexSelectorKind::scalar: output_ << "scalar"; break;
        case semantic::IndexSelectorKind::numeric: output_ << "numeric"; break;
        case semantic::IndexSelectorKind::logical: output_ << "logical"; break;
        case semantic::IndexSelectorKind::empty: output_ << "empty"; break;
        case semantic::IndexSelectorKind::slice: break;
      }
      output_ << "\", value: ";
      emit_expression(index_expression.children[child_index]);
      output_ << " }";
    }
    if (runtime_extent) output_ << ')';
  }

  void emit_index_value(const Expression& index_expression, const std::size_t child_index) {
    if (semantic::requires_runtime_extent(
            index_expression.plan.index_extents.at(child_index - 1U))) {
      output_ << "(__mpf_extent) => (";
      emit_expression(index_expression.children[child_index]);
      output_ << ')';
      return;
    }
    emit_expression(index_expression.children[child_index]);
  }

  void indentation() {
    for (std::size_t level = 0; level < indent_; ++level) {
      output_ << "  ";
    }
  }

  void emit_scope_declarations(const javascript::lir::ScopePlan& plan) {
    if (!plan.declarations.empty()) {
      indentation();
      output_ << "let ";
      bool first = true;
      for (const auto& declaration : plan.declarations) {
        if (!first) {
          output_ << ", ";
        }
        output_ << mangler_->name(declaration.symbol, declaration.name);
        first = false;
      }
      output_ << ";\n";
    }
  }

  void emit_statements(const std::vector<Statement>& statements) {
    for (const auto& statement : statements) {
      emit_statement(statement);
    }
  }

  void emit_case_condition(const Statement& clause, const std::string& selector,
                           const bool character) {
    for (std::size_t index = 0; index < clause.plan.selectors.size(); ++index) {
      if (index != 0) output_ << " || ";
      const auto& value = clause.case_selectors[index];
      const auto form = clause.plan.selectors[index];
      output_ << '(';
      if (form == javascript::lir::SelectorForm::value) {
        if (character) {
          output_ << "__mpf_fortran_compare(" << selector << ", ";
          emit_expression(value.lower);
          output_ << ") === 0";
        } else {
          output_ << selector << " === ";
          emit_expression(value.lower);
        }
      } else {
        const bool has_lower = form == javascript::lir::SelectorForm::closed_range ||
                               form == javascript::lir::SelectorForm::lower_bound;
        const bool has_upper = form == javascript::lir::SelectorForm::closed_range ||
                               form == javascript::lir::SelectorForm::upper_bound;
        if (has_lower) {
          if (character) {
            output_ << "__mpf_fortran_compare(" << selector << ", ";
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
            output_ << "__mpf_fortran_compare(" << selector << ", ";
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
    const auto selector = temporary(statement.id, javascript::lir::TemporaryRole::select_value);
    indentation();
    output_ << "{\n";
    ++indent_;
    indentation();
    output_ << "const " << selector << " = ";
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
      emit_statements(clause.body);
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
        emit_statements(default_clause->body);
        --indent_;
        indentation();
        output_ << "}\n";
      } else {
        emit_scope_declarations(default_clause->body_scope);
        emit_statements(default_clause->body);
      }
    }
    --indent_;
    indentation();
    output_ << "}\n";
  }

  void emit_array_initializer(const std::vector<std::size_t>& shape, const std::string& value,
                              const std::size_t dimension = 0) {
    if (dimension + 1U == shape.size()) {
      output_ << "new Array(" << shape[dimension] << ").fill(" << value << ')';
      return;
    }
    output_ << "Array.from({ length: " << shape[dimension] << " }, () => ";
    emit_array_initializer(shape, value, dimension + 1U);
    output_ << ')';
  }

  void emit_statement(const Statement& statement) {
    mark(statement.id);
    switch (statement.plan.form) {
      case javascript::lir::StatementForm::discard: break;
      case javascript::lir::StatementForm::declaration_initializer:
        indentation();
        emit_variable_name(statement.symbol_id, statement.name, statement.plan.target_access);
        output_ << " = ";
        emit_expression(statement.expression);
        output_ << ";\n";
        break;
      case javascript::lir::StatementForm::declaration_array:
        indentation();
        emit_variable_name(statement.symbol_id, statement.name, statement.plan.target_access);
        output_ << " = ";
        emit_array_initializer(statement.plan.array_shape, statement.plan.array_default);
        output_ << ";\n";
        break;
      case javascript::lir::StatementForm::assignment:
        indentation();
        emit_variable_name(statement.symbol_id, statement.name, statement.plan.target_access);
        output_ << " = ";
        emit_expression(statement.expression);
        output_ << ";\n";
        break;
      case javascript::lir::StatementForm::multi_pattern: {
        const auto temporary =
            this->temporary(statement.id, javascript::lir::TemporaryRole::assignment_value);
        indentation();
        output_ << "const " << temporary << " = ";
        emit_expression(statement.expression);
        output_ << ";\n";
        emit_python_assignment_pattern(statement.plan.assignment_leaves, temporary);
        break;
      }
      case javascript::lir::StatementForm::multi_destructure:
        indentation();
        output_ << '[';
        for (std::size_t index = 0; index < statement.plan.targets.size(); ++index) {
          if (index != 0) output_ << ", ";
          emit_variable_name(index < statement.target_symbols.size()
                                 ? statement.target_symbols[index]
                                 : SymbolId{},
                             statement.plan.targets[index], statement.plan.target_accesses[index]);
        }
        output_ << "] = ";
        emit_expression(statement.expression);
        output_ << ";\n";
        break;
      case javascript::lir::StatementForm::indexed_element_assignment:
      case javascript::lir::StatementForm::indexed_section_assignment:
        indentation();
        if (statement.plan.sparse_mutation.valid()) {
          const auto deletion =
              semantic::sparse_mutation_is_deletion(statement.plan.sparse_mutation.kind);
          output_ << (deletion ? "__mpf_sparse_erase(" : "__mpf_sparse_assign(");
          emit_expression(statement.target_expression.children[0]);
          output_ << ", [";
          for (std::size_t index = 1; index < statement.target_expression.children.size();
               ++index) {
            if (index != 1U) output_ << ", ";
            emit_selector_descriptor(statement.target_expression, index);
          }
          output_ << "], ";
          if (!deletion) {
            emit_expression(statement.expression);
            output_ << ", ";
          }
          output_ << statement.target_expression.plan.index_base << ", "
                  << (semantic::sparse_mutation_is_linear(statement.plan.sparse_mutation.kind)
                          ? "true"
                          : "false")
                  << ", ";
          if (deletion) {
            output_ << statement.plan.indexed_mutation.axis << ", ";
          } else {
            output_ << (statement.plan.sparse_mutation.replacement ==
                                semantic::SparseReplacementKind::scalar_expansion
                            ? "true"
                            : "false")
                    << ", ";
            emit_sparse_result_shape(statement.plan.sparse_mutation.selection_shape);
            output_ << ", ";
          }
          emit_optional_shape(statement.plan.sparse_mutation.result_shape);
          output_ << ");\n";
        } else if (statement.plan.indexed_mutation.kind == semantic::IndexedMutationKind::grow ||
                   statement.plan.indexed_mutation.kind == semantic::IndexedMutationKind::erase) {
          output_ << (statement.plan.indexed_mutation.kind == semantic::IndexedMutationKind::grow
                          ? "__mpf_grow("
                          : "__mpf_erase(");
          emit_expression(statement.target_expression.children[0]);
          output_ << ", [";
          for (std::size_t index = 1; index < statement.target_expression.children.size();
               ++index) {
            if (index != 1) output_ << ", ";
            emit_selector_descriptor(statement.target_expression, index);
          }
          output_ << "], ";
          if (statement.plan.indexed_mutation.kind == semantic::IndexedMutationKind::grow) {
            emit_expression(statement.expression);
            output_ << ", ";
          }
          output_ << statement.target_expression.plan.index_base << ", "
                  << (statement.target_expression.plan.allow_negative_index ? "true" : "false")
                  << ", " << (statement.plan.indexed_mutation.linear ? "true" : "false");
          if (statement.plan.indexed_mutation.kind == semantic::IndexedMutationKind::grow) {
            output_ << ", " << statement.plan.array_default;
          } else {
            output_ << ", " << statement.plan.indexed_mutation.axis;
          }
          output_ << ", ";
          emit_optional_shape(statement.plan.mutation_result_shape);
          output_ << ");\n";
        } else if (statement.plan.form ==
                   javascript::lir::StatementForm::indexed_section_assignment) {
          output_ << "__mpf_set_section(";
          emit_expression(statement.target_expression.children[0]);
          output_ << ", [";
          for (std::size_t index = 1; index < statement.target_expression.children.size();
               ++index) {
            if (index != 1) output_ << ", ";
            emit_selector_descriptor(statement.target_expression, index);
          }
          output_ << "], ";
          emit_expression(statement.expression);
          output_ << ", " << statement.target_expression.plan.index_base << ", "
                  << (statement.target_expression.plan.allow_negative_index ? "true" : "false")
                  << ", " << (statement.target_expression.plan.column_major ? "true" : "false")
                  << ", " << (statement.plan.resizable_section ? "true" : "false") << ");\n";
        } else {
          output_ << "__mpf_set(";
          emit_expression(statement.target_expression.children[0]);
          output_ << ", [";
          for (std::size_t index = 1; index < statement.target_expression.children.size();
               ++index) {
            if (index != 1) output_ << ", ";
            emit_index_value(statement.target_expression, index);
          }
          output_ << "], ";
          emit_expression(statement.expression);
          output_ << ", " << statement.target_expression.plan.index_base << ", "
                  << (statement.target_expression.plan.allow_negative_index ? "true" : "false")
                  << ", " << (statement.target_expression.plan.column_major ? "true" : "false")
                  << ");\n";
        }
        break;
      case javascript::lir::StatementForm::print_empty:
      case javascript::lir::StatementForm::print_value:
      case javascript::lir::StatementForm::print_tuple:
        indentation();
        output_ << "console.log(";
        if (statement.plan.form == javascript::lir::StatementForm::print_tuple) {
          for (std::size_t index = 0; index < statement.expression.children.size(); ++index) {
            if (index != 0) output_ << ", ";
            emit_expression(statement.expression.children[index]);
          }
        } else if (statement.plan.form == javascript::lir::StatementForm::print_value) {
          emit_expression(statement.expression);
        }
        output_ << ");\n";
        break;
      case javascript::lir::StatementForm::return_void:
      case javascript::lir::StatementForm::return_value:
        indentation();
        output_ << "return";
        if (statement.plan.form == javascript::lir::StatementForm::return_value) {
          output_ << ' ';
          emit_expression(statement.expression);
        }
        output_ << ";\n";
        break;
      case javascript::lir::StatementForm::break_loop:
        indentation();
        if (!loop_completion_flags_.empty() && !loop_completion_flags_.back().empty()) {
          output_ << loop_completion_flags_.back() << " = false;\n";
          indentation();
        }
        output_ << "break;\n";
        break;
      case javascript::lir::StatementForm::continue_loop:
        indentation();
        output_ << "continue;\n";
        break;
      case javascript::lir::StatementForm::expression:
        indentation();
        emit_expression(statement.expression);
        output_ << ";\n";
        break;
      case javascript::lir::StatementForm::conditional:
        indentation();
        output_ << "if (";
        emit_condition(statement.expression, statement.plan.condition);
        output_ << ") {\n";
        ++indent_;
        emit_scope_declarations(statement.body_scope);
        emit_statements(statement.body);
        --indent_;
        indentation();
        output_ << '}';
        if (statement.plan.has_alternative) {
          output_ << " else {\n";
          ++indent_;
          emit_scope_declarations(statement.alternative_scope);
          emit_statements(statement.alternative);
          --indent_;
          indentation();
          output_ << '}';
        }
        output_ << "\n";
        break;
      case javascript::lir::StatementForm::selection: emit_select_case(statement); break;
      case javascript::lir::StatementForm::case_clause: break;
      case javascript::lir::StatementForm::while_loop: {
        const auto completion =
            statement.plan.has_alternative
                ? temporary(statement.id, javascript::lir::TemporaryRole::loop_completed)
                : std::string{};
        if (!completion.empty()) {
          indentation();
          output_ << "let " << completion << " = true;\n";
        }
        indentation();
        output_ << "while (";
        emit_condition(statement.expression, statement.plan.condition);
        output_ << ") {\n";
        ++indent_;
        emit_scope_declarations(statement.body_scope);
        loop_completion_flags_.push_back(completion);
        emit_statements(statement.body);
        loop_completion_flags_.pop_back();
        --indent_;
        indentation();
        output_ << "}\n";
        if (!completion.empty()) {
          indentation();
          output_ << "if (" << completion << ") {\n";
          ++indent_;
          emit_scope_declarations(statement.alternative_scope);
          emit_statements(statement.alternative);
          --indent_;
          indentation();
          output_ << "}\n";
        }
        break;
      }
      case javascript::lir::StatementForm::range_loop: {
        const auto start = temporary(statement.id, javascript::lir::TemporaryRole::range_start);
        const auto stop = temporary(statement.id, javascript::lir::TemporaryRole::range_stop);
        const auto step = temporary(statement.id, javascript::lir::TemporaryRole::range_step);
        auto variable = mangler_->name(statement.symbol_id, statement.name);
        if (statement.plan.target_access == javascript::lir::VariableAccess::reference_box_value) {
          variable += ".value";
        }
        const auto cursor =
            statement.plan.retain_loop_value
                ? temporary(statement.id, javascript::lir::TemporaryRole::range_cursor)
                : variable;
        indentation();
        output_ << "{\n";
        ++indent_;
        emit_scope_declarations(statement.statement_scope);
        indentation();
        output_ << "const " << start << " = ";
        emit_expression(statement.expression);
        output_ << ";\n";
        indentation();
        output_ << "const " << stop << " = ";
        emit_expression(statement.secondary_expression);
        output_ << ";\n";
        indentation();
        output_ << "const " << step << " = ";
        if (statement.plan.range_has_step)
          emit_expression(statement.tertiary_expression);
        else
          output_ << '1';
        output_ << ";\n";
        indentation();
        output_ << "if (" << step
                << " === 0) throw new RangeError(\"MPF range step cannot be zero\");\n";
        const auto completion =
            statement.plan.has_alternative
                ? temporary(statement.id, javascript::lir::TemporaryRole::loop_completed)
                : std::string{};
        if (!completion.empty()) {
          indentation();
          output_ << "let " << completion << " = true;\n";
        }
        indentation();
        output_ << "for (";
        if (statement.plan.retain_loop_value) output_ << "let ";
        output_ << cursor << " = " << start;
        output_ << "; " << step << " >= 0 ? " << cursor
                << (statement.plan.inclusive_stop ? " <= " : " < ") << stop << " : " << cursor
                << (statement.plan.inclusive_stop ? " >= " : " > ") << stop << "; " << cursor
                << " += " << step << ") {\n";
        ++indent_;
        emit_scope_declarations(statement.body_scope);
        loop_completion_flags_.push_back(completion);
        if (statement.plan.retain_loop_value) {
          indentation();
          output_ << variable << " = " << cursor << ";\n";
        }
        emit_statements(statement.body);
        loop_completion_flags_.pop_back();
        --indent_;
        indentation();
        output_ << "}\n";
        if (!completion.empty()) {
          indentation();
          output_ << "if (" << completion << ") {\n";
          ++indent_;
          emit_scope_declarations(statement.alternative_scope);
          emit_statements(statement.alternative);
          --indent_;
          indentation();
          output_ << "}\n";
        }
        --indent_;
        indentation();
        output_ << "}\n";
        break;
      }
      case javascript::lir::StatementForm::for_loop: {
        auto variable = mangler_->name(statement.symbol_id, statement.name);
        if (statement.plan.target_access == javascript::lir::VariableAccess::reference_box_value) {
          variable += ".value";
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
        emit_statements(statement.body);
        loop_completion_flags_.pop_back();
        --indent_;
        indentation();
        output_ << "}\n";
        --indent_;
        indentation();
        output_ << "}\n";
        break;
      }
      case javascript::lir::StatementForm::function: {
        indentation();
        if (statement.function_abi.exported) {
          output_ << "export ";
        }
        output_ << "function " << mangler_->name(statement.symbol_id, statement.name) << '(';
        for (std::size_t index = 0; index < statement.parameters.size(); ++index) {
          if (index != 0) {
            output_ << ", ";
          }
          output_ << mangler_->name(index < statement.parameter_symbols.size()
                                        ? statement.parameter_symbols[index]
                                        : SymbolId{},
                                    statement.parameters[index]);
          if (index < statement.plan.parameter_defaults.size() &&
              statement.plan.parameter_defaults[index]) {
            output_ << " = ";
            emit_expression(statement.parameter_defaults[index]);
          }
        }
        output_ << ") {\n";
        ++indent_;
        emit_scope_declarations(statement.function_scope);
        emit_statements(statement.body);
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
            output_ << "return [";
            for (std::size_t index = 0; index < statement.plan.return_names.size(); ++index) {
              if (index != 0) {
                output_ << ", ";
              }
              output_ << mangler_->name(index < statement.return_symbols.size()
                                            ? statement.return_symbols[index]
                                            : SymbolId{},
                                        statement.plan.return_names[index]);
            }
            output_ << "];\n";
          }
        }
        --indent_;
        indentation();
        output_ << "}\n";
        break;
      }
    }
  }

  void emit_condition(const Expression& expression,
                      const javascript::lir::ConditionForm condition) {
    if (condition == javascript::lir::ConditionForm::runtime_truthy) {
      output_ << "__mpf_truthy(";
      emit_expression(expression);
      output_ << ')';
    } else if (condition == javascript::lir::ConditionForm::matlab_all_nonzero) {
      output_ << "__mpf_matlab_truthy(";
      emit_expression(expression);
      output_ << ')';
    } else {
      emit_expression(expression);
    }
  }

  void mark(const LirNodeId node) {
    const auto* segment = source_segments_->find(node);
    if (segment == nullptr) {
      throw std::logic_error("verified JavaScript LIR source segment is missing");
    }
    if (segment->source.line == 0) return;
    const auto position = output_.tellp();
    if (position < 0) return;
    markers_.push_back({static_cast<std::size_t>(position), segment->source, segment->origin});
  }

  const std::string& temporary(const LirNodeId node, const javascript::lir::TemporaryRole role,
                               const std::size_t ordinal = 0) const {
    const auto* name = temporaries_->find(node, role, ordinal);
    if (name == nullptr) throw std::logic_error("verified JavaScript LIR temporary is missing");
    return *name;
  }

  std::ostringstream output_;
  std::size_t indent_{0};
  const javascript::lir::TemporaryPlan* temporaries_{nullptr};
  const SourceSegmentPlan* source_segments_{nullptr};
  std::unique_ptr<IdentifierMangler> mangler_;
  std::vector<std::string> loop_completion_flags_;
  std::vector<RenderMarker> markers_;
};

}  // namespace

RenderedOutput render_javascript(const javascript::lir::SemanticProgram& program) {
  return Renderer().render(program);
}

}  // namespace mpf::detail
