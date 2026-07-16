#include "javascript_renderer.hpp"

#include <algorithm>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "identifier_mangler.hpp"
#include "javascript_lir.hpp"
#include "javascript_runtime.hpp"

namespace mpf::detail {
namespace {

using Expression = javascript::lir::Expression;
using Statement = javascript::lir::Statement;
using Program = javascript::lir::SemanticProgram;

bool expression_has_direct_slice(const Expression& expression) {
  return expression.kind == ExpressionKind::index && expression.children.size() > 1 &&
         std::any_of(expression.children.begin() + 1, expression.children.end(),
                     [](const Expression& child) { return child.kind == ExpressionKind::slice; });
}

class Renderer final {
 public:
  RenderedOutput render(const Program& program) {
    emission_ = program.emission;
    temporaries_ = &program.temporaries;
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
    if (expression.kind == ExpressionKind::conditional) return 0;
    if (expression.kind == ExpressionKind::binary) {
      if (expression.value == "||") return 1;
      if (expression.value == "&&") return 2;
      if (expression.value == "===" || expression.value == "!==" || expression.value == "<" ||
          expression.value == "<=" || expression.value == ">" || expression.value == ">=")
        return 3;
      if (expression.value == "+" || expression.value == "-") return 4;
      if (expression.value == "*" || expression.value == "/" || expression.value == "%") return 5;
      if (expression.value == "**") return 7;
    }
    if (expression.kind == ExpressionKind::unary) return 6;
    if (expression.kind == ExpressionKind::call || expression.kind == ExpressionKind::member ||
        expression.kind == ExpressionKind::index)
      return 9;
    return 10;
  }

  void emit_comparison_chain(const Expression& expression) {
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
      const auto& operation = expression.operators[index - 1];
      if (index < expression.children.size() - 1)
        output_ << "if (!(";
      else
        output_ << "return ";
      if (emission_.structural_equality && (operation == "===" || operation == "!==")) {
        if (operation == "!==") output_ << '!';
        output_ << "__mpf_py_equal(" << operands[index - 1] << ", " << operands[index] << ')';
      } else {
        output_ << operands[index - 1] << ' ' << operation << ' ' << operands[index];
      }
      if (index < expression.children.size() - 1)
        output_ << ")) return false; ";
      else
        output_ << "; ";
    }
    output_ << "})()";
  }

  static std::string mapped_identifier(const Expression& expression) {
    return std::string(expression.target_binding.code);
  }

  void emit_expression(const Expression& expression, const int parent_precedence = 0) {
    mark(expression.location, expression.origin);
    if (expression.kind == ExpressionKind::call &&
        std::any_of(expression.argument_transfers.begin(), expression.argument_transfers.end(),
                    [](const ArgumentTransfer transfer) {
                      return argument_transfer_writes(transfer) &&
                             !argument_transfer_forwards_optional(transfer);
                    })) {
      emit_reference_call(expression);
      return;
    }
    if (expression.kind == ExpressionKind::call && expression.multi_output_call &&
        expression.requested_outputs == 1) {
      auto raw_call = expression;
      raw_call.multi_output_call = false;
      output_ << '(';
      emit_expression(raw_call);
      output_ << ")[0]";
      return;
    }
    const auto precedence = expression_precedence(expression);
    const bool parenthesize = precedence < parent_precedence;
    if (parenthesize) output_ << '(';
    switch (expression.kind) {
      case ExpressionKind::invalid:
      case ExpressionKind::omitted_argument: output_ << "undefined"; break;
      case ExpressionKind::identifier:
        if (expression.binding == BindingKind::builtin) {
          output_ << mapped_identifier(expression);
        } else {
          emit_variable_name(expression.value);
        }
        break;
      case ExpressionKind::number_literal:
      case ExpressionKind::string_literal:
      case ExpressionKind::boolean_literal:
      case ExpressionKind::null_literal: output_ << expression.value; break;
      case ExpressionKind::unary:
        if (emission_.dynamic_truthiness && expression.value == "!") {
          output_ << "__mpf_py_not(";
          if (!expression.children.empty()) emit_expression(expression.children.front());
          output_ << ')';
          break;
        }
        output_ << expression.value;
        if (!expression.children.empty() &&
            expression.children.front().kind == ExpressionKind::binary) {
          output_ << '(';
          emit_expression(expression.children.front());
          output_ << ')';
        } else if (!expression.children.empty()) {
          emit_expression(expression.children.front(), precedence);
        }
        break;
      case ExpressionKind::binary:
        if (emission_.operand_logical_result &&
            (expression.value == "&&" || expression.value == "||")) {
          output_ << (expression.value == "&&" ? "__mpf_py_and" : "__mpf_py_or") << "(() => (";
          emit_expression(expression.children[0]);
          output_ << "), () => (";
          emit_expression(expression.children[1]);
          output_ << "))";
          break;
        }
        if (emission_.structural_equality &&
            (expression.value == "===" || expression.value == "!==")) {
          if (expression.value == "!==") output_ << '!';
          output_ << "__mpf_py_equal(";
          emit_expression(expression.children[0]);
          output_ << ", ";
          emit_expression(expression.children[1]);
          output_ << ')';
          break;
        }
        if (expression.value == "//") {
          output_ << "Math.floor(";
          emit_expression(expression.children[0]);
          output_ << " / ";
          emit_expression(expression.children[1]);
          output_ << ')';
          break;
        }
        if (expression.value == "**" && expression.children[0].kind == ExpressionKind::unary) {
          output_ << '(';
          emit_expression(expression.children[0]);
          output_ << ')';
        } else {
          emit_expression(expression.children[0], precedence + (expression.value == "**" ? 1 : 0));
        }
        output_ << ' ' << expression.value << ' ';
        emit_expression(expression.children[1], precedence + (expression.value == "**" ? 0 : 1));
        break;
      case ExpressionKind::comparison_chain: emit_comparison_chain(expression); break;
      case ExpressionKind::conditional:
        output_ << "(__mpf_truthy(";
        emit_expression(expression.children[0]);
        output_ << ") ? (";
        emit_expression(expression.children[1]);
        output_ << ") : (";
        emit_expression(expression.children[2]);
        output_ << "))";
        break;
      case ExpressionKind::call:
        if (!expression.children.empty()) {
          const auto& callee = expression.children.front();
          if (callee.kind == ExpressionKind::identifier) {
            if (callee.binding == BindingKind::builtin &&
                callee.intrinsic == IntrinsicId::python_float && expression.children.size() == 2) {
              output_ << "__mpf_py_float(";
              emit_expression(expression.children[1]);
              output_ << ')';
              break;
            }
            if (callee.binding == BindingKind::builtin &&
                callee.intrinsic == IntrinsicId::python_length && expression.children.size() == 2) {
              output_ << '(';
              emit_expression(expression.children[1]);
              output_ << ").length";
              break;
            }
            if (callee.binding == BindingKind::builtin &&
                callee.intrinsic == IntrinsicId::matlab_length && expression.children.size() == 2) {
              output_ << "__mpf_length(";
              emit_expression(expression.children[1]);
              output_ << ')';
              break;
            }
            if (callee.binding == BindingKind::builtin &&
                callee.intrinsic == IntrinsicId::element_count && expression.children.size() == 2) {
              output_ << "__mpf_numel(";
              emit_expression(expression.children[1]);
              output_ << ')';
              break;
            }
            if (callee.binding == BindingKind::builtin && callee.intrinsic == IntrinsicId::sum &&
                expression.children.size() == 2) {
              output_ << "__mpf_sum(";
              emit_expression(expression.children[1]);
              output_ << ')';
              break;
            }
            if (callee.binding == BindingKind::builtin &&
                callee.intrinsic == IntrinsicId::present && expression.children.size() == 2) {
              output_ << '(' << mangler_->name(expression.children[1].value) << " !== undefined)";
              break;
            }
            if (callee.binding == BindingKind::builtin &&
                callee.intrinsic == IntrinsicId::reshape && expression.children.size() >= 3) {
              output_ << "__mpf_reshape(";
              emit_expression(expression.children[1]);
              output_ << ", ";
              if (expression.children.size() == 3) {
                emit_expression(expression.children[2]);
              } else {
                output_ << '[';
                for (std::size_t dimension = 2; dimension < expression.children.size();
                     ++dimension) {
                  if (dimension != 2) output_ << ", ";
                  emit_expression(expression.children[dimension]);
                }
                output_ << ']';
              }
              output_ << ')';
              break;
            }
            output_ << (callee.binding == BindingKind::builtin ? mapped_identifier(callee)
                                                               : mangler_->name(callee.value));
          } else {
            emit_expression(callee, precedence);
          }
        }
        output_ << '(';
        for (std::size_t index = 1; index < expression.children.size(); ++index) {
          if (index != 1) output_ << ", ";
          const auto intent_index = index - 1;
          if (intent_index < expression.argument_transfers.size() &&
              argument_transfer_forwards_optional(expression.argument_transfers[intent_index]) &&
              expression.children[index].kind == ExpressionKind::identifier) {
            output_ << mangler_->name(expression.children[index].value);
          } else {
            emit_expression(expression.children[index]);
          }
        }
        output_ << ')';
        break;
      case ExpressionKind::index:
        if (std::any_of(
                expression.children.begin() + 1, expression.children.end(),
                [](const Expression& child) { return child.kind == ExpressionKind::slice; })) {
          output_ << "__mpf_section(";
          emit_expression(expression.children[0]);
          output_ << ", [";
          for (std::size_t index = 1; index < expression.children.size(); ++index) {
            if (index != 1) output_ << ", ";
            if (expression.children[index].kind == ExpressionKind::slice) {
              emit_slice_descriptor(expression.children[index]);
            } else {
              output_ << "{ slice: false, value: ";
              emit_expression(expression.children[index]);
              output_ << " }";
            }
          }
          output_ << "], " << expression.index_base << ", "
                  << (expression.allow_negative_index ? "true" : "false") << ", "
                  << (expression.column_major ? "true" : "false") << ')';
        } else {
          output_ << "__mpf_get(";
          emit_expression(expression.children[0]);
          output_ << ", [";
          for (std::size_t index = 1; index < expression.children.size(); ++index) {
            if (index != 1) output_ << ", ";
            emit_expression(expression.children[index]);
          }
          output_ << ']';
          output_ << ", " << expression.index_base << ", "
                  << (expression.allow_negative_index ? "true" : "false") << ", "
                  << (expression.column_major ? "true" : "false") << ')';
        }
        break;
      case ExpressionKind::slice: output_ << "undefined"; break;
      case ExpressionKind::member:
        if (!expression.children.empty()) emit_expression(expression.children.front(), precedence);
        output_ << '.' << expression.value;
        break;
      case ExpressionKind::list:
      case ExpressionKind::tuple:
        output_ << '[';
        for (std::size_t index = 0; index < expression.children.size(); ++index) {
          if (index != 0) output_ << ", ";
          emit_expression(expression.children[index]);
        }
        output_ << ']';
        break;
    }
    if (parenthesize) output_ << ')';
  }

  void emit_variable_name(const std::string& name) {
    output_ << mangler_->name(name);
    if (active_reference_parameters_.count(name) != 0U) output_ << ".value";
  }

  void emit_pattern_access(const std::string& temporary,
                           const std::vector<AssignmentAccess>& path) {
    output_ << temporary;
    for (const auto& access : path) output_ << '[' << access.index << ']';
  }

  void emit_python_assignment_pattern(const AssignmentPattern& pattern,
                                      const std::string& temporary) {
    std::vector<const AssignmentPattern*> leaves;
    collect_assignment_leaves(pattern, leaves);
    for (const auto* leaf : leaves) {
      indentation();
      emit_variable_name(leaf->name);
      output_ << " = ";
      if (leaf->kind == AssignmentPatternKind::name) {
        emit_pattern_access(temporary, leaf->access_path);
      } else {
        output_ << '[';
        for (std::size_t index = 0; index < leaf->captured_paths.size(); ++index) {
          if (index != 0) output_ << ", ";
          emit_pattern_access(temporary, leaf->captured_paths[index]);
        }
        output_ << ']';
      }
      output_ << ";\n";
    }
  }

  void emit_reference_call(const Expression& expression) {
    auto raw_call = expression;
    raw_call.argument_transfers.clear();
    std::vector<std::string> references(expression.children.size());
    output_ << "(() => { ";
    for (std::size_t index = 1; index < expression.children.size(); ++index) {
      const auto intent_index = index - 1;
      const auto transfer = intent_index < expression.argument_transfers.size()
                                ? expression.argument_transfers[intent_index]
                                : ArgumentTransfer::value;
      if (!argument_transfer_writes(transfer)) continue;
      if (expression.children[index].kind == ExpressionKind::omitted_argument ||
          argument_transfer_forwards_optional(transfer)) {
        continue;
      }
      references[index] = temporary(
          expression.id, javascript::lir::TemporaryRole::reference_argument, intent_index);
      output_ << "const " << references[index] << " = { value: ";
      if ((transfer == ArgumentTransfer::mutable_borrow_out ||
           transfer == ArgumentTransfer::copy_out) &&
          expression.children[index].inferred_type != ValueType::list) {
        output_ << "undefined";
      } else {
        emit_expression(expression.children[index]);
      }
      output_ << " }; ";
      raw_call.children[index].kind = ExpressionKind::identifier;
      raw_call.children[index].value = references[index];
      raw_call.children[index].children.clear();
      raw_call.children[index].binding = BindingKind::variable;
    }
    const auto result = temporary(expression.id, javascript::lir::TemporaryRole::call_result);
    output_ << "const " << result << " = ";
    emit_expression(raw_call);
    output_ << "; ";
    for (std::size_t index = 1; index < references.size(); ++index) {
      if (references[index].empty()) continue;
      emit_reference_writeback(expression.children[index], references[index]);
      output_ << "; ";
    }
    output_ << "return " << result << "; })()";
  }

  void emit_reference_writeback(const Expression& target, const std::string& reference) {
    if (expression_has_direct_slice(target)) {
      output_ << "__mpf_set_section(";
      emit_expression(target.children[0]);
      output_ << ", [";
      for (std::size_t index = 1; index < target.children.size(); ++index) {
        if (index != 1) output_ << ", ";
        if (target.children[index].kind == ExpressionKind::slice) {
          emit_slice_descriptor(target.children[index]);
        } else {
          output_ << "{ slice: false, value: ";
          emit_expression(target.children[index]);
          output_ << " }";
        }
      }
      output_ << "], " << reference << ".value, " << target.index_base << ", "
              << (target.allow_negative_index ? "true" : "false") << ", "
              << (target.column_major ? "true" : "false") << ", false)";
      return;
    }
    if (target.kind == ExpressionKind::index) {
      output_ << "__mpf_set(";
      emit_expression(target.children[0]);
      output_ << ", [";
      for (std::size_t index = 1; index < target.children.size(); ++index) {
        if (index != 1) output_ << ", ";
        emit_expression(target.children[index]);
      }
      output_ << "], " << reference << ".value, " << target.index_base << ", "
              << (target.allow_negative_index ? "true" : "false") << ", "
              << (target.column_major ? "true" : "false") << ')';
      return;
    }
    emit_expression(target);
    output_ << " = " << reference << ".value";
  }

  void emit_slice_descriptor(const Expression& slice) {
    output_ << "{ slice: true, start: ";
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
    output_ << ", inclusive: " << (slice.slice_stop_inclusive ? "true" : "false") << " }";
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
      for (const auto& name : plan.declarations) {
        if (!first) {
          output_ << ", ";
        }
        output_ << mangler_->name(name);
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
    for (std::size_t index = 0; index < clause.case_selectors.size(); ++index) {
      if (index != 0) output_ << " || ";
      const auto& value = clause.case_selectors[index];
      output_ << '(';
      if (!value.range) {
        if (character) {
          output_ << "__mpf_fortran_compare(" << selector << ", ";
          emit_expression(value.lower);
          output_ << ") === 0";
        } else {
          output_ << selector << " === ";
          emit_expression(value.lower);
        }
      } else {
        if (value.has_lower) {
          if (character) {
            output_ << "__mpf_fortran_compare(" << selector << ", ";
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
      if (clause.default_case) {
        default_clause = &clause;
        continue;
      }
      indentation();
      output_ << (emitted_condition ? "else if (" : "if (");
      emit_case_condition(clause, selector,
                          statement.expression.inferred_type == ValueType::string);
      output_ << ") {\n";
      ++indent_;
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
        emit_statements(default_clause->body);
        --indent_;
        indentation();
        output_ << "}\n";
      } else {
        emit_statements(default_clause->body);
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
        if (statement.dummy_parameter) break;
        if (statement.has_expression ||
            (statement.declared_type == ValueType::list && !statement.shape.empty())) {
          indentation();
          emit_variable_name(statement.name);
          output_ << " = ";
          if (statement.has_expression) {
            emit_expression(statement.expression);
          } else {
            const char* default_value = "0";
            switch (statement.element_type) {
              case ValueType::boolean: default_value = "false"; break;
              case ValueType::string: default_value = "\"\""; break;
              default: break;
            }
            if (statement.shape.size() == 1) {
              output_ << "new Array(" << statement.shape[0] << ").fill(" << default_value << ')';
            } else {
              output_ << "Array.from({ length: " << statement.shape[0] << " }, () => new Array("
                      << statement.shape[1] << ").fill(" << default_value << "))";
            }
          }
          output_ << ";\n";
        }
        break;
      case StatementKind::assignment:
        indentation();
        emit_variable_name(statement.name);
        output_ << " = ";
        emit_expression(statement.expression);
        output_ << ";\n";
        break;
      case StatementKind::multi_assignment:
        if (statement.has_target_pattern) {
          const auto temporary =
              this->temporary(statement.id, javascript::lir::TemporaryRole::assignment_value);
          indentation();
          output_ << "const " << temporary << " = ";
          emit_expression(statement.expression);
          output_ << ";\n";
          emit_python_assignment_pattern(statement.target_pattern, temporary);
        } else {
          indentation();
          output_ << '[';
          for (std::size_t index = 0; index < statement.target_names.size(); ++index) {
            if (index != 0) output_ << ", ";
            emit_variable_name(statement.target_names[index]);
          }
          output_ << "] = ";
          emit_expression(statement.expression);
          output_ << ";\n";
        }
        break;
      case StatementKind::indexed_assignment:
        indentation();
        if (expression_has_direct_slice(statement.target_expression)) {
          output_ << "__mpf_set_section(";
          emit_expression(statement.target_expression.children[0]);
          output_ << ", [";
          for (std::size_t index = 1; index < statement.target_expression.children.size();
               ++index) {
            if (index != 1) output_ << ", ";
            if (statement.target_expression.children[index].kind == ExpressionKind::slice) {
              emit_slice_descriptor(statement.target_expression.children[index]);
            } else {
              output_ << "{ slice: false, value: ";
              emit_expression(statement.target_expression.children[index]);
              output_ << " }";
            }
          }
          output_ << "], ";
          emit_expression(statement.expression);
          output_ << ", " << statement.target_expression.index_base << ", "
                  << (statement.target_expression.allow_negative_index ? "true" : "false") << ", "
                  << (statement.target_expression.column_major ? "true" : "false") << ", "
                  << (emission_.resizable_sections ? "true" : "false") << ");\n";
        } else {
          output_ << "__mpf_set(";
          emit_expression(statement.target_expression.children[0]);
          output_ << ", [";
          for (std::size_t index = 1; index < statement.target_expression.children.size();
               ++index) {
            if (index != 1) output_ << ", ";
            emit_expression(statement.target_expression.children[index]);
          }
          output_ << "], ";
          emit_expression(statement.expression);
          output_ << ", " << statement.target_expression.index_base << ", "
                  << (statement.target_expression.allow_negative_index ? "true" : "false") << ", "
                  << (statement.target_expression.column_major ? "true" : "false") << ");\n";
        }
        break;
      case StatementKind::print:
        indentation();
        output_ << "console.log(";
        if (statement.has_expression && statement.expression.kind == ExpressionKind::tuple) {
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
        emit_statements(statement.body);
        --indent_;
        indentation();
        output_ << '}';
        if (!statement.alternative.empty()) {
          output_ << " else {\n";
          ++indent_;
          emit_statements(statement.alternative);
          --indent_;
          indentation();
          output_ << '}';
        }
        output_ << "\n";
        break;
      case StatementKind::select_case: emit_select_case(statement); break;
      case StatementKind::case_clause: break;
      case StatementKind::while_loop: {
        const auto completion =
            statement.alternative.empty()
                ? std::string{}
                : temporary(statement.id, javascript::lir::TemporaryRole::loop_completed);
        if (!completion.empty()) {
          indentation();
          output_ << "let " << completion << " = true;\n";
        }
        indentation();
        output_ << "while (";
        emit_condition(statement.expression);
        output_ << ") {\n";
        ++indent_;
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
          emit_statements(statement.alternative);
          --indent_;
          indentation();
          output_ << "}\n";
        }
        break;
      }
      case StatementKind::range_loop: {
        const auto start = temporary(statement.id, javascript::lir::TemporaryRole::range_start);
        const auto stop = temporary(statement.id, javascript::lir::TemporaryRole::range_stop);
        const auto step = temporary(statement.id, javascript::lir::TemporaryRole::range_step);
        auto variable = mangler_->name(statement.name);
        if (active_reference_parameters_.count(statement.name) != 0U) variable += ".value";
        const auto cursor =
            statement.retain_last_loop_value
                ? temporary(statement.id, javascript::lir::TemporaryRole::range_cursor)
                : variable;
        indentation();
        output_ << "{\n";
        ++indent_;
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
        if (statement.has_tertiary_expression)
          emit_expression(statement.tertiary_expression);
        else
          output_ << '1';
        output_ << ";\n";
        indentation();
        output_ << "if (" << step
                << " === 0) throw new RangeError(\"MPF range step cannot be zero\");\n";
        const auto completion =
            statement.alternative.empty()
                ? std::string{}
                : temporary(statement.id, javascript::lir::TemporaryRole::loop_completed);
        if (!completion.empty()) {
          indentation();
          output_ << "let " << completion << " = true;\n";
        }
        indentation();
        output_ << "for (";
        if (statement.retain_last_loop_value) output_ << "let ";
        output_ << cursor << " = " << start;
        output_ << "; " << step << " >= 0 ? " << cursor
                << (statement.inclusive_stop ? " <= " : " < ") << stop << " : " << cursor
                << (statement.inclusive_stop ? " >= " : " > ") << stop << "; " << cursor
                << " += " << step << ") {\n";
        ++indent_;
        loop_completion_flags_.push_back(completion);
        if (statement.retain_last_loop_value) {
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
      case StatementKind::function: {
        const auto saved_reference_parameters = active_reference_parameters_;
        active_reference_parameters_.clear();
        for (std::size_t index = 0; index < statement.parameters.size(); ++index) {
          if (index < statement.function_abi.parameters.size() &&
              statement.function_abi.parameters[index] ==
                  javascript::lir::ParameterPassing::reference_box) {
            active_reference_parameters_.insert(statement.parameters[index]);
          }
        }
        indentation();
        if (statement.function_abi.exported) {
          output_ << "export ";
        }
        output_ << "function " << mangler_->name(statement.name) << '(';
        for (std::size_t index = 0; index < statement.parameters.size(); ++index) {
          if (index != 0) {
            output_ << ", ";
          }
          output_ << mangler_->name(statement.parameters[index]);
          if (emission_.emit_parameter_defaults && index < statement.parameter_defaults.size() &&
              statement.parameter_defaults[index].valid()) {
            output_ << " = ";
            emit_expression(statement.parameter_defaults[index]);
          }
        }
        output_ << ") {\n";
        ++indent_;
        emit_scope_declarations(statement.function_scope);
        emit_statements(statement.body);
        if (!statement.return_names.empty()) {
          indentation();
          if (statement.return_names.size() == 1) {
            output_ << "return " << mangler_->name(statement.return_names.front()) << ";\n";
          } else {
            output_ << "return [";
            for (std::size_t index = 0; index < statement.return_names.size(); ++index) {
              if (index != 0) {
                output_ << ", ";
              }
              output_ << mangler_->name(statement.return_names[index]);
            }
            output_ << "];\n";
          }
        }
        --indent_;
        indentation();
        output_ << "}\n";
        active_reference_parameters_ = saved_reference_parameters;
        break;
      }
    }
  }

  void emit_condition(const Expression& expression) {
    if (emission_.dynamic_truthiness) {
      output_ << "__mpf_truthy(";
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

  const std::string& temporary(const LirNodeId node, const javascript::lir::TemporaryRole role,
                               const std::size_t ordinal = 0) const {
    const auto* name = temporaries_->find(node, role, ordinal);
    if (name == nullptr) throw std::logic_error("verified JavaScript LIR temporary is missing");
    return *name;
  }

  std::ostringstream output_;
  std::size_t indent_{0};
  javascript::lir::EmissionPlan emission_{};
  const javascript::lir::TemporaryPlan* temporaries_{nullptr};
  std::unique_ptr<IdentifierMangler> mangler_;
  std::vector<std::string> loop_completion_flags_;
  std::set<std::string> active_reference_parameters_;
  std::vector<RenderMarker> markers_;
};

}  // namespace

RenderedOutput render_javascript(const javascript::lir::SemanticProgram& program) {
  return Renderer().render(program);
}

}  // namespace mpf::detail
