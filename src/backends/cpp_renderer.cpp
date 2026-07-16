#include "cpp_renderer.hpp"

#include <algorithm>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "cpp_lir.hpp"
#include "cpp_runtime.hpp"
#include "identifier_mangler.hpp"

namespace mpf::detail {
namespace {

using Expression = cpp::lir::Expression;
using Statement = cpp::lir::Statement;
using Program = cpp::lir::SemanticProgram;

bool expression_has_direct_slice(const Expression& expression) {
  return expression.plan.index == cpp::lir::IndexForm::slice ||
         expression.plan.index == cpp::lir::IndexForm::row_slice ||
         expression.plan.index == cpp::lir::IndexForm::column ||
         expression.plan.index == cpp::lir::IndexForm::block ||
         expression.plan.index == cpp::lir::IndexForm::section_nd;
}

class Renderer final {
 public:
  RenderedOutput render(const Program& program) {
    emission_ = program.emission;
    temporaries_ = &program.temporaries;
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
      output_ << "\nint main() { return " << unit.generated_namespace << "::run(); }\n";
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
    if (comparison.form == cpp::lir::ComparisonForm::dynamic_compare) {
      output_ << "mpf_runtime::py_compare(" << left << ", " << right << ", " << comparison.token
              << ')';
      return;
    }
    output_ << left << ' ' << comparison.token << ' ' << right;
  }

  void emit_comparison_chain(const Expression& expression) {
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
    output_ << ", " << slice.index_base << ", " << (slice.allow_negative_index ? "true" : "false")
            << ", " << (slice.slice_stop_inclusive ? "true" : "false");
  }

  void emit_shape_array(const std::vector<std::size_t>& shape) {
    output_ << "std::array<std::size_t, " << shape.size() << ">{";
    for (std::size_t index = 0; index < shape.size(); ++index) {
      if (index != 0) output_ << ", ";
      output_ << shape[index];
    }
    output_ << '}';
  }

  void emit_selector_tuple(const Expression& expression) {
    output_ << "std::make_tuple(";
    for (std::size_t index = 1; index < expression.children.size(); ++index) {
      if (index != 1) output_ << ", ";
      const auto& selector = expression.children[index];
      if (expression.plan.selector_slices[index - 1U]) {
        output_ << "mpf_runtime::slice_selector{";
        emit_slice_bounds(selector);
        output_ << ", " << expression.index_base << ", "
                << (expression.allow_negative_index ? "true" : "false") << ", "
                << (selector.slice_stop_inclusive ? "true" : "false") << '}';
      } else {
        output_ << "mpf_runtime::scalar_selector{static_cast<std::int64_t>(";
        emit_expression(selector);
        output_ << ")}";
      }
    }
    output_ << ')';
  }

  void emit_runtime_index(const Expression& container, const Expression& index_expression,
                          const Expression& index_metadata) {
    output_ << "mpf_runtime::index(";
    emit_expression(container);
    output_ << ", static_cast<std::int64_t>(";
    emit_expression(index_expression);
    output_ << "), " << index_metadata.index_base << ", "
            << (index_metadata.allow_negative_index ? "true" : "false") << ')';
  }

  void emit_row_slice(const Expression& index_expression, const Expression& slice) {
    output_ << "mpf_runtime::slice(";
    emit_expression(index_expression.children[0]);
    output_ << ", ";
    emit_slice_parameters(slice);
    output_ << ')';
  }

  void emit_section_replacement(const Expression& target, const Expression& replacement) {
    if (target.column_major && target.shape.size() == 1 && replacement.shape.size() == 2 &&
        (replacement.shape[0] == 1 || replacement.shape[1] == 1)) {
      output_ << "mpf_runtime::flatten_column_major(";
      emit_expression(replacement);
      output_ << ')';
    } else {
      emit_expression(replacement);
    }
  }

  void emit_section_assignment(const Expression& target, const Expression& replacement) {
    if (target.plan.index == cpp::lir::IndexForm::section_nd) {
      output_ << "mpf_runtime::assign_section_nd(";
      emit_expression(target.children[0]);
      output_ << ", ";
      emit_selector_tuple(target);
      output_ << ", " << target.index_base << ", "
              << (target.allow_negative_index ? "true" : "false") << ", ";
      emit_section_replacement(target, replacement);
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
        output_ << ", " << target.index_base << ", "
                << (slice.slice_stop_inclusive ? "true" : "false") << ", ";
        emit_section_replacement(target, replacement);
        output_ << ')';
      } else {
        output_ << "mpf_runtime::assign_slice(";
        emit_expression(target.children[0]);
        output_ << ", ";
        emit_slice_parameters(slice);
        output_ << ", ";
        emit_section_replacement(target, replacement);
        output_ << ", " << (emission_.resizable_sections ? "true" : "false") << ')';
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
      emit_section_replacement(target, replacement);
      output_ << ", false)";
    } else if (target.plan.index == cpp::lir::IndexForm::column) {
      output_ << "mpf_runtime::assign_column(";
      emit_expression(target.children[0]);
      output_ << ", ";
      emit_slice_bounds(row);
      output_ << ", static_cast<std::int64_t>(";
      emit_expression(column);
      output_ << "), " << target.index_base << ", "
              << (target.allow_negative_index ? "true" : "false") << ", "
              << (row.slice_stop_inclusive ? "true" : "false") << ", ";
      emit_section_replacement(target, replacement);
      output_ << ')';
    } else {
      output_ << "mpf_runtime::assign_block(";
      emit_expression(target.children[0]);
      output_ << ", ";
      emit_slice_bounds(row);
      output_ << ", ";
      emit_slice_bounds(column);
      output_ << ", " << target.index_base << ", "
              << (target.allow_negative_index ? "true" : "false") << ", "
              << (row.slice_stop_inclusive ? "true" : "false") << ", ";
      emit_section_replacement(target, replacement);
      output_ << ')';
    }
  }

  bool has_writable_section_actual(const Expression& expression) const {
    return expression.plan.form == cpp::lir::ExpressionForm::call &&
           std::any_of(expression.plan.call_arguments.begin(), expression.plan.call_arguments.end(),
                       [](const cpp::lir::CallArgumentForm form) {
                         return form == cpp::lir::CallArgumentForm::copy_section;
                       });
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
        output_ << "))";
        return;
      case cpp::lir::CallForm::element_count:
        output_ << "static_cast<std::int64_t>(mpf_runtime::numel(";
        emit_expression(expression.children[1]);
        output_ << "))";
        return;
      case cpp::lir::CallForm::sum:
        output_ << "mpf_runtime::sum(";
        emit_expression(expression.children[1]);
        output_ << ')';
        return;
      case cpp::lir::CallForm::present:
        output_ << '(' << mangler_->name(expression.children[1].plan.token) << ".has_value())";
        return;
      case cpp::lir::CallForm::reshape:
        output_ << "mpf_runtime::reshape_column_major_nd(";
        emit_expression(expression.children[1]);
        output_ << ", ";
        emit_shape_array(expression.children[1].shape);
        output_ << ", ";
        emit_shape_array(expression.shape);
        output_ << ')';
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
                 expression.plan.call_arguments[index - 1U] ==
                     cpp::lir::CallArgumentForm::forward_optional) {
        output_ << mangler_->name(expression.children[index].plan.token);
      } else {
        emit_expression(expression.children[index]);
      }
    }
    output_ << ')';
  }

  void emit_call_value(const Expression& expression,
                       const std::vector<std::string>* replacements = nullptr) {
    if (expression.plan.first_result) output_ << "std::get<0>(";
    emit_direct_call(expression, replacements);
    if (expression.plan.first_result) output_ << ')';
  }

  void emit_section_reference_call(const Expression& expression) {
    std::vector<std::string> temporaries(expression.children.size());
    output_ << "([&]() { ";
    for (std::size_t index = 1; index < expression.children.size(); ++index) {
      const auto intent_index = index - 1;
      if (intent_index >= expression.plan.call_arguments.size() ||
          expression.plan.call_arguments[intent_index] !=
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
    if (expression.procedure_has_result) {
      result = temporary(expression.id, cpp::lir::TemporaryRole::call_result);
      output_ << "auto " << result << " = ";
    }
    emit_call_value(expression, &temporaries);
    output_ << "; ";
    for (std::size_t index = 1; index < temporaries.size(); ++index) {
      if (temporaries[index].empty()) continue;
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
    mark(expression.location, expression.origin);
    if (expression.plan.form == cpp::lir::ExpressionForm::call) {
      if (has_writable_section_actual(expression)) {
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
        output_ << mangler_->name(expression.plan.token);
        if (active_optional_parameters_.count(expression.plan.token) != 0U) {
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
      case cpp::lir::ExpressionForm::unary_operator:
        output_ << expression.plan.token;
        if (!expression.children.empty()) emit_expression(expression.children.front(), own);
        break;
      case cpp::lir::ExpressionForm::binary_lazy_and:
      case cpp::lir::ExpressionForm::binary_lazy_or:
        output_ << (expression.plan.form == cpp::lir::ExpressionForm::binary_lazy_and
                        ? "mpf_runtime::py_and"
                        : "mpf_runtime::py_or")
                << "([&]() { return (";
        emit_expression(expression.children[0]);
        output_ << "); }, [&]() { return (";
        emit_expression(expression.children[1]);
        output_ << "); })";
        break;
      case cpp::lir::ExpressionForm::binary_power:
        output_ << "std::pow(";
        emit_expression(expression.children[0]);
        output_ << ", ";
        emit_expression(expression.children[1]);
        output_ << ')';
        break;
      case cpp::lir::ExpressionForm::binary_floor_divide:
        output_ << "static_cast<std::int64_t>(std::floor(static_cast<double>(";
        emit_expression(expression.children[0]);
        output_ << ") / static_cast<double>(";
        emit_expression(expression.children[1]);
        output_ << ")))";
        break;
      case cpp::lir::ExpressionForm::binary_real_divide:
        output_ << "static_cast<double>(";
        emit_expression(expression.children[0]);
        output_ << ") / static_cast<double>(";
        emit_expression(expression.children[1]);
        output_ << ')';
        break;
      case cpp::lir::ExpressionForm::binary_dynamic_compare:
        output_ << "mpf_runtime::py_compare(";
        emit_expression(expression.children[0]);
        output_ << ", ";
        emit_expression(expression.children[1]);
        output_ << ", " << expression.plan.token << ')';
        break;
      case cpp::lir::ExpressionForm::binary_operator:
        emit_expression(expression.children[0], own);
        output_ << ' ' << expression.plan.token << ' ';
        emit_expression(expression.children[1], own + 1);
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
            output_ << "), " << expression.index_base << ", "
                    << (expression.allow_negative_index ? "true" : "false") << ')';
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
            output_ << ", " << expression.index_base << ", "
                    << (expression.allow_negative_index ? "true" : "false") << ')';
            break;
          case cpp::lir::IndexForm::matrix_linear:
            output_ << "mpf_runtime::matrix_linear_index(";
            emit_expression(expression.children[0]);
            output_ << ", static_cast<std::int64_t>(";
            emit_expression(expression.children[1]);
            output_ << "), " << expression.index_base << ')';
            break;
          case cpp::lir::IndexForm::nested:
            for (std::size_t index = 1; index < expression.children.size(); ++index) {
              output_ << "mpf_runtime::index(";
            }
            emit_expression(expression.children[0]);
            for (std::size_t index = 1; index < expression.children.size(); ++index) {
              output_ << ", static_cast<std::int64_t>(";
              emit_expression(expression.children[index]);
              output_ << "), " << expression.index_base << ", "
                      << (expression.allow_negative_index ? "true" : "false") << ')';
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
          if (index < expression.plan.widen_children.size() &&
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
      output_ << ' ' << mangler_->name(declaration.name);
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
    if (!declaration.probe_path.empty()) {
      emit_pattern_access(declaration.probe_path, [&] { emit_expression(probe); });
      return;
    }
    if (declaration.tuple_index != dynamic_extent) {
      if (declaration.probe_sequence_list) {
        output_ << '(';
        emit_expression(probe);
        output_ << ").at(" << declaration.tuple_index << ')';
      } else {
        output_ << "std::get<" << declaration.tuple_index << ">(";
        emit_expression(probe);
        output_ << ')';
      }
      return;
    }
    emit_expression(probe);
  }

  static const char* cpp_type(const ValueType type) noexcept {
    switch (type) {
      case ValueType::integer: return "std::int64_t";
      case ValueType::real: return "double";
      case ValueType::boolean: return "bool";
      case ValueType::string: return "std::string";
      case ValueType::null_value: return "std::nullptr_t";
      case ValueType::unknown:
      case ValueType::list:
      case ValueType::tuple:
      case ValueType::function: return "double";
    }
    return "double";
  }

  static std::string cpp_container_type(const ValueType element_type,
                                        const std::size_t dimensions) {
    const std::string element = cpp_type(element_type);
    std::string result;
    result.reserve(element.size() + dimensions * std::string_view("std::vector<>").size());
    for (std::size_t dimension = 0; dimension < dimensions; ++dimension) {
      result += "std::vector<";
    }
    result += element;
    result.append(dimensions, '>');
    return result;
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
    output_ << statement.function_abi.return_type << ' ' << mangler_->name(statement.name) << '(';
    for (std::size_t index = 0; index < statement.parameters.size(); ++index) {
      if (index != 0) output_ << ", ";
      const auto& parameter = statement.function_abi.parameters[index];
      if (parameter.passing == cpp::lir::ParameterPassing::optional_reference) {
        output_ << "mpf_runtime::optional_argument<" << parameter.concrete_type << "> "
                << mangler_->name(statement.parameters[index]);
        continue;
      }
      if (parameter.passing == cpp::lir::ParameterPassing::const_reference) output_ << "const ";
      output_ << parameter.template_parameter;
      if (parameter.passing == cpp::lir::ParameterPassing::const_reference ||
          parameter.passing == cpp::lir::ParameterPassing::mutable_reference) {
        output_ << '&';
      }
      output_ << ' ' << mangler_->name(statement.parameters[index]);
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
    const auto saved_optional_parameters = active_optional_parameters_;
    active_optional_parameters_.clear();
    for (std::size_t index = 0; index < statement.parameters.size(); ++index) {
      if (index < statement.function_abi.parameters.size() &&
          statement.function_abi.parameters[index].passing ==
              cpp::lir::ParameterPassing::optional_reference) {
        active_optional_parameters_.insert(statement.parameters[index]);
      }
    }
    indent_ = function_indent + 1;
    emit_scope_declarations(statement.function_scope);
    for (const auto& child : statement.body) emit_statement(child);
    if (!statement.return_names.empty()) {
      indentation();
      if (statement.return_names.size() == 1) {
        output_ << "return " << mangler_->name(statement.return_names.front()) << ";\n";
      } else {
        output_ << "return std::make_tuple(";
        for (std::size_t index = 0; index < statement.return_names.size(); ++index) {
          if (index != 0) output_ << ", ";
          output_ << mangler_->name(statement.return_names[index]);
        }
        output_ << ");\n";
      }
    }
    indent_ = function_indent;
    indentation();
    output_ << "}\n";
    active_optional_parameters_ = saved_optional_parameters;
  }

  void emit_pattern_leaf_value(const AssignmentPattern& leaf, const std::string& temporary) {
    if (leaf.kind == AssignmentPatternKind::name) {
      emit_pattern_access(leaf.access_path, [&] { output_ << temporary; });
      return;
    }
    const auto dimensions = std::max<std::size_t>(1, leaf.shape.size());
    output_ << cpp_container_type(leaf.element_type, dimensions) << '{';
    for (std::size_t index = 0; index < leaf.captured_paths.size(); ++index) {
      if (index != 0) output_ << ", ";
      const bool widen = leaf.element_type == ValueType::real && dimensions == 1;
      if (widen) output_ << "static_cast<double>(";
      emit_pattern_access(leaf.captured_paths[index], [&] { output_ << temporary; });
      if (widen) output_ << ')';
    }
    output_ << '}';
  }

  void emit_python_assignment_pattern(const AssignmentPattern& pattern,
                                      const std::string& temporary) {
    std::vector<const AssignmentPattern*> leaves;
    collect_assignment_leaves(pattern, leaves);
    for (const auto* leaf : leaves) {
      indentation();
      output_ << mangler_->name(leaf->name) << " = ";
      emit_pattern_leaf_value(*leaf, temporary);
      output_ << ";\n";
    }
  }

  void emit_case_condition(const Statement& clause, const std::string& selector,
                           const bool character) {
    for (std::size_t index = 0; index < clause.case_selectors.size(); ++index) {
      if (index != 0) output_ << " || ";
      const auto& value = clause.case_selectors[index];
      output_ << '(';
      if (!value.range) {
        if (character) {
          output_ << "mpf_runtime::fortran_compare(" << selector << ", ";
          emit_expression(value.lower);
          output_ << ") == 0";
        } else {
          output_ << selector << " == ";
          emit_expression(value.lower);
        }
      } else {
        if (value.has_lower) {
          if (character) {
            output_ << "mpf_runtime::fortran_compare(" << selector << ", ";
            emit_expression(value.lower);
            output_ << ") >= 0";
          } else {
            output_ << selector << " >= ";
            emit_expression(value.lower);
          }
        }
        if (value.has_lower && value.has_upper) output_ << " && ";
        if (value.has_upper) {
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
      if (clause.default_case) {
        default_clause = &clause;
        continue;
      }
      indentation();
      output_ << (emitted_condition ? "else if (" : "if (");
      emit_case_condition(clause, selector, statement.expression.plan.string_value);
      output_ << ") {\n";
      ++indent_;
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
        for (const auto& child : default_clause->body) emit_statement(child);
        --indent_;
        indentation();
        output_ << "}\n";
      } else {
        for (const auto& child : default_clause->body) emit_statement(child);
      }
    }
    --indent_;
    indentation();
    output_ << "}\n";
  }

  void emit_statement(const Statement& statement) {
    mark({statement.line, 1}, statement.origin);
    switch (statement.kind) {
      case StatementKind::declaration:
        if (statement.has_expression) {
          indentation();
          output_ << mangler_->name(statement.name) << " = ";
          emit_expression(statement.expression);
          output_ << ";\n";
        }
        break;
      case StatementKind::assignment:
        indentation();
        output_ << mangler_->name(statement.name);
        if (active_optional_parameters_.count(statement.name) != 0U) {
          output_ << ".value()";
        }
        output_ << " = ";
        emit_expression(statement.expression);
        output_ << ";\n";
        break;
      case StatementKind::multi_assignment: {
        const auto temporary =
            this->temporary(statement.id, cpp::lir::TemporaryRole::assignment_value);
        indentation();
        output_ << "const auto " << temporary << " = ";
        emit_expression(statement.expression);
        output_ << ";\n";
        if (statement.has_target_pattern) {
          emit_python_assignment_pattern(statement.target_pattern, temporary);
        } else {
          for (std::size_t index = 0; index < statement.target_names.size(); ++index) {
            indentation();
            output_ << mangler_->name(statement.target_names[index]) << " = ";
            output_ << "std::get<" << index << ">(" << temporary << ");\n";
          }
        }
        break;
      }
      case StatementKind::indexed_assignment:
        indentation();
        if (expression_has_direct_slice(statement.target_expression)) {
          emit_section_assignment(statement.target_expression, statement.expression);
        } else {
          emit_expression(statement.target_expression);
          output_ << " = ";
          emit_expression(statement.expression);
        }
        output_ << ";\n";
        break;
      case StatementKind::print:
        indentation();
        output_ << "mpf_runtime::print(";
        if (statement.has_expression &&
            statement.expression.plan.form == cpp::lir::ExpressionForm::tuple) {
          for (std::size_t index = 0; index < statement.expression.children.size(); ++index) {
            if (index != 0) output_ << ", ";
            emit_expression(statement.expression.children[index]);
          }
        } else if (statement.has_expression) {
          emit_expression(statement.expression);
        }
        output_ << ");\n";
        break;
      case StatementKind::return_statement:
        indentation();
        output_ << "return";
        if (statement.has_expression) {
          output_ << ' ';
          emit_expression(statement.expression);
        }
        output_ << ";\n";
        break;
      case StatementKind::break_statement:
        indentation();
        if (!loop_completion_flags_.empty() && !loop_completion_flags_.back().empty()) {
          output_ << loop_completion_flags_.back() << " = false;\n";
          indentation();
        }
        output_ << "break;\n";
        break;
      case StatementKind::continue_statement:
        indentation();
        output_ << "continue;\n";
        break;
      case StatementKind::expression:
        indentation();
        emit_expression(statement.expression);
        output_ << ";\n";
        break;
      case StatementKind::if_statement:
        indentation();
        output_ << "if (";
        emit_condition(statement.expression);
        output_ << ") {\n";
        ++indent_;
        for (const auto& child : statement.body) emit_statement(child);
        --indent_;
        indentation();
        output_ << '}';
        if (!statement.alternative.empty()) {
          output_ << " else {\n";
          ++indent_;
          for (const auto& child : statement.alternative) emit_statement(child);
          --indent_;
          indentation();
          output_ << '}';
        }
        output_ << '\n';
        break;
      case StatementKind::select_case: emit_select_case(statement); break;
      case StatementKind::case_clause: break;
      case StatementKind::while_loop: {
        const auto completion =
            statement.alternative.empty()
                ? std::string{}
                : temporary(statement.id, cpp::lir::TemporaryRole::loop_completed);
        if (!completion.empty()) {
          indentation();
          output_ << "bool " << completion << " = true;\n";
        }
        indentation();
        output_ << "while (";
        emit_condition(statement.expression);
        output_ << ") {\n";
        ++indent_;
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
          for (const auto& child : statement.alternative) emit_statement(child);
          --indent_;
          indentation();
          output_ << "}\n";
        }
        break;
      }
      case StatementKind::range_loop: {
        const auto start = temporary(statement.id, cpp::lir::TemporaryRole::range_start);
        const auto stop = temporary(statement.id, cpp::lir::TemporaryRole::range_stop);
        const auto step = temporary(statement.id, cpp::lir::TemporaryRole::range_step);
        const auto first = temporary(statement.id, cpp::lir::TemporaryRole::range_first);
        auto variable = mangler_->name(statement.name);
        if (active_optional_parameters_.count(statement.name) != 0U) {
          variable += ".value()";
        }
        const auto cursor = statement.retain_last_loop_value
                                ? temporary(statement.id, cpp::lir::TemporaryRole::range_cursor)
                                : variable;
        indentation();
        output_ << "{\n";
        ++indent_;
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
        if (statement.has_tertiary_expression)
          emit_expression(statement.tertiary_expression);
        else
          output_ << '1';
        output_ << ";\n";
        indentation();
        output_ << "if (" << step
                << " == 0) throw std::invalid_argument(\"MPF range step cannot be zero\");\n";
        const auto completion =
            statement.alternative.empty()
                ? std::string{}
                : temporary(statement.id, cpp::lir::TemporaryRole::loop_completed);
        if (!completion.empty()) {
          indentation();
          output_ << "bool " << completion << " = true;\n";
        }
        indentation();
        if (statement.retain_last_loop_value) output_ << "auto ";
        output_ << cursor << " = " << start << ";\n";
        indentation();
        output_ << "bool " << first << " = true;\n";
        indentation();
        output_ << "while (mpf_runtime::range_next(" << cursor << ", " << stop << ", " << step
                << ", " << (statement.inclusive_stop ? "true" : "false") << ", " << first
                << ")) {\n";
        ++indent_;
        loop_completion_flags_.push_back(completion);
        if (statement.retain_last_loop_value) {
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
      case StatementKind::function: break;
    }
  }

  void emit_condition(const Expression& expression) {
    if (emission_.dynamic_truthiness) {
      output_ << "mpf_runtime::truthy(";
      emit_expression(expression);
      output_ << ')';
    } else {
      emit_expression(expression);
    }
  }

  void mark(const SourceLocation source, const HirNodeId origin) {
    if (source.line == 0) return;
    const auto position = output_.tellp();
    if (position < 0) return;
    markers_.push_back({static_cast<std::size_t>(position), source, origin});
  }

  const std::string& temporary(const LirNodeId node, const cpp::lir::TemporaryRole role,
                               const std::size_t ordinal = 0) const {
    const auto* name = temporaries_->find(node, role, ordinal);
    if (name == nullptr) throw std::logic_error("verified cpp LIR temporary is missing");
    return *name;
  }

  std::ostringstream output_;
  std::size_t indent_{0};
  cpp::lir::EmissionPlan emission_{};
  const cpp::lir::TemporaryPlan* temporaries_{nullptr};
  std::vector<const Expression*> expressions_;
  std::unique_ptr<IdentifierMangler> mangler_;
  std::vector<std::string> loop_completion_flags_;
  std::set<std::string> active_optional_parameters_;
  std::vector<RenderMarker> markers_;
};

}  // namespace

RenderedOutput render_cpp(const cpp::lir::SemanticProgram& program) {
  return Renderer().render(program);
}

}  // namespace mpf::detail
