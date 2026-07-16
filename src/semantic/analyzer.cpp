#include "analyzer.hpp"

#include <algorithm>
#include <cstdlib>
#include <limits>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "../compiler/function_graph_generic.hpp"

namespace mpf::detail {
namespace {

using Expression = hir::Expression;
using Statement = hir::Statement;
using Program = hir::Program;
using CaseSelector = hir::CaseSelector;

struct Symbol {
  Symbol() = default;
  Symbol(const ValueType symbol_type, const BindingKind symbol_binding, const bool symbol_assigned,
         const ValueType symbol_element_type, std::vector<std::size_t> symbol_shape)
      : type(symbol_type),
        binding(symbol_binding),
        assigned(symbol_assigned),
        element_type(symbol_element_type),
        shape(std::move(symbol_shape)) {}

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

using Scope = std::unordered_map<std::string, Symbol>;

bool numeric(const ValueType type) noexcept {
  return type == ValueType::integer || type == ValueType::real || type == ValueType::unknown;
}

ValueType join_types(const ValueType left, const ValueType right) noexcept {
  if (left == right) return left;
  if (left == ValueType::unknown) return right;
  if (right == ValueType::unknown) return left;
  if (numeric(left) && numeric(right)) return ValueType::real;
  return ValueType::unknown;
}

bool same_metadata(const ValueMetadata& left, const ValueMetadata& right) {
  if (left.type != right.type || left.element_type != right.element_type ||
      left.shape != right.shape || left.sequence != right.sequence ||
      left.list_sequence != right.list_sequence || left.elements.size() != right.elements.size()) {
    return false;
  }
  for (std::size_t index = 0; index < left.elements.size(); ++index) {
    if (!same_metadata(left.elements[index], right.elements[index])) return false;
  }
  return true;
}

bool same_metadata(const std::vector<ValueMetadata>& left,
                   const std::vector<ValueMetadata>& right) {
  if (left.size() != right.size()) return false;
  for (std::size_t index = 0; index < left.size(); ++index) {
    if (!same_metadata(left[index], right[index])) return false;
  }
  return true;
}

ValueMetadata expression_metadata(const Expression& expression) {
  ValueMetadata metadata;
  metadata.type = expression.inferred_type;
  metadata.element_type = expression.element_type;
  metadata.shape = expression.shape;
  metadata.sequence =
      expression.inferred_type == ValueType::tuple || expression.inferred_type == ValueType::list;
  metadata.list_sequence =
      expression.sequence_is_list || expression.inferred_type == ValueType::list;
  metadata.elements = expression.sequence_elements;
  return metadata;
}

std::optional<double> numeric_constant(const Expression& expression) {
  if (expression.kind == ExpressionKind::number_literal) {
    char* end = nullptr;
    const auto value = std::strtod(expression.value.c_str(), &end);
    if (end != expression.value.c_str() && end != nullptr && *end == '\0') return value;
  }
  if (expression.kind == ExpressionKind::unary && expression.children.size() == 1 &&
      (expression.value == "+" || expression.value == "-")) {
    const auto child = numeric_constant(expression.children.front());
    if (child.has_value()) return expression.value == "-" ? -*child : *child;
  }
  return std::nullopt;
}

bool contains_slice(const Expression& expression) {
  if (expression.kind == ExpressionKind::slice) return true;
  return std::any_of(expression.children.begin(), expression.children.end(),
                     [](const Expression& child) { return contains_slice(child); });
}

bool has_direct_slice(const Expression& expression) {
  return expression.kind == ExpressionKind::index && expression.children.size() > 1 &&
         std::any_of(expression.children.begin() + 1, expression.children.end(),
                     [](const Expression& child) { return child.kind == ExpressionKind::slice; });
}

bool safe_python_default(const Expression& expression) {
  switch (expression.kind) {
    case ExpressionKind::number_literal:
    case ExpressionKind::string_literal:
    case ExpressionKind::boolean_literal:
    case ExpressionKind::null_literal: return true;
    case ExpressionKind::unary:
      return expression.children.size() == 1 &&
             (expression.value == "+" || expression.value == "-") &&
             safe_python_default(expression.children.front());
    case ExpressionKind::invalid:
    case ExpressionKind::omitted_argument:
    case ExpressionKind::identifier:
    case ExpressionKind::binary:
    case ExpressionKind::comparison_chain:
    case ExpressionKind::conditional:
    case ExpressionKind::call:
    case ExpressionKind::member:
    case ExpressionKind::index:
    case ExpressionKind::slice:
    case ExpressionKind::list:
    case ExpressionKind::tuple: return false;
  }
  return false;
}

bool safe_fortran_case_constant(const Expression& expression) {
  if (expression.kind == ExpressionKind::number_literal ||
      expression.kind == ExpressionKind::string_literal ||
      expression.kind == ExpressionKind::boolean_literal) {
    return true;
  }
  return expression.kind == ExpressionKind::unary && expression.children.size() == 1 &&
         (expression.value == "+" || expression.value == "-") &&
         expression.children.front().kind == ExpressionKind::number_literal;
}

std::optional<std::string> fortran_string_constant(const Expression& expression) {
  if (expression.kind != ExpressionKind::string_literal || expression.value.size() < 2) {
    return std::nullopt;
  }
  std::string result;
  const auto& value = expression.value;
  for (std::size_t index = 1; index + 1 < value.size(); ++index) {
    if (value[index] == '\\' && index + 2 < value.size()) {
      const auto escaped = value[++index];
      if (escaped == 'n')
        result.push_back('\n');
      else if (escaped == 'r')
        result.push_back('\r');
      else
        result.push_back(escaped);
    } else {
      result.push_back(value[index]);
    }
  }
  return result;
}

int fortran_string_compare(const std::string& left, const std::string& right) {
  const auto width = std::max(left.size(), right.size());
  for (std::size_t index = 0; index < width; ++index) {
    const auto left_character = static_cast<unsigned char>(index < left.size() ? left[index] : ' ');
    const auto right_character =
        static_cast<unsigned char>(index < right.size() ? right[index] : ' ');
    if (left_character < right_character) return -1;
    if (left_character > right_character) return 1;
  }
  return 0;
}

const Expression* root_container(const Expression& expression) {
  const Expression* current = &expression;
  while (current->kind == ExpressionKind::index && !current->children.empty()) {
    current = &current->children.front();
  }
  return current;
}

std::vector<std::size_t> assignment_shape(const std::vector<std::size_t>& shape,
                                          const std::size_t target_rank) {
  if (target_rank != 1 || shape.size() <= 1) return shape;
  std::vector<std::size_t> squeezed;
  for (const auto extent : shape) {
    if (extent != 1) squeezed.push_back(extent);
  }
  if (squeezed.empty() && !shape.empty()) squeezed.push_back(1);
  return squeezed;
}

bool known_shape(const std::vector<std::size_t>& shape) {
  return std::none_of(shape.begin(), shape.end(),
                      [](const std::size_t extent) { return extent == dynamic_extent; });
}

class Analyzer final {
 public:
  explicit Analyzer(Program& program) : program_(program) {}

  std::vector<Diagnostic> analyze() {
    scopes_.emplace_back();
    predeclare(program_.statements);
    const auto function_graph =
        build_function_dependency_graph_generic<Expression, Statement>(program_.statements);
    for (const auto index : function_graph.definition_order) {
      analyze_function(program_.statements[index]);
    }
    bool terminated = false;
    for (auto& statement : program_.statements) {
      if (statement.kind == StatementKind::function) continue;
      if (terminated) {
        warn(statement.line, "MPF2101",
             "statement is unreachable because control flow already terminates");
      }
      terminated = terminated || analyze_statement(statement);
    }
    if (program_.language == SourceLanguage::fortran) {
      refresh_call_intents(program_.statements);
    }
    annotate_types(program_.statements, current());
    return std::move(diagnostics_);
  }

 private:
  void diagnose(const std::size_t line, std::string code, std::string message) {
    diagnostics_.push_back(
        {DiagnosticSeverity::error, std::move(code), std::move(message), {line, 1}});
  }

  void warn(const std::size_t line, std::string code, std::string message) {
    diagnostics_.push_back(
        {DiagnosticSeverity::warning, std::move(code), std::move(message), {line, 1}});
  }

  void predeclare(const std::vector<Statement>& statements) {
    for (const auto& statement : statements) {
      switch (statement.kind) {
        case StatementKind::function:
          current()[statement.name] = {
              ValueType::function, BindingKind::function, true, ValueType::unknown, {}};
          break;
        case StatementKind::declaration:
        case StatementKind::assignment:
          current().try_emplace(statement.name,
                                Symbol{statement.declared_type, BindingKind::variable, false,
                                       statement.element_type, statement.shape});
          break;
        case StatementKind::multi_assignment:
          for (const auto& name : statement.target_names) {
            current().try_emplace(name, Symbol{});
          }
          break;
        case StatementKind::indexed_assignment: break;
        case StatementKind::range_loop:
          current().try_emplace(
              statement.name,
              Symbol{ValueType::integer, BindingKind::variable, false, ValueType::unknown, {}});
          predeclare(statement.body);
          predeclare(statement.alternative);
          break;
        case StatementKind::if_statement:
        case StatementKind::select_case:
        case StatementKind::case_clause:
        case StatementKind::while_loop:
          predeclare(statement.body);
          predeclare(statement.alternative);
          break;
        case StatementKind::print:
        case StatementKind::return_statement:
        case StatementKind::break_statement:
        case StatementKind::continue_statement:
        case StatementKind::expression: break;
      }
    }
  }

  Scope& current() { return scopes_.back(); }

  Symbol* lookup(const std::string& name) {
    for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
      const auto found = scope->find(name);
      if (found != scope->end()) return &found->second;
    }
    return nullptr;
  }

  bool analyze_statements(std::vector<Statement>& statements) {
    bool terminated = false;
    for (auto& statement : statements) {
      if (terminated) {
        warn(statement.line, "MPF2101",
             "statement is unreachable because control flow already terminates");
      }
      const auto statement_terminates = analyze_statement(statement);
      terminated = terminated || statement_terminates;
    }
    return terminated;
  }

  ValueMetadata materialize_sequence(ValueMetadata metadata) const {
    if (!metadata.sequence || !metadata.elements.empty() || metadata.type != ValueType::list ||
        metadata.shape.empty() || metadata.shape.front() == dynamic_extent) {
      return metadata;
    }
    ValueMetadata child;
    if (metadata.shape.size() == 1) {
      child.type = metadata.element_type;
    } else {
      child.type = ValueType::list;
      child.element_type = metadata.element_type;
      child.shape.assign(metadata.shape.begin() + 1, metadata.shape.end());
      child.sequence = true;
      child.list_sequence = true;
      child = materialize_sequence(std::move(child));
    }
    metadata.elements.assign(metadata.shape.front(), child);
    return metadata;
  }

  void bind_assignment_leaf(Statement& statement, AssignmentPattern& leaf,
                            const ValueMetadata& metadata) {
    auto& symbol = current()[leaf.name];
    leaf.type = metadata.type;
    leaf.element_type = metadata.element_type;
    leaf.shape = metadata.shape;
    leaf.previous_type = symbol.type;
    leaf.previous_element_type = symbol.element_type;
    statement.target_previous_types.push_back(symbol.type);
    statement.target_previous_element_types.push_back(symbol.element_type);
    symbol.binding = BindingKind::variable;
    symbol.type = join_types(symbol.type, metadata.type);
    symbol.element_type = join_types(symbol.element_type, metadata.element_type);
    if (symbol.shape.empty()) symbol.shape = metadata.shape;
    symbol.sequence_is_list = metadata.list_sequence;
    symbol.sequence_elements = metadata.elements;
    if (metadata.type == ValueType::tuple) {
      symbol.tuple_types.clear();
      symbol.tuple_element_types.clear();
      symbol.tuple_shapes.clear();
      for (const auto& element : metadata.elements) {
        symbol.tuple_types.push_back(element.type);
        symbol.tuple_element_types.push_back(element.element_type);
        symbol.tuple_shapes.push_back(element.shape);
      }
    }
    symbol.assigned = true;
    statement.target_types.push_back(metadata.type);
    statement.target_element_types.push_back(metadata.element_type);
    statement.target_shapes.push_back(metadata.shape);
  }

  ValueMetadata captured_metadata(const std::vector<ValueMetadata>& elements) const {
    ValueMetadata result;
    result.type = ValueType::list;
    result.sequence = true;
    result.list_sequence = true;
    result.elements = elements;
    result.shape = {elements.size()};
    if (elements.empty()) return result;

    bool all_lists = true;
    auto element_type = ValueType::unknown;
    auto child_shape = elements.front().shape;
    for (const auto& element : elements) {
      all_lists = all_lists && element.type == ValueType::list;
      const auto scalar_type =
          element.type == ValueType::list ? element.element_type : element.type;
      element_type = join_types(element_type, scalar_type);
      if (element.shape != child_shape) child_shape.clear();
    }
    result.element_type = element_type;
    if (all_lists && !child_shape.empty()) {
      result.shape.insert(result.shape.end(), child_shape.begin(), child_shape.end());
    }
    return result;
  }

  bool associate_assignment_pattern(Statement& statement, AssignmentPattern& pattern,
                                    ValueMetadata metadata, std::vector<AssignmentAccess> path) {
    if (pattern.kind == AssignmentPatternKind::name) {
      pattern.access_path = std::move(path);
      bind_assignment_leaf(statement, pattern, metadata);
      return true;
    }
    if (pattern.kind != AssignmentPatternKind::sequence) {
      diagnose(pattern.location.line, "MPF2042",
               "starred assignment target must belong to a sequence pattern");
      return false;
    }

    metadata = materialize_sequence(std::move(metadata));
    if (!metadata.sequence) {
      diagnose(pattern.location.line, "MPF2042",
               "nested Python assignment pattern requires a fixed sequence value");
      return false;
    }
    const auto starred = std::find_if(pattern.children.begin(), pattern.children.end(),
                                      [](const AssignmentPattern& child) {
                                        return child.kind == AssignmentPatternKind::starred_name;
                                      });
    const bool has_starred = starred != pattern.children.end();
    const auto required = pattern.children.size() - (has_starred ? 1U : 0U);
    if ((!has_starred && metadata.elements.size() != required) ||
        (has_starred && metadata.elements.size() < required)) {
      diagnose(pattern.location.line, "MPF2042",
               "Python assignment pattern does not match the fixed sequence length");
      return false;
    }

    const auto star_index =
        has_starred ? static_cast<std::size_t>(std::distance(pattern.children.begin(), starred))
                    : pattern.children.size();
    const auto capture_count = has_starred ? metadata.elements.size() - required : 0U;
    bool valid = true;
    for (std::size_t index = 0; index < pattern.children.size(); ++index) {
      auto& child = pattern.children[index];
      if (child.kind == AssignmentPatternKind::starred_name) {
        std::vector<ValueMetadata> captured;
        captured.reserve(capture_count);
        child.captured_paths.clear();
        for (std::size_t capture = 0; capture < capture_count; ++capture) {
          const auto source_index = star_index + capture;
          captured.push_back(metadata.elements[source_index]);
          auto captured_path = path;
          captured_path.push_back({source_index, metadata.list_sequence});
          child.captured_paths.push_back(std::move(captured_path));
        }
        bind_assignment_leaf(statement, child, captured_metadata(captured));
        continue;
      }
      const auto source_index = index < star_index ? index : index + capture_count - 1U;
      auto child_path = path;
      child_path.push_back({source_index, metadata.list_sequence});
      valid = associate_assignment_pattern(statement, child, metadata.elements[source_index],
                                           std::move(child_path)) &&
              valid;
    }
    return valid;
  }

  bool analyze_statement(Statement& statement) {
    switch (statement.kind) {
      case StatementKind::declaration: {
        auto& symbol = current()[statement.name];
        const bool was_assigned = symbol.assigned;
        const bool is_parameter = !function_parameters_.empty() &&
                                  function_parameters_.back().count(statement.name) != 0U;
        if (program_.language == SourceLanguage::fortran &&
            statement.parameter_intent != ParameterIntent::none && !is_parameter) {
          diagnose(statement.line, "MPF2036",
                   "Fortran INTENT attribute is only valid for a dummy argument");
        }
        if (program_.language == SourceLanguage::fortran && statement.optional_parameter &&
            !is_parameter) {
          diagnose(statement.line, "MPF2040",
                   "Fortran OPTIONAL attribute is only valid for a dummy argument");
        }
        if (program_.language == SourceLanguage::fortran &&
            statement.declared_type == ValueType::list && !statement.dummy_parameter &&
            std::find(statement.shape.begin(), statement.shape.end(), dynamic_extent) !=
                statement.shape.end()) {
          diagnose(statement.line, "MPF2039",
                   "Fortran assumed-shape arrays are only valid as dummy arguments");
        }
        if (program_.language == SourceLanguage::fortran && is_parameter &&
            statement.has_expression) {
          diagnose(statement.line, "MPF2036",
                   "Fortran dummy argument initialization is not valid in a procedure declaration");
        }
        symbol.binding = BindingKind::variable;
        symbol.type = statement.declared_type;
        symbol.element_type = statement.element_type;
        symbol.shape = statement.shape;
        if (statement.has_expression) {
          const auto initializer_type = analyze_expression(statement.expression);
          symbol.type = join_types(symbol.type, initializer_type);
          if (statement.expression.inferred_type == ValueType::list) {
            if (symbol.element_type != ValueType::unknown &&
                statement.expression.element_type != ValueType::unknown &&
                join_types(symbol.element_type, statement.expression.element_type) ==
                    ValueType::unknown) {
              diagnose(statement.line, "MPF2020",
                       "array initializer element type is incompatible with declaration");
            }
            symbol.element_type =
                join_types(symbol.element_type, statement.expression.element_type);
            statement.expression.element_type = symbol.element_type;
            if (!symbol.shape.empty() && !statement.expression.shape.empty() &&
                known_shape(symbol.shape) && known_shape(statement.expression.shape) &&
                symbol.shape != statement.expression.shape) {
              diagnose(statement.line, "MPF2024",
                       "array initializer shape does not match its declared shape");
            }
          }
          symbol.assigned = true;
        } else if (symbol.type == ValueType::list && !symbol.shape.empty()) {
          symbol.assigned = true;
        } else if (is_parameter) {
          symbol.assigned = was_assigned;
        }
        return false;
      }
      case StatementKind::assignment: {
        diagnose_fortran_parameter_write(statement.name, statement.line);
        const auto type = analyze_expression(statement.expression);
        auto& symbol = current()[statement.name];
        symbol.binding = BindingKind::variable;
        statement.previous_type = symbol.type;
        statement.previous_element_type = symbol.element_type;
        const auto joined = join_types(symbol.type, type);
        symbol.type = joined;
        if (type == ValueType::list) {
          const auto joined_element =
              join_types(symbol.element_type, statement.expression.element_type);
          symbol.element_type = joined_element;
          if (program_.language == SourceLanguage::fortran && !symbol.shape.empty() &&
              !statement.expression.shape.empty() && known_shape(symbol.shape) &&
              known_shape(statement.expression.shape) &&
              symbol.shape != statement.expression.shape) {
            diagnose(statement.line, "MPF2024",
                     "Fortran array assignment shape does not match its declared shape");
          }
          if (program_.language != SourceLanguage::fortran || symbol.shape.empty()) {
            symbol.shape = statement.expression.shape;
          }
        } else if (type == ValueType::tuple) {
          if (statement.previous_type == ValueType::unknown) {
            symbol.tuple_types = statement.expression.tuple_types;
            symbol.tuple_element_types = statement.expression.tuple_element_types;
            symbol.tuple_shapes = statement.expression.tuple_shapes;
          } else if (symbol.tuple_types != statement.expression.tuple_types ||
                     symbol.tuple_element_types != statement.expression.tuple_element_types ||
                     symbol.tuple_shapes != statement.expression.tuple_shapes) {
            symbol.tuple_types.clear();
            symbol.tuple_element_types.clear();
            symbol.tuple_shapes.clear();
          }
        }
        if (type == ValueType::list || type == ValueType::tuple) {
          if (statement.previous_type == ValueType::unknown ||
              (symbol.sequence_is_list == statement.expression.sequence_is_list &&
               same_metadata(symbol.sequence_elements, statement.expression.sequence_elements))) {
            symbol.sequence_is_list = statement.expression.sequence_is_list;
            symbol.sequence_elements = statement.expression.sequence_elements;
          } else {
            symbol.sequence_elements.clear();
          }
        }
        symbol.assigned = true;
        return false;
      }
      case StatementKind::multi_assignment: {
        statement.expression.requested_outputs = statement.target_names.size();
        analyze_expression(statement.expression);
        statement.target_types.clear();
        statement.target_element_types.clear();
        statement.target_shapes.clear();
        statement.target_previous_types.clear();
        statement.target_previous_element_types.clear();
        statement.target_types.reserve(statement.target_names.size());
        statement.target_element_types.reserve(statement.target_names.size());
        statement.target_shapes.reserve(statement.target_names.size());
        statement.target_previous_types.reserve(statement.target_names.size());
        statement.target_previous_element_types.reserve(statement.target_names.size());
        if (program_.language == SourceLanguage::matlab) {
          if (statement.expression.kind != ExpressionKind::call ||
              !statement.expression.multi_output_call) {
            diagnose(statement.line, "MPF2034",
                     "Matlab multi-output assignment requires a function with multiple outputs");
            return false;
          }
        } else if (program_.language == SourceLanguage::python) {
          if (!statement.has_target_pattern || !statement.target_pattern.valid()) {
            diagnose(statement.line, "MPF2042",
                     "Python unpacking requires a structured assignment pattern");
            return false;
          }
          auto metadata = expression_metadata(statement.expression);
          if (metadata.elements.empty() && statement.expression.inferred_type == ValueType::tuple &&
              !statement.expression.tuple_types.empty()) {
            metadata.sequence = true;
            metadata.list_sequence = false;
            for (std::size_t index = 0; index < statement.expression.tuple_types.size(); ++index) {
              ValueMetadata element;
              element.type = statement.expression.tuple_types[index];
              element.element_type = index < statement.expression.tuple_element_types.size()
                                         ? statement.expression.tuple_element_types[index]
                                         : ValueType::unknown;
              element.shape = index < statement.expression.tuple_shapes.size()
                                  ? statement.expression.tuple_shapes[index]
                                  : std::vector<std::size_t>{};
              metadata.elements.push_back(std::move(element));
            }
          }
          if (!associate_assignment_pattern(statement, statement.target_pattern,
                                            std::move(metadata), {})) {
            return false;
          }
          return false;
        } else {
          diagnose(statement.line, "MPF2042",
                   "multi-target assignment is not supported for this source language");
          return false;
        }
        std::unordered_set<std::string> targets;
        for (std::size_t index = 0; index < statement.target_names.size(); ++index) {
          const auto& name = statement.target_names[index];
          if (!targets.insert(name).second && program_.language == SourceLanguage::matlab) {
            diagnose(statement.line, "MPF2034",
                     "Matlab multi-output assignment target '" + name + "' is duplicated");
          }
          auto& symbol = current()[name];
          const auto type = index < statement.expression.tuple_types.size()
                                ? statement.expression.tuple_types[index]
                                : ValueType::unknown;
          const auto element_type = index < statement.expression.tuple_element_types.size()
                                        ? statement.expression.tuple_element_types[index]
                                        : ValueType::unknown;
          const auto shape = index < statement.expression.tuple_shapes.size()
                                 ? statement.expression.tuple_shapes[index]
                                 : std::vector<std::size_t>{};
          statement.target_previous_types.push_back(symbol.type);
          statement.target_previous_element_types.push_back(symbol.element_type);
          symbol.binding = BindingKind::variable;
          symbol.type = join_types(symbol.type, type);
          symbol.element_type = join_types(symbol.element_type, element_type);
          if (symbol.shape.empty()) symbol.shape = shape;
          symbol.assigned = true;
          statement.target_types.push_back(type);
          statement.target_element_types.push_back(element_type);
          statement.target_shapes.push_back(shape);
        }
        return false;
      }
      case StatementKind::indexed_assignment: {
        diagnose_fortran_parameter_write(statement.name, statement.line);
        const auto value_type = analyze_expression(statement.expression);
        const auto target_type = analyze_expression(statement.target_expression);
        if (statement.target_expression.kind != ExpressionKind::index) {
          diagnose(statement.line, "MPF2022",
                   "indexed assignment target is not an array/list element");
        } else if (has_direct_slice(statement.target_expression)) {
          analyze_section_assignment(statement, value_type);
        } else if (contains_slice(statement.target_expression)) {
          diagnose(statement.line, "MPF2029",
                   "cannot assign through a temporary array/list section");
        } else if (target_type != ValueType::unknown && value_type != ValueType::unknown &&
                   join_types(target_type, value_type) == ValueType::unknown) {
          diagnose(statement.line, "MPF2020", "indexed assignment changes the array element type");
        }
        statement.element_type = has_direct_slice(statement.target_expression)
                                     ? statement.target_expression.element_type
                                     : target_type;
        return false;
      }
      case StatementKind::print:
        if (statement.has_expression) analyze_expression(statement.expression);
        return false;
      case StatementKind::expression: {
        const auto* saved_call = fortran_call_expression_;
        if (program_.language == SourceLanguage::fortran && statement.procedure_call) {
          fortran_call_expression_ = &statement.expression;
        }
        if (statement.has_expression) analyze_expression(statement.expression);
        fortran_call_expression_ = saved_call;
        return false;
      }
      case StatementKind::return_statement:
        if (statement.has_expression) analyze_expression(statement.expression);
        if (function_depth_ == 0) {
          diagnose(statement.line, "MPF2012", "return statement is only valid inside a function");
        }
        return true;
      case StatementKind::break_statement:
        if (loop_depth_ == 0) {
          diagnose(statement.line, "MPF2010", "break/exit statement is only valid inside a loop");
        }
        return true;
      case StatementKind::continue_statement:
        if (loop_depth_ == 0) {
          diagnose(statement.line, "MPF2011",
                   "continue/cycle statement is only valid inside a loop");
        }
        return true;
      case StatementKind::if_statement:
        analyze_expression(statement.expression);
        return analyze_branches(statement.body, statement.alternative);
      case StatementKind::select_case: return analyze_select_case(statement);
      case StatementKind::case_clause:
        diagnose(statement.line, "MPF2043", "CASE clause appears outside SELECT CASE");
        return false;
      case StatementKind::while_loop: {
        analyze_expression(statement.expression);
        const auto before = current();
        ++loop_depth_;
        analyze_statements(statement.body);
        --loop_depth_;
        current() = before;
        if (!statement.alternative.empty()) analyze_statements(statement.alternative);
        current() = before;
        return false;
      }
      case StatementKind::range_loop: {
        diagnose_fortran_parameter_write(statement.name, statement.line);
        const auto start = analyze_expression(statement.expression);
        const auto stop = analyze_expression(statement.secondary_expression);
        auto step = ValueType::integer;
        if (statement.has_tertiary_expression)
          step = analyze_expression(statement.tertiary_expression);
        auto& variable = current()[statement.name];
        variable.type = join_types(join_types(start, stop), step);
        variable.binding = BindingKind::variable;
        if (program_.language == SourceLanguage::python &&
            ((start != ValueType::integer && start != ValueType::unknown) ||
             (stop != ValueType::integer && stop != ValueType::unknown) ||
             (step != ValueType::integer && step != ValueType::unknown))) {
          diagnose(statement.line, "MPF2006", "Python range arguments must be integers");
        }
        const auto start_value = numeric_constant(statement.expression);
        const auto stop_value = numeric_constant(statement.secondary_expression);
        const auto step_value = statement.has_tertiary_expression
                                    ? numeric_constant(statement.tertiary_expression)
                                    : std::optional<double>{1.0};
        bool definitely_nonempty = false;
        if (step_value.has_value() && *step_value == 0.0) {
          diagnose(statement.line, "MPF2005", "range loop step cannot be zero");
        }
        if (start_value.has_value() && stop_value.has_value() && step_value.has_value() &&
            *step_value != 0.0) {
          definitely_nonempty = *step_value > 0.0
                                    ? (statement.inclusive_stop ? *start_value <= *stop_value
                                                                : *start_value < *stop_value)
                                    : (statement.inclusive_stop ? *start_value >= *stop_value
                                                                : *start_value > *stop_value);
          variable.assigned = variable.assigned || definitely_nonempty;
        }
        if (!statement.retain_last_loop_value) variable.assigned = true;
        const auto before = current();
        current()[statement.name].assigned = true;
        ++loop_depth_;
        analyze_statements(statement.body);
        --loop_depth_;
        const auto after_body = current();
        current() = before;
        if (!statement.alternative.empty()) analyze_statements(statement.alternative);
        current() = definitely_nonempty ? after_body : before;
        return false;
      }
      case StatementKind::function: analyze_function(statement); return false;
    }
    return false;
  }

  bool analyze_branches(std::vector<Statement>& body, std::vector<Statement>& alternative) {
    const auto before = current();
    const auto body_terminates = analyze_statements(body);
    const auto after_body = current();
    current() = before;
    const auto alternative_terminates = !alternative.empty() && analyze_statements(alternative);
    const auto after_alternative = current();
    current() = before;
    for (auto& [name, symbol] : current()) {
      const auto body_symbol = after_body.find(name);
      const auto alternative_symbol = after_alternative.find(name);
      if (body_symbol != after_body.end() && alternative_symbol != after_alternative.end()) {
        symbol.assigned = body_symbol->second.assigned && alternative_symbol->second.assigned;
        symbol.type = join_types(body_symbol->second.type, alternative_symbol->second.type);
        symbol.element_type =
            join_types(body_symbol->second.element_type, alternative_symbol->second.element_type);
        symbol.shape = body_symbol->second.shape == alternative_symbol->second.shape
                           ? body_symbol->second.shape
                           : std::vector<std::size_t>{};
        if (body_symbol->second.tuple_types == alternative_symbol->second.tuple_types &&
            body_symbol->second.tuple_element_types ==
                alternative_symbol->second.tuple_element_types &&
            body_symbol->second.tuple_shapes == alternative_symbol->second.tuple_shapes) {
          symbol.tuple_types = body_symbol->second.tuple_types;
          symbol.tuple_element_types = body_symbol->second.tuple_element_types;
          symbol.tuple_shapes = body_symbol->second.tuple_shapes;
        } else {
          symbol.tuple_types.clear();
          symbol.tuple_element_types.clear();
          symbol.tuple_shapes.clear();
        }
        if (body_symbol->second.sequence_is_list == alternative_symbol->second.sequence_is_list &&
            same_metadata(body_symbol->second.sequence_elements,
                          alternative_symbol->second.sequence_elements)) {
          symbol.sequence_is_list = body_symbol->second.sequence_is_list;
          symbol.sequence_elements = body_symbol->second.sequence_elements;
        } else {
          symbol.sequence_elements.clear();
        }
      }
    }
    return !alternative.empty() && body_terminates && alternative_terminates;
  }

  void merge_select_flows(const Scope& before, const std::vector<Scope>& flows) {
    current() = before;
    if (flows.empty()) return;
    for (auto& [name, symbol] : current()) {
      std::vector<const Symbol*> candidates;
      candidates.reserve(flows.size());
      for (const auto& flow : flows) {
        const auto found = flow.find(name);
        candidates.push_back(found == flow.end() ? &before.at(name) : &found->second);
      }

      symbol.assigned = std::all_of(candidates.begin(), candidates.end(),
                                    [](const Symbol* candidate) { return candidate->assigned; });
      const auto merge_type = [&](const auto member) {
        ValueType merged = ValueType::unknown;
        bool incompatible = false;
        for (const auto* candidate : candidates) {
          const auto value = candidate->*member;
          if (value == ValueType::unknown) continue;
          if (merged == ValueType::unknown) {
            merged = value;
            continue;
          }
          const auto joined = join_types(merged, value);
          if (joined == ValueType::unknown) {
            incompatible = true;
            break;
          }
          merged = joined;
        }
        return incompatible ? ValueType::unknown : merged;
      };
      symbol.type = merge_type(&Symbol::type);
      symbol.element_type = merge_type(&Symbol::element_type);

      const auto merged_shape = candidates.front()->shape;
      symbol.shape = merged_shape;
      if (!std::all_of(candidates.begin() + 1, candidates.end(),
                       [&](const Symbol* candidate) { return candidate->shape == merged_shape; })) {
        symbol.shape.clear();
      }
      const auto& first = *candidates.front();
      const bool same_tuple =
          std::all_of(candidates.begin() + 1, candidates.end(), [&](const Symbol* candidate) {
            return candidate->tuple_types == first.tuple_types &&
                   candidate->tuple_element_types == first.tuple_element_types &&
                   candidate->tuple_shapes == first.tuple_shapes;
          });
      if (same_tuple) {
        symbol.tuple_types = first.tuple_types;
        symbol.tuple_element_types = first.tuple_element_types;
        symbol.tuple_shapes = first.tuple_shapes;
      } else {
        symbol.tuple_types.clear();
        symbol.tuple_element_types.clear();
        symbol.tuple_shapes.clear();
      }
      const bool same_sequence =
          std::all_of(candidates.begin() + 1, candidates.end(), [&](const Symbol* candidate) {
            return candidate->sequence_is_list == first.sequence_is_list &&
                   same_metadata(candidate->sequence_elements, first.sequence_elements);
          });
      if (same_sequence) {
        symbol.sequence_is_list = first.sequence_is_list;
        symbol.sequence_elements = first.sequence_elements;
      } else {
        symbol.sequence_elements.clear();
      }
    }
  }

  bool analyze_select_case(Statement& statement) {
    const auto selector_type = analyze_expression(statement.expression);
    if (selector_type != ValueType::unknown && selector_type != ValueType::integer &&
        selector_type != ValueType::string && selector_type != ValueType::boolean) {
      diagnose(statement.line, "MPF2043",
               "Fortran SELECT CASE selector must be integer, character, or logical");
    }

    const auto before = current();
    std::vector<Scope> flows;
    bool has_default = false;
    bool all_terminate = true;
    std::vector<std::pair<double, double>> numeric_intervals;
    using CharacterInterval = std::pair<std::optional<std::string>, std::optional<std::string>>;
    std::vector<CharacterInterval> character_intervals;
    bool logical_values[2]{false, false};
    for (auto& clause : statement.body) {
      current() = before;
      has_default = has_default || clause.default_case;
      for (auto& value : clause.case_selectors) {
        if (value.has_lower) {
          if (!safe_fortran_case_constant(value.lower)) {
            diagnose(clause.line, "MPF2043",
                     "Fortran CASE bound must be a supported scalar constant expression");
          }
          const auto type = analyze_expression(value.lower);
          if (selector_type != ValueType::unknown && type != ValueType::unknown &&
              selector_type != type) {
            diagnose(clause.line, "MPF2043",
                     "Fortran CASE value type does not match the SELECT CASE selector");
          }
        }
        if (value.has_upper) {
          if (!safe_fortran_case_constant(value.upper)) {
            diagnose(clause.line, "MPF2043",
                     "Fortran CASE bound must be a supported scalar constant expression");
          }
          const auto type = analyze_expression(value.upper);
          if (selector_type != ValueType::unknown && type != ValueType::unknown &&
              selector_type != type) {
            diagnose(clause.line, "MPF2043",
                     "Fortran CASE value type does not match the SELECT CASE selector");
          }
        }
        if (value.range && selector_type == ValueType::boolean) {
          diagnose(clause.line, "MPF2043", "Fortran logical CASE selectors cannot use ranges");
        }
        if (selector_type == ValueType::integer) {
          const auto lower = value.has_lower
                                 ? numeric_constant(value.lower)
                                 : std::optional<double>{-std::numeric_limits<double>::infinity()};
          const auto upper =
              value.range ? (value.has_upper
                                 ? numeric_constant(value.upper)
                                 : std::optional<double>{std::numeric_limits<double>::infinity()})
                          : lower;
          if (lower.has_value() && upper.has_value()) {
            if (*lower > *upper) {
              diagnose(clause.line, "MPF2043",
                       "Fortran CASE range lower bound exceeds its upper bound");
            }
            for (const auto& existing : numeric_intervals) {
              if (std::max(*lower, existing.first) <= std::min(*upper, existing.second)) {
                diagnose(clause.line, "MPF2043",
                         "Fortran CASE selector overlaps an earlier selector");
                break;
              }
            }
            numeric_intervals.emplace_back(*lower, *upper);
          }
        } else if (selector_type == ValueType::string) {
          const auto lower =
              value.has_lower ? fortran_string_constant(value.lower) : std::optional<std::string>{};
          const auto upper = value.range ? (value.has_upper ? fortran_string_constant(value.upper)
                                                            : std::optional<std::string>{})
                                         : lower;
          if (lower.has_value() && upper.has_value() &&
              fortran_string_compare(*lower, *upper) > 0) {
            diagnose(clause.line, "MPF2043",
                     "Fortran CASE range lower bound exceeds its upper bound");
          }
          for (const auto& existing : character_intervals) {
            const bool before_existing = upper.has_value() && existing.first.has_value() &&
                                         fortran_string_compare(*upper, *existing.first) < 0;
            const bool after_existing = existing.second.has_value() && lower.has_value() &&
                                        fortran_string_compare(*existing.second, *lower) < 0;
            if (!before_existing && !after_existing) {
              diagnose(clause.line, "MPF2043",
                       "Fortran CASE selector overlaps an earlier selector");
              break;
            }
          }
          character_intervals.emplace_back(lower, upper);
        } else if (selector_type == ValueType::boolean && !value.range && value.has_lower &&
                   value.lower.kind == ExpressionKind::boolean_literal) {
          const auto logical_index = value.lower.value == "true" ? std::size_t{1} : std::size_t{0};
          if (logical_values[logical_index]) {
            diagnose(clause.line, "MPF2043", "Fortran CASE selector overlaps an earlier selector");
          }
          logical_values[logical_index] = true;
        }
      }

      const auto terminates = analyze_statements(clause.body);
      all_terminate = all_terminate && terminates;
      if (!terminates) flows.push_back(current());
    }
    if (!has_default) {
      flows.push_back(before);
      all_terminate = false;
    }
    merge_select_flows(before, flows);
    return has_default && all_terminate;
  }

  void diagnose_fortran_parameter_write(const std::string& name, const std::size_t line) {
    if (program_.language == SourceLanguage::fortran && !function_parameters_.empty() &&
        function_parameters_.back().count(name) != 0U) {
      const auto parameter = std::find(function_parameter_order_.back().begin(),
                                       function_parameter_order_.back().end(), name);
      const auto position = static_cast<std::size_t>(
          std::distance(function_parameter_order_.back().begin(), parameter));
      if ((*function_parameter_intents_.back())[position] == ParameterIntent::in) {
        diagnose(line, "MPF2036", "Fortran INTENT(IN) dummy argument cannot be modified");
      }
      function_parameter_writes_.back()[position] = true;
    }
  }

  void mark_fortran_parameter_read(const std::string& name) {
    if (program_.language != SourceLanguage::fortran || function_parameters_.empty() ||
        function_parameters_.back().count(name) == 0U)
      return;
    const auto parameter = std::find(function_parameter_order_.back().begin(),
                                     function_parameter_order_.back().end(), name);
    const auto position = static_cast<std::size_t>(
        std::distance(function_parameter_order_.back().begin(), parameter));
    function_parameter_reads_.back()[position] = true;
  }

  void collect_value_returns(const std::vector<Statement>& statements,
                             std::vector<const Expression*>& returns) const {
    for (const auto& statement : statements) {
      if (statement.kind == StatementKind::function) continue;
      if (statement.kind == StatementKind::return_statement && statement.has_expression) {
        returns.push_back(&statement.expression);
      }
      collect_value_returns(statement.body, returns);
      collect_value_returns(statement.alternative, returns);
    }
  }

  void infer_python_tuple_returns(Statement& function) const {
    std::vector<const Expression*> returns;
    collect_value_returns(function.body, returns);
    if (returns.empty() ||
        std::any_of(returns.begin(), returns.end(), [](const Expression* expression) {
          return expression->inferred_type != ValueType::tuple || expression->tuple_types.empty();
        })) {
      return;
    }
    const auto arity = returns.front()->tuple_types.size();
    if (std::any_of(returns.begin(), returns.end(), [arity](const Expression* expression) {
          return expression->tuple_types.size() != arity;
        })) {
      return;
    }

    function.return_types = returns.front()->tuple_types;
    function.return_element_types = returns.front()->tuple_element_types;
    function.return_shapes = returns.front()->tuple_shapes;
    function.return_element_types.resize(arity, ValueType::unknown);
    function.return_shapes.resize(arity);
    for (std::size_t index = 0; index < arity; ++index) {
      bool type_conflict = false;
      bool element_conflict = false;
      auto type = function.return_types[index];
      auto element = function.return_element_types[index];
      auto shape = function.return_shapes[index];
      for (std::size_t path = 1; path < returns.size(); ++path) {
        const auto next_type = returns[path]->tuple_types[index];
        const auto joined_type = join_types(type, next_type);
        if (type != ValueType::unknown && next_type != ValueType::unknown &&
            joined_type == ValueType::unknown) {
          type_conflict = true;
        }
        type = joined_type;
        const auto next_element = index < returns[path]->tuple_element_types.size()
                                      ? returns[path]->tuple_element_types[index]
                                      : ValueType::unknown;
        const auto joined_element = join_types(element, next_element);
        if (element != ValueType::unknown && next_element != ValueType::unknown &&
            joined_element == ValueType::unknown) {
          element_conflict = true;
        }
        element = joined_element;
        const auto next_shape = index < returns[path]->tuple_shapes.size()
                                    ? returns[path]->tuple_shapes[index]
                                    : std::vector<std::size_t>{};
        if (shape != next_shape) shape.clear();
      }
      function.return_types[index] = type_conflict ? ValueType::unknown : type;
      function.return_element_types[index] = element_conflict ? ValueType::unknown : element;
      function.return_shapes[index] = std::move(shape);
    }
  }

  void infer_python_sequence_metadata(Statement& function) const {
    std::vector<const Expression*> returns;
    collect_value_returns(function.body, returns);
    if (returns.empty()) return;
    const auto first = expression_metadata(*returns.front());
    if (!first.sequence) return;
    for (std::size_t index = 1; index < returns.size(); ++index) {
      if (!same_metadata(first, expression_metadata(*returns[index]))) return;
    }
    function.element_type = first.element_type;
    function.shape = first.shape;
    function.return_sequence_is_list = first.list_sequence;
    function.return_sequence_elements = first.elements;
  }

  void analyze_function(Statement& function) {
    if (program_.language == SourceLanguage::python) {
      if (function.parameter_kinds.size() < function.parameters.size()) {
        function.parameter_kinds.resize(function.parameters.size(),
                                        ParameterKind::positional_or_keyword);
      }
      if (function.parameter_defaults.size() < function.parameters.size()) {
        function.parameter_defaults.resize(function.parameters.size());
      }
      for (auto& default_value : function.parameter_defaults) {
        if (!default_value.valid()) continue;
        if (!safe_python_default(default_value)) {
          diagnose(default_value.location.line, "MPF2041",
                   "Python defaults currently require an immutable scalar literal");
        }
        analyze_expression(default_value);
      }
    }
    scopes_.emplace_back();
    predeclare(function.body);
    function.parameter_intents.clear();
    if (program_.language == SourceLanguage::fortran) {
      function.parameter_intents.assign(function.parameters.size(), ParameterIntent::none);
      function.parameter_optional.assign(function.parameters.size(), false);
    }
    for (auto& statement : function.body) {
      if (program_.language != SourceLanguage::fortran) break;
      if (statement.kind != StatementKind::declaration) continue;
      const auto parameter =
          std::find(function.parameters.begin(), function.parameters.end(), statement.name);
      if (parameter == function.parameters.end()) {
        continue;
      }
      statement.dummy_parameter = true;
      const auto position =
          static_cast<std::size_t>(std::distance(function.parameters.begin(), parameter));
      if (statement.optional_parameter) {
        function.parameter_optional[position] = true;
      }
      if (statement.parameter_intent != ParameterIntent::none) {
        if (function.parameter_intents[position] != ParameterIntent::none &&
            function.parameter_intents[position] != statement.parameter_intent) {
          diagnose(statement.line, "MPF2036",
                   "Fortran dummy argument has conflicting INTENT declarations");
        }
        function.parameter_intents[position] = statement.parameter_intent;
      }
    }
    function_parameters_.emplace_back(function.parameters.begin(), function.parameters.end());
    function_parameter_order_.push_back(function.parameters);
    function_parameter_reads_.emplace_back(function.parameters.size(), false);
    function_parameter_writes_.emplace_back(function.parameters.size(), false);
    function_parameter_intents_.push_back(&function.parameter_intents);
    function_optional_parameters_.emplace_back();
    for (std::size_t index = 0; index < function.parameters.size(); ++index) {
      if (index < function.parameter_optional.size() && function.parameter_optional[index]) {
        function_optional_parameters_.back().insert(function.parameters[index]);
      }
    }
    for (std::size_t index = 0; index < function.parameters.size(); ++index) {
      const auto& parameter = function.parameters[index];
      const auto default_type = program_.language == SourceLanguage::python &&
                                        index < function.parameter_defaults.size() &&
                                        function.parameter_defaults[index].valid()
                                    ? function.parameter_defaults[index].inferred_type
                                    : ValueType::unknown;
      current()[parameter] = {default_type,
                              BindingKind::variable,
                              program_.language != SourceLanguage::fortran ||
                                  function.parameter_intents[index] != ParameterIntent::out,
                              ValueType::unknown,
                              {}};
    }
    for (std::size_t index = 0; index < function.return_names.size(); ++index) {
      const auto type =
          index < function.return_types.size() ? function.return_types[index] : ValueType::unknown;
      current()[function.return_names[index]] = {
          type, BindingKind::variable, false, ValueType::unknown, {}};
    }
    ++function_depth_;
    const auto saved_loop_depth = loop_depth_;
    loop_depth_ = 0;
    const auto body_terminates = analyze_statements(function.body);
    loop_depth_ = saved_loop_depth;
    --function_depth_;
    annotate_types(function.body, current());
    function.parameter_types.clear();
    function.parameter_element_types.clear();
    function.parameter_shapes.clear();
    function.parameter_types.reserve(function.parameters.size());
    function.parameter_element_types.reserve(function.parameters.size());
    function.parameter_shapes.reserve(function.parameters.size());
    for (const auto& parameter : function.parameters) {
      const auto found = current().find(parameter);
      function.parameter_types.push_back(found == current().end() ? ValueType::unknown
                                                                  : found->second.type);
      function.parameter_element_types.push_back(
          found == current().end() ? ValueType::unknown : found->second.element_type);
      function.parameter_shapes.push_back(found == current().end() ? std::vector<std::size_t>{}
                                                                   : found->second.shape);
    }
    for (std::size_t index = 0;
         program_.language == SourceLanguage::fortran && index < function.parameters.size();
         ++index) {
      auto& intent = function.parameter_intents[index];
      if (intent == ParameterIntent::none) {
        intent = function_parameter_writes_.back()[index]
                     ? (function_parameter_reads_.back()[index] ? ParameterIntent::inout
                                                                : ParameterIntent::out)
                     : ParameterIntent::in;
      }
      const bool optional =
          index < function.parameter_optional.size() && function.parameter_optional[index];
      if (intent == ParameterIntent::out && !optional) {
        const auto found = current().find(function.parameters[index]);
        if (found == current().end() || !found->second.assigned) {
          diagnose(function.line, "MPF2036",
                   "Fortran INTENT(OUT) dummy argument '" + function.parameters[index] +
                       "' is not definitely assigned");
        }
      }
    }
    bool has_value_return = false;
    bool has_empty_return = false;
    bool incompatible_returns = false;
    function.declared_type = collect_return_type(function.body, has_value_return, has_empty_return,
                                                 incompatible_returns);
    function.has_value_return = has_value_return;
    (void)body_terminates;
    (void)has_value_return;
    (void)has_empty_return;
    (void)incompatible_returns;
    std::vector<ValueType> output_types;
    std::vector<ValueType> output_element_types;
    std::vector<std::vector<std::size_t>> output_shapes;
    output_types.reserve(function.return_names.size());
    output_element_types.reserve(function.return_names.size());
    output_shapes.reserve(function.return_names.size());
    for (const auto& result : function.return_names) {
      const auto found = current().find(result);
      if (found == current().end() || !found->second.assigned) {
        diagnose(function.line, "MPF2004",
                 "function result '" + result + "' is not definitely assigned");
        output_types.push_back(ValueType::unknown);
        output_element_types.push_back(ValueType::unknown);
        output_shapes.emplace_back();
      } else {
        output_types.push_back(found->second.type);
        output_element_types.push_back(found->second.element_type);
        output_shapes.push_back(found->second.shape);
      }
    }
    function.return_types = output_types;
    function.return_element_types = output_element_types;
    function.return_shapes = output_shapes;
    if (output_types.size() == 1) {
      function.declared_type = output_types.front();
    } else if (output_types.size() > 1) {
      function.declared_type = ValueType::tuple;
    }
    if (program_.language == SourceLanguage::python && function.declared_type == ValueType::tuple) {
      infer_python_tuple_returns(function);
    }
    if (program_.language == SourceLanguage::python &&
        (function.declared_type == ValueType::tuple || function.declared_type == ValueType::list)) {
      infer_python_sequence_metadata(function);
    }
    function_parameter_intents_.pop_back();
    function_optional_parameters_.pop_back();
    function_parameter_writes_.pop_back();
    function_parameter_reads_.pop_back();
    function_parameter_order_.pop_back();
    function_parameters_.pop_back();
    scopes_.pop_back();
  }

  ValueType collect_return_type(const std::vector<Statement>& statements, bool& has_value,
                                bool& has_empty, bool& incompatible) const {
    ValueType result = ValueType::unknown;
    for (const auto& statement : statements) {
      if (statement.kind == StatementKind::function) continue;
      if (statement.kind == StatementKind::return_statement) {
        if (!statement.has_expression) {
          has_empty = true;
        } else {
          has_value = true;
          const auto joined = join_types(result, statement.expression.inferred_type);
          if (result != ValueType::unknown &&
              statement.expression.inferred_type != ValueType::unknown &&
              joined == ValueType::unknown) {
            incompatible = true;
          }
          result = joined;
        }
      }
      const auto body_type =
          collect_return_type(statement.body, has_value, has_empty, incompatible);
      auto joined = join_types(result, body_type);
      if (result != ValueType::unknown && body_type != ValueType::unknown &&
          joined == ValueType::unknown) {
        incompatible = true;
      }
      result = joined;
      const auto alternative_type =
          collect_return_type(statement.alternative, has_value, has_empty, incompatible);
      joined = join_types(result, alternative_type);
      if (result != ValueType::unknown && alternative_type != ValueType::unknown &&
          joined == ValueType::unknown) {
        incompatible = true;
      }
      result = joined;
    }
    return result;
  }

  void annotate_types(std::vector<Statement>& statements, const Scope& scope) {
    for (auto& statement : statements) {
      if (statement.kind == StatementKind::assignment ||
          statement.kind == StatementKind::range_loop) {
        const auto found = scope.find(statement.name);
        if (found != scope.end()) {
          statement.declared_type = found->second.type;
          statement.element_type = found->second.element_type;
          statement.shape = found->second.shape;
          if (statement.kind == StatementKind::assignment &&
              statement.expression.kind == ExpressionKind::list) {
            statement.expression.element_type = found->second.element_type;
          }
        }
      }
      if (statement.kind != StatementKind::function) {
        annotate_types(statement.body, scope);
        annotate_types(statement.alternative, scope);
      }
    }
  }

  void refresh_expression_call_intents(Expression& expression) {
    if (expression.kind == ExpressionKind::call && !expression.children.empty()) {
      const auto& callee = expression.children.front();
      if (callee.kind == ExpressionKind::identifier && callee.binding == BindingKind::function) {
        const auto function = std::find_if(program_.statements.begin(), program_.statements.end(),
                                           [&](const Statement& statement) {
                                             return statement.kind == StatementKind::function &&
                                                    statement.name == callee.value;
                                           });
        if (function != program_.statements.end()) {
          expression.argument_intents = function->parameter_intents;
          expression.procedure_has_result =
              !function->return_names.empty() || function->has_value_return;
        }
      }
    }
    for (auto& child : expression.children) refresh_expression_call_intents(child);
  }

  void refresh_call_intents(std::vector<Statement>& statements) {
    for (auto& statement : statements) {
      if (statement.has_expression) refresh_expression_call_intents(statement.expression);
      if (statement.has_target_expression) {
        refresh_expression_call_intents(statement.target_expression);
      }
      if (statement.has_secondary_expression) {
        refresh_expression_call_intents(statement.secondary_expression);
      }
      if (statement.has_tertiary_expression) {
        refresh_expression_call_intents(statement.tertiary_expression);
      }
      refresh_call_intents(statement.body);
      refresh_call_intents(statement.alternative);
    }
  }

  ValueType analyze_expression(Expression& expression) {
    switch (expression.kind) {
      case ExpressionKind::invalid: return expression.inferred_type = ValueType::unknown;
      case ExpressionKind::number_literal:
        expression.inferred_type = expression.value.find_first_of(".eE") == std::string::npos
                                       ? ValueType::integer
                                       : ValueType::real;
        return expression.inferred_type;
      case ExpressionKind::string_literal: return expression.inferred_type = ValueType::string;
      case ExpressionKind::boolean_literal: return expression.inferred_type = ValueType::boolean;
      case ExpressionKind::null_literal: return expression.inferred_type = ValueType::null_value;
      case ExpressionKind::omitted_argument: return expression.inferred_type = ValueType::unknown;
      case ExpressionKind::identifier: {
        if (auto* symbol = lookup(expression.value)) {
          expression.binding = symbol->binding;
          expression.inferred_type = symbol->type;
          expression.element_type = symbol->element_type;
          expression.shape = symbol->shape;
          expression.tuple_types = symbol->tuple_types;
          expression.tuple_element_types = symbol->tuple_element_types;
          expression.tuple_shapes = symbol->tuple_shapes;
          expression.sequence_is_list = symbol->sequence_is_list;
          expression.sequence_elements = symbol->sequence_elements;
          if (symbol->binding == BindingKind::variable) {
            mark_fortran_parameter_read(expression.value);
          }
          const auto local = current().find(expression.value);
          const bool belongs_to_current_scope =
              local != current().end() && &local->second == symbol;
          if (symbol->binding == BindingKind::variable && !symbol->assigned &&
              (scopes_.size() == 1 || belongs_to_current_scope)) {
            diagnose(
                expression.location.line, "MPF2003",
                "variable '" + expression.value + "' is used before it is definitely assigned");
          }
          return expression.inferred_type;
        }
        const auto intrinsic = find_intrinsic(program_.language, expression.value);
        if (intrinsic != IntrinsicId::none) {
          expression.binding = BindingKind::builtin;
          expression.intrinsic = intrinsic;
          expression.inferred_type =
              intrinsic == IntrinsicId::not_a_number || intrinsic == IntrinsicId::infinity
                  ? ValueType::real
                  : ValueType::function;
          return expression.inferred_type;
        }
        diagnose(expression.location.line, "MPF2001",
                 "undefined identifier '" + expression.value + "'");
        return expression.inferred_type = ValueType::unknown;
      }
      case ExpressionKind::unary: {
        const auto operand = expression.children.empty()
                                 ? ValueType::unknown
                                 : analyze_expression(expression.children.front());
        expression.inferred_type = expression.value == "!" ? ValueType::boolean : operand;
        return expression.inferred_type;
      }
      case ExpressionKind::binary: return analyze_binary(expression);
      case ExpressionKind::comparison_chain: return analyze_comparison_chain(expression);
      case ExpressionKind::conditional: return analyze_conditional(expression);
      case ExpressionKind::call: return analyze_call(expression);
      case ExpressionKind::member:
        if (!expression.children.empty()) analyze_expression(expression.children.front());
        return expression.inferred_type = ValueType::unknown;
      case ExpressionKind::index: return analyze_index(expression);
      case ExpressionKind::slice:
        diagnose(expression.location.line, "MPF2029",
                 "slice/colon expressions are only valid as array/list subscripts");
        return expression.inferred_type = ValueType::unknown;
      case ExpressionKind::list: {
        ValueType element_type = ValueType::unknown;
        bool incompatible = false;
        bool ragged = false;
        bool nested = !expression.children.empty();
        std::vector<std::size_t> nested_shape;
        for (auto& child : expression.children) {
          const auto child_type = analyze_expression(child);
          nested = nested && child_type == ValueType::list;
          if (child_type == ValueType::list) {
            if (nested_shape.empty())
              nested_shape = child.shape;
            else if (nested_shape != child.shape) {
              incompatible = true;
              ragged = true;
            }
          }
          const auto scalar_type = child_type == ValueType::list ? child.element_type : child_type;
          const auto joined = join_types(element_type, scalar_type);
          if (element_type != ValueType::unknown && child_type != ValueType::unknown &&
              joined == ValueType::unknown)
            incompatible = true;
          element_type = joined;
        }
        const bool mixed_nesting =
            std::any_of(
                expression.children.begin(), expression.children.end(),
                [](const Expression& child) { return child.inferred_type == ValueType::list; }) &&
            !nested;
        incompatible = incompatible || mixed_nesting;
        if (incompatible && program_.language != SourceLanguage::python) {
          diagnose(expression.location.line, "MPF2020",
                   "target array/list requires a homogeneous element type");
        }
        expression.element_type = element_type;
        expression.shape = {expression.children.size()};
        if (nested && !nested_shape.empty() && !ragged) {
          expression.shape.insert(expression.shape.end(), nested_shape.begin(), nested_shape.end());
        } else if (nested && ragged) {
          const auto maximum_rank =
              std::max_element(expression.children.begin(), expression.children.end(),
                               [](const Expression& left, const Expression& right) {
                                 return left.shape.size() < right.shape.size();
                               })
                  ->shape.size();
          expression.shape.insert(expression.shape.end(), maximum_rank, dynamic_extent);
        } else if (mixed_nesting) {
          expression.element_type = ValueType::unknown;
        }
        expression.sequence_is_list = true;
        expression.sequence_elements.clear();
        expression.sequence_elements.reserve(expression.children.size());
        for (const auto& child : expression.children) {
          expression.sequence_elements.push_back(expression_metadata(child));
        }
        return expression.inferred_type = ValueType::list;
      }
      case ExpressionKind::tuple:
        expression.tuple_types.clear();
        expression.tuple_element_types.clear();
        expression.tuple_shapes.clear();
        for (auto& child : expression.children) {
          expression.tuple_types.push_back(analyze_expression(child));
          expression.tuple_element_types.push_back(child.element_type);
          expression.tuple_shapes.push_back(child.shape);
        }
        expression.sequence_is_list = false;
        expression.sequence_elements.clear();
        expression.sequence_elements.reserve(expression.children.size());
        for (const auto& child : expression.children) {
          expression.sequence_elements.push_back(expression_metadata(child));
        }
        return expression.inferred_type = ValueType::tuple;
    }
    return ValueType::unknown;
  }

  ValueType analyze_binary(Expression& expression) {
    const auto left = analyze_expression(expression.children[0]);
    const auto right = analyze_expression(expression.children[1]);
    if (program_.language == SourceLanguage::python &&
        (expression.value == "&&" || expression.value == "||")) {
      expression.inferred_type = join_types(left, right);
      if (left == ValueType::list && right == ValueType::list) {
        expression.element_type =
            join_types(expression.children[0].element_type, expression.children[1].element_type);
        if (expression.children[0].shape == expression.children[1].shape) {
          expression.shape = expression.children[0].shape;
        } else {
          const auto rank =
              std::max(expression.children[0].shape.size(), expression.children[1].shape.size());
          expression.shape.assign(rank, dynamic_extent);
        }
      }
      return expression.inferred_type;
    }
    if (expression.value == "&&" || expression.value == "||" || expression.value == "===" ||
        expression.value == "!==" || expression.value == "<" || expression.value == "<=" ||
        expression.value == ">" || expression.value == ">=") {
      if (program_.language == SourceLanguage::python &&
          (expression.value == "<" || expression.value == "<=" || expression.value == ">" ||
           expression.value == ">=")) {
        validate_python_ordering(left, right, expression.location.line);
      }
      return expression.inferred_type = ValueType::boolean;
    }
    if (expression.value == "+" && left == ValueType::string && right == ValueType::string) {
      return expression.inferred_type = ValueType::string;
    }
    if ((!numeric(left) || !numeric(right)) && left != ValueType::unknown &&
        right != ValueType::unknown) {
      diagnose(expression.location.line, "MPF2002",
               "operator '" + expression.value + "' cannot be applied to " + to_string(left) +
                   " and " + to_string(right));
      return expression.inferred_type = ValueType::unknown;
    }
    if (expression.value == "//") return expression.inferred_type = ValueType::integer;
    if (expression.value == "/" && (program_.language == SourceLanguage::python ||
                                    program_.language == SourceLanguage::matlab)) {
      return expression.inferred_type = ValueType::real;
    }
    if (expression.value == "**") return expression.inferred_type = ValueType::real;
    return expression.inferred_type = join_types(left, right);
  }

  void validate_python_ordering(const ValueType left, const ValueType right,
                                const std::size_t line) {
    if (left == ValueType::unknown || right == ValueType::unknown) return;
    const auto numeric_like = [](const ValueType type) {
      return type == ValueType::integer || type == ValueType::real || type == ValueType::boolean;
    };
    if ((numeric_like(left) && numeric_like(right)) ||
        (left == ValueType::string && right == ValueType::string) ||
        (left == ValueType::list && right == ValueType::list) ||
        (left == ValueType::tuple && right == ValueType::tuple)) {
      return;
    }
    diagnose(line, "MPF2044", "Python ordering comparison has incompatible operand types");
  }

  ValueType analyze_comparison_chain(Expression& expression) {
    if (expression.children.size() < 3 ||
        expression.operators.size() + 1 != expression.children.size()) {
      diagnose(expression.location.line, "MPF2044", "malformed Python comparison chain IR");
      return expression.inferred_type = ValueType::unknown;
    }
    std::vector<ValueType> operand_types;
    operand_types.reserve(expression.children.size());
    for (auto& child : expression.children) {
      operand_types.push_back(analyze_expression(child));
    }
    for (std::size_t index = 0; index < expression.operators.size(); ++index) {
      const auto& operation = expression.operators[index];
      if (operation == "<" || operation == "<=" || operation == ">" || operation == ">=") {
        validate_python_ordering(operand_types[index], operand_types[index + 1],
                                 expression.location.line);
      }
    }
    return expression.inferred_type = ValueType::boolean;
  }

  ValueType analyze_conditional(Expression& expression) {
    if (expression.children.size() != 3) {
      diagnose(expression.location.line, "MPF2044", "malformed Python conditional expression IR");
      return expression.inferred_type = ValueType::unknown;
    }
    analyze_expression(expression.children[0]);
    const auto true_type = analyze_expression(expression.children[1]);
    const auto false_type = analyze_expression(expression.children[2]);
    expression.inferred_type = join_types(true_type, false_type);
    if (true_type == ValueType::list && false_type == ValueType::list) {
      expression.element_type =
          join_types(expression.children[1].element_type, expression.children[2].element_type);
      if (expression.children[1].shape == expression.children[2].shape) {
        expression.shape = expression.children[1].shape;
      } else {
        const auto rank =
            std::max(expression.children[1].shape.size(), expression.children[2].shape.size());
        expression.shape.assign(rank, dynamic_extent);
      }
    }
    if (true_type == ValueType::tuple && false_type == ValueType::tuple &&
        expression.children[1].tuple_types == expression.children[2].tuple_types &&
        expression.children[1].tuple_element_types == expression.children[2].tuple_element_types &&
        expression.children[1].tuple_shapes == expression.children[2].tuple_shapes) {
      expression.tuple_types = expression.children[1].tuple_types;
      expression.tuple_element_types = expression.children[1].tuple_element_types;
      expression.tuple_shapes = expression.children[1].tuple_shapes;
    }
    if ((true_type == ValueType::list || true_type == ValueType::tuple) &&
        true_type == false_type &&
        expression.children[1].sequence_is_list == expression.children[2].sequence_is_list &&
        same_metadata(expression.children[1].sequence_elements,
                      expression.children[2].sequence_elements)) {
      expression.sequence_is_list = expression.children[1].sequence_is_list;
      expression.sequence_elements = expression.children[1].sequence_elements;
    }
    return expression.inferred_type;
  }

  void normalize_fortran_arguments(Expression& expression, const Statement& function) {
    const auto actual_count = expression.children.size() - 1;
    if (expression.argument_names.size() < actual_count) {
      expression.argument_names.resize(actual_count);
    }
    std::vector<std::optional<Expression>> associated(function.parameters.size());
    std::vector<bool> used(function.parameters.size(), false);
    std::size_t positional = 0;
    bool saw_keyword = false;
    for (std::size_t index = 0; index < actual_count; ++index) {
      auto actual = std::move(expression.children[index + 1]);
      const auto& keyword = expression.argument_names[index];
      std::size_t formal;
      if (keyword.empty()) {
        if (saw_keyword) {
          diagnose(actual.location.line, "MPF2040",
                   "Fortran positional actual argument cannot follow a keyword argument");
        }
        while (positional < used.size() && used[positional]) ++positional;
        formal = positional++;
        if (formal >= function.parameters.size()) {
          diagnose(actual.location.line, "MPF2034",
                   "procedure '" + function.name + "' received too many actual arguments");
          continue;
        }
      } else {
        saw_keyword = true;
        const auto found =
            std::find(function.parameters.begin(), function.parameters.end(), keyword);
        if (found == function.parameters.end()) {
          diagnose(actual.location.line, "MPF2040",
                   "unknown keyword actual argument '" + keyword + "' for procedure '" +
                       function.name + "'");
          continue;
        }
        formal = static_cast<std::size_t>(std::distance(function.parameters.begin(), found));
      }
      if (used[formal]) {
        diagnose(
            actual.location.line, "MPF2040",
            "dummy argument '" + function.parameters[formal] + "' is associated more than once");
        continue;
      }
      used[formal] = true;
      associated[formal] = std::move(actual);
    }

    std::vector<Expression> normalized;
    normalized.reserve(function.parameters.size() + 1);
    normalized.push_back(std::move(expression.children.front()));
    for (std::size_t index = 0; index < function.parameters.size(); ++index) {
      if (associated[index].has_value()) {
        normalized.push_back(std::move(*associated[index]));
        continue;
      }
      const bool optional =
          index < function.parameter_optional.size() && function.parameter_optional[index];
      if (!optional) {
        diagnose(expression.location.line, "MPF2034",
                 "required dummy argument '" + function.parameters[index] +
                     "' is not associated in call to '" + function.name + "'");
      }
      Expression omitted;
      omitted.kind = ExpressionKind::omitted_argument;
      omitted.location = expression.location;
      normalized.push_back(std::move(omitted));
    }
    expression.children = std::move(normalized);
    expression.argument_names.assign(function.parameters.size(), {});
    expression.argument_optional_forward.assign(function.parameters.size(), false);
  }

  void normalize_python_arguments(Expression& expression, const Statement& function) {
    const auto actual_count = expression.children.size() - 1;
    if (expression.argument_names.size() < actual_count) {
      expression.argument_names.resize(actual_count);
    }
    std::vector<std::optional<Expression>> associated(function.parameters.size());
    std::vector<bool> used(function.parameters.size(), false);
    std::size_t positional = 0;
    bool saw_keyword = false;
    for (std::size_t index = 0; index < actual_count; ++index) {
      auto actual = std::move(expression.children[index + 1]);
      const auto& keyword = expression.argument_names[index];
      std::size_t formal;
      if (keyword.empty()) {
        if (saw_keyword) {
          diagnose(actual.location.line, "MPF2041",
                   "Python positional argument cannot follow a keyword argument");
        }
        while (positional < function.parameters.size()) {
          const auto kind = positional < function.parameter_kinds.size()
                                ? function.parameter_kinds[positional]
                                : ParameterKind::positional_or_keyword;
          if (!used[positional] && kind != ParameterKind::keyword_only) break;
          ++positional;
        }
        formal = positional++;
        if (formal >= function.parameters.size()) {
          diagnose(actual.location.line, "MPF2034",
                   "function '" + function.name + "' received too many positional arguments");
          continue;
        }
      } else {
        saw_keyword = true;
        const auto found =
            std::find(function.parameters.begin(), function.parameters.end(), keyword);
        if (found == function.parameters.end()) {
          diagnose(
              actual.location.line, "MPF2041",
              "unknown keyword argument '" + keyword + "' for function '" + function.name + "'");
          continue;
        }
        formal = static_cast<std::size_t>(std::distance(function.parameters.begin(), found));
        const auto kind = formal < function.parameter_kinds.size()
                              ? function.parameter_kinds[formal]
                              : ParameterKind::positional_or_keyword;
        if (kind == ParameterKind::positional_only) {
          diagnose(actual.location.line, "MPF2041",
                   "positional-only parameter '" + keyword + "' cannot be passed by keyword");
          continue;
        }
      }
      if (used[formal]) {
        diagnose(actual.location.line, "MPF2041",
                 "parameter '" + function.parameters[formal] + "' receives more than one argument");
        continue;
      }
      used[formal] = true;
      associated[formal] = std::move(actual);
    }

    std::vector<Expression> normalized;
    normalized.reserve(function.parameters.size() + 1);
    normalized.push_back(std::move(expression.children.front()));
    for (std::size_t index = 0; index < function.parameters.size(); ++index) {
      if (associated[index].has_value()) {
        normalized.push_back(std::move(*associated[index]));
      } else if (index < function.parameter_defaults.size() &&
                 function.parameter_defaults[index].valid()) {
        normalized.push_back(function.parameter_defaults[index]);
      } else {
        diagnose(expression.location.line, "MPF2034",
                 "required parameter '" + function.parameters[index] + "' is missing in call to '" +
                     function.name + "'");
        Expression omitted;
        omitted.kind = ExpressionKind::omitted_argument;
        omitted.location = expression.location;
        normalized.push_back(std::move(omitted));
      }
    }
    expression.children = std::move(normalized);
    expression.argument_names.assign(function.parameters.size(), {});
  }

  ValueType analyze_call(Expression& expression) {
    if (expression.children.empty()) return expression.inferred_type = ValueType::unknown;
    auto& callee = expression.children.front();
    analyze_expression(callee);
    if ((program_.language == SourceLanguage::matlab ||
         program_.language == SourceLanguage::fortran) &&
        callee.kind == ExpressionKind::identifier && callee.binding == BindingKind::variable &&
        (callee.inferred_type == ValueType::list || callee.inferred_type == ValueType::unknown) &&
        expression.children.size() >= 2) {
      if (program_.language == SourceLanguage::fortran &&
          std::any_of(expression.argument_names.begin(), expression.argument_names.end(),
                      [](const std::string& name) { return !name.empty(); })) {
        diagnose(expression.location.line, "MPF2040",
                 "Fortran array subscripts cannot use procedure argument keywords");
      }
      expression.kind = ExpressionKind::index;
      expression.index_base = 1;
      expression.allow_negative_index = false;
      expression.column_major = true;
      return analyze_index(expression, true);
    }
    const Statement* called_function = nullptr;
    if (callee.binding == BindingKind::function && callee.kind == ExpressionKind::identifier) {
      const auto function = std::find_if(
          program_.statements.begin(), program_.statements.end(), [&](const Statement& statement) {
            return statement.kind == StatementKind::function && statement.name == callee.value;
          });
      if (function != program_.statements.end()) called_function = &*function;
    }
    if (program_.language == SourceLanguage::fortran && called_function != nullptr) {
      normalize_fortran_arguments(expression, *called_function);
    } else if (program_.language == SourceLanguage::python && called_function != nullptr) {
      normalize_python_arguments(expression, *called_function);
    } else if (program_.language == SourceLanguage::fortran &&
               std::any_of(expression.argument_names.begin(), expression.argument_names.end(),
                           [](const std::string& name) { return !name.empty(); })) {
      diagnose(expression.location.line, "MPF2040",
               "Fortran keyword actual arguments require a known procedure interface");
    } else if (program_.language == SourceLanguage::python &&
               std::any_of(expression.argument_names.begin(), expression.argument_names.end(),
                           [](const std::string& name) { return !name.empty(); })) {
      diagnose(expression.location.line, "MPF2041",
               "Python keyword arguments require a known user-function signature");
    }
    const auto& associated_callee = expression.children.front();
    if (program_.language == SourceLanguage::fortran && called_function != nullptr) {
      expression.argument_intents = called_function->parameter_intents;
      expression.procedure_has_result =
          !called_function->return_names.empty() || called_function->has_value_return;
    }
    if (associated_callee.kind == ExpressionKind::identifier &&
        associated_callee.binding == BindingKind::builtin &&
        associated_callee.intrinsic == IntrinsicId::present) {
      const bool valid =
          expression.children.size() == 2 &&
          expression.children[1].kind == ExpressionKind::identifier &&
          !function_optional_parameters_.empty() &&
          function_optional_parameters_.back().count(expression.children[1].value) != 0U;
      if (expression.children.size() == 2 &&
          expression.children[1].kind == ExpressionKind::identifier) {
        if (auto* symbol = lookup(expression.children[1].value)) {
          expression.children[1].binding = symbol->binding;
          expression.children[1].inferred_type = symbol->type;
          expression.children[1].element_type = symbol->element_type;
          expression.children[1].shape = symbol->shape;
        }
      } else if (expression.children.size() == 2) {
        analyze_expression(expression.children[1]);
      }
      if (!valid) {
        diagnose(expression.location.line, "MPF2040",
                 "Fortran PRESENT requires one OPTIONAL dummy argument");
      }
      return expression.inferred_type = ValueType::boolean;
    }
    ValueType argument_type = ValueType::unknown;
    std::unordered_set<std::string> reference_actuals;
    for (std::size_t index = 1; index < expression.children.size(); ++index) {
      auto& argument = expression.children[index];
      const auto intent_index = index - 1;
      const auto intent = intent_index < expression.argument_intents.size()
                              ? expression.argument_intents[intent_index]
                              : ParameterIntent::in;
      const bool writes_actual = intent == ParameterIntent::out || intent == ParameterIntent::inout;
      if (argument.kind == ExpressionKind::omitted_argument) continue;
      if (program_.language == SourceLanguage::fortran && called_function != nullptr &&
          argument.kind == ExpressionKind::identifier && !function_optional_parameters_.empty() &&
          function_optional_parameters_.back().count(argument.value) != 0U) {
        const bool target_optional = intent_index < called_function->parameter_optional.size() &&
                                     called_function->parameter_optional[intent_index];
        if (target_optional) {
          expression.argument_optional_forward[intent_index] = true;
        } else {
          diagnose(argument.location.line, "MPF2040",
                   "an optional dummy actual requires an optional dummy target");
        }
      }
      if (program_.language == SourceLanguage::fortran && writes_actual) {
        if (argument.kind != ExpressionKind::identifier) {
          argument_type = join_types(argument_type, analyze_expression(argument));
        }
        const auto* root = root_container(argument);
        if (root == nullptr || root->kind != ExpressionKind::identifier ||
            (argument.kind != ExpressionKind::identifier &&
             (argument.kind != ExpressionKind::index ||
              (contains_slice(argument) && !has_direct_slice(argument))))) {
          diagnose(
              argument.location.line, "MPF2038",
              "Fortran OUT/INOUT actual argument must be a definable name, element, or section");
          continue;
        }
        if (!reference_actuals.insert(root->value).second) {
          diagnose(argument.location.line, "MPF2038",
                   "aliased Fortran OUT/INOUT actual argument storage is not supported");
        }
        auto* symbol = lookup(root->value);
        if (symbol == nullptr || symbol->binding != BindingKind::variable) {
          if (argument.kind == ExpressionKind::identifier) analyze_expression(argument);
          diagnose(argument.location.line, "MPF2038",
                   "Fortran OUT/INOUT actual argument is not a variable");
          continue;
        }
        if (argument.kind == ExpressionKind::identifier) {
          if (intent == ParameterIntent::inout) {
            argument_type = join_types(argument_type, analyze_expression(argument));
          } else {
            argument.binding = symbol->binding;
            argument.inferred_type = symbol->type;
            argument.element_type = symbol->element_type;
            argument.shape = symbol->shape;
            argument_type = join_types(argument_type, symbol->type);
          }
        }
        diagnose_fortran_parameter_write(root->value, argument.location.line);
        symbol->assigned = true;
      } else {
        argument_type = join_types(argument_type, analyze_expression(argument));
      }
    }
    if (program_.language == SourceLanguage::fortran && called_function != nullptr) {
      const auto comparable =
          std::min(expression.children.size() - 1, called_function->parameter_types.size());
      for (std::size_t index = 0; index < comparable; ++index) {
        const auto& argument = expression.children[index + 1];
        if (argument.kind == ExpressionKind::omitted_argument) continue;
        const auto expected_type = called_function->parameter_types[index];
        if (expected_type == ValueType::list) {
          if (argument.inferred_type != ValueType::list &&
              argument.inferred_type != ValueType::unknown) {
            diagnose(argument.location.line, "MPF2039",
                     "Fortran array dummy argument requires an array actual");
            continue;
          }
          const auto& expected_shape = called_function->parameter_shapes[index];
          if (!expected_shape.empty() && !argument.shape.empty() &&
              expected_shape.size() != argument.shape.size()) {
            diagnose(argument.location.line, "MPF2039",
                     "Fortran dummy and actual array ranks do not match");
            continue;
          }
          for (std::size_t dimension = 0;
               dimension < expected_shape.size() && dimension < argument.shape.size();
               ++dimension) {
            if (expected_shape[dimension] != dynamic_extent &&
                argument.shape[dimension] != dynamic_extent &&
                expected_shape[dimension] != argument.shape[dimension]) {
              diagnose(argument.location.line, "MPF2039",
                       "Fortran dummy and actual array extents do not match");
              break;
            }
          }
          const auto expected_element = called_function->parameter_element_types[index];
          if (expected_element != ValueType::unknown &&
              argument.element_type != ValueType::unknown &&
              join_types(expected_element, argument.element_type) == ValueType::unknown) {
            diagnose(argument.location.line, "MPF2039",
                     "Fortran dummy and actual array element types do not match");
          }
        } else if (expected_type != ValueType::unknown &&
                   argument.inferred_type == ValueType::list) {
          diagnose(argument.location.line, "MPF2039",
                   "Fortran scalar dummy argument cannot receive an array actual");
        }
      }
    }
    if (associated_callee.kind == ExpressionKind::identifier &&
        associated_callee.binding == BindingKind::builtin) {
      if (associated_callee.intrinsic == IntrinsicId::python_float) {
        if (expression.children.size() != 2) {
          diagnose(expression.location.line, "MPF2033",
                   "Python float requires exactly one argument");
        } else {
          const auto type = expression.children[1].inferred_type;
          if (type != ValueType::integer && type != ValueType::real && type != ValueType::boolean &&
              type != ValueType::string && type != ValueType::unknown) {
            diagnose(expression.location.line, "MPF2033",
                     "Python float argument is not convertible in the current subset");
          }
        }
        return expression.inferred_type = ValueType::real;
      }
      if (associated_callee.intrinsic == IntrinsicId::python_length ||
          associated_callee.intrinsic == IntrinsicId::matlab_length ||
          associated_callee.intrinsic == IntrinsicId::element_count) {
        if (expression.children.size() != 2) {
          diagnose(expression.location.line, "MPF2026",
                   "length/size builtin requires one argument");
        } else if (expression.children[1].inferred_type != ValueType::list &&
                   expression.children[1].inferred_type != ValueType::unknown) {
          diagnose(expression.location.line, "MPF2022",
                   "length/size argument is not an array/list");
        }
        return expression.inferred_type = ValueType::integer;
      }
      if (associated_callee.intrinsic == IntrinsicId::sum && expression.children.size() == 2) {
        if (expression.children[1].inferred_type != ValueType::list &&
            expression.children[1].inferred_type != ValueType::unknown) {
          diagnose(expression.location.line, "MPF2022", "sum argument is not an array/list");
        } else if (expression.children[1].shape.size() > 1 &&
                   program_.language != SourceLanguage::fortran) {
          diagnose(expression.location.line, "MPF2028",
                   "multidimensional SUM semantics are not supported for this source language");
        }
        return expression.inferred_type = expression.children[1].element_type;
      }
      if (associated_callee.intrinsic == IntrinsicId::sum && expression.children.size() != 2) {
        diagnose(expression.location.line, "MPF2026", "sum builtin requires one argument");
        return expression.inferred_type = ValueType::unknown;
      }
      if (associated_callee.intrinsic == IntrinsicId::reshape) {
        return analyze_reshape(expression);
      }
      if (associated_callee.intrinsic == IntrinsicId::absolute ||
          associated_callee.intrinsic == IntrinsicId::minimum ||
          associated_callee.intrinsic == IntrinsicId::maximum) {
        return expression.inferred_type = argument_type;
      }
      return expression.inferred_type = ValueType::real;
    }
    if (associated_callee.binding == BindingKind::variable &&
        associated_callee.inferred_type == ValueType::list) {
      diagnose(expression.location.line, "MPF2025", "array/list indexing syntax is invalid");
    }
    if (called_function != nullptr) {
      const auto* function = called_function;
      const auto argument_count = expression.children.size() - 1;
      if (argument_count != function->parameters.size()) {
        diagnose(expression.location.line, "MPF2034",
                 "function '" + associated_callee.value + "' expects " +
                     std::to_string(function->parameters.size()) +
                     " input arguments but received " + std::to_string(argument_count));
      }
      if (program_.language == SourceLanguage::fortran) {
        const bool call_statement = fortran_call_expression_ == &expression;
        const bool subroutine = function->return_names.empty();
        if (call_statement != subroutine) {
          diagnose(expression.location.line, "MPF2037",
                   call_statement ? "Fortran CALL requires a SUBROUTINE"
                                  : "Fortran SUBROUTINE references require a CALL statement");
        }
      }
      if (program_.language == SourceLanguage::matlab && function->return_names.size() > 1) {
        expression.multi_output_call = true;
        expression.tuple_types = function->return_types;
        expression.tuple_element_types = function->return_element_types;
        expression.tuple_shapes = function->return_shapes;
        if (expression.requested_outputs > function->return_names.size()) {
          diagnose(expression.location.line, "MPF2034",
                   "function '" + associated_callee.value + "' provides " +
                       std::to_string(function->return_names.size()) +
                       " outputs but the assignment requests " +
                       std::to_string(expression.requested_outputs));
        }
        if (expression.requested_outputs == 1 && !function->return_types.empty()) {
          expression.element_type = function->return_element_types.front();
          expression.shape = function->return_shapes.front();
          return expression.inferred_type = function->return_types.front();
        }
        return expression.inferred_type = ValueType::tuple;
      }
      if (program_.language == SourceLanguage::python &&
          function->declared_type == ValueType::tuple) {
        expression.tuple_types = function->return_types;
        expression.tuple_element_types = function->return_element_types;
        expression.tuple_shapes = function->return_shapes;
      }
      if (program_.language == SourceLanguage::python &&
          (function->declared_type == ValueType::tuple ||
           function->declared_type == ValueType::list)) {
        expression.element_type = function->element_type;
        expression.shape = function->shape;
        expression.sequence_is_list = function->return_sequence_is_list;
        expression.sequence_elements = function->return_sequence_elements;
      }
      return expression.inferred_type = function->declared_type;
    }
    return expression.inferred_type = ValueType::unknown;
  }

  ValueType analyze_index(Expression& expression, const bool container_already_analyzed = false) {
    if (expression.children.size() < 2) {
      diagnose(expression.location.line, "MPF2025", "index expression requires at least one index");
      return expression.inferred_type = ValueType::unknown;
    }
    auto& container = expression.children[0];
    if (!container_already_analyzed) analyze_expression(container);
    if (container.inferred_type != ValueType::list &&
        container.inferred_type != ValueType::unknown) {
      diagnose(expression.location.line, "MPF2022", "indexed expression is not an array/list");
      return expression.inferred_type = ValueType::unknown;
    }
    const auto index_count = expression.children.size() - 1;
    if (!container.shape.empty() && index_count > container.shape.size()) {
      diagnose(expression.location.line, "MPF2025", "too many indexes for array/list shape");
    }
    if (program_.language == SourceLanguage::fortran && !container.shape.empty() &&
        index_count != container.shape.size()) {
      diagnose(expression.location.line, "MPF2025",
               "Fortran array reference rank does not match its declared rank");
    }
    bool has_slice = false;
    std::vector<std::size_t> result_shape;
    for (std::size_t position = 0; position < index_count; ++position) {
      auto& index = expression.children[position + 1];
      std::size_t extent = dynamic_extent;
      if (expression.column_major && index_count == 1 && container.shape.size() > 1) {
        extent = 1;
        for (const auto dimension : container.shape) {
          if (dimension == dynamic_extent) {
            extent = dynamic_extent;
            break;
          }
          if (dimension != 0 && extent > std::numeric_limits<std::size_t>::max() / dimension) {
            diagnose(expression.location.line, "MPF2027", "array shape exceeds target size limits");
            extent = dynamic_extent;
            break;
          }
          extent *= dimension;
        }
      } else if (position < container.shape.size()) {
        extent = container.shape[position];
      }
      if (index.kind == ExpressionKind::slice) {
        has_slice = true;
        result_shape.push_back(analyze_slice(index, extent));
        continue;
      }
      const auto index_type = analyze_expression(index);
      if (index_type != ValueType::integer && index_type != ValueType::unknown) {
        diagnose(index.location.line, "MPF2023", "array/list index must be an integer");
      }
      validate_static_index(index.location.line, index, extent, expression.index_base,
                            expression.allow_negative_index);
    }
    if (has_slice && index_count > 2) {
      diagnose(expression.location.line, "MPF2029",
               "array sections with more than two direct selectors are not yet supported");
    }
    expression.element_type = container.element_type;
    if (container.shape.empty()) {
      if (has_slice) {
        expression.shape = std::move(result_shape);
        return expression.inferred_type = ValueType::list;
      }
      return expression.inferred_type = ValueType::unknown;
    }
    if (!(expression.column_major && index_count == 1 && container.shape.size() > 1) &&
        index_count < container.shape.size()) {
      result_shape.insert(result_shape.end(),
                          container.shape.begin() + static_cast<std::ptrdiff_t>(index_count),
                          container.shape.end());
    }
    if (has_slice || !result_shape.empty()) {
      expression.shape = std::move(result_shape);
      return expression.inferred_type = ValueType::list;
    }
    return expression.inferred_type = container.element_type;
  }

  void analyze_section_assignment(Statement& statement, const ValueType value_type) {
    auto& target = statement.target_expression;
    const bool replacement_is_list = value_type == ValueType::list;
    const auto replacement_element =
        replacement_is_list ? statement.expression.element_type : value_type;
    if (program_.language == SourceLanguage::python && !replacement_is_list) {
      diagnose(statement.line, "MPF2031",
               "Python slice assignment requires a list replacement in the current subset");
    }
    if (target.element_type != ValueType::unknown && replacement_element != ValueType::unknown &&
        join_types(target.element_type, replacement_element) == ValueType::unknown) {
      diagnose(statement.line, "MPF2020", "section assignment changes the array element type");
    }

    if (replacement_is_list) {
      const auto normalized_replacement =
          assignment_shape(statement.expression.shape, target.shape.size());
      if (program_.language == SourceLanguage::python) {
        bool extended_slice = false;
        for (std::size_t index = 1; index < target.children.size(); ++index) {
          const auto& selector = target.children[index];
          if (selector.kind != ExpressionKind::slice || selector.children.size() != 3 ||
              !selector.children[2].valid())
            continue;
          const auto step = numeric_constant(selector.children[2]);
          if (step.has_value() && *step != 1.0) extended_slice = true;
        }
        if (extended_slice && !target.shape.empty() && !normalized_replacement.empty() &&
            target.shape.front() != dynamic_extent &&
            normalized_replacement.front() != dynamic_extent &&
            target.shape.front() != normalized_replacement.front()) {
          diagnose(
              statement.line, "MPF2031",
              "Python extended slice assignment requires the same number of replacement elements");
        }
      } else if (known_shape(target.shape) && known_shape(normalized_replacement) &&
                 target.shape != normalized_replacement) {
        diagnose(statement.line, "MPF2031",
                 "section assignment replacement shape is not conformable with the selected shape");
      }
    }

    if (program_.language == SourceLanguage::python) {
      const auto* root = root_container(target);
      if (root != nullptr && root->kind == ExpressionKind::identifier) {
        if (auto* symbol = lookup(root->value)) symbol->shape.clear();
      }
    }
  }

  std::size_t analyze_slice(Expression& slice, const std::size_t extent) {
    slice.inferred_type = ValueType::list;
    for (auto& bound : slice.children) {
      if (!bound.valid()) continue;
      const auto type = analyze_expression(bound);
      if (type != ValueType::integer && type != ValueType::unknown) {
        diagnose(bound.location.line, "MPF2023", "slice bounds and step must be integers");
      }
    }
    if (slice.children.size() != 3 || extent == dynamic_extent) return dynamic_extent;
    const auto constant = [](const Expression& expression) -> std::optional<long long> {
      if (!expression.valid()) return std::nullopt;
      const auto value = numeric_constant(expression);
      if (!value.has_value() ||
          *value < static_cast<double>(std::numeric_limits<long long>::min()) ||
          *value > static_cast<double>(std::numeric_limits<long long>::max()))
        return std::nullopt;
      const auto integral = static_cast<long long>(*value);
      if (static_cast<double>(integral) != *value) return std::nullopt;
      return integral;
    };
    const auto start_value = constant(slice.children[0]);
    const auto stop_value = constant(slice.children[1]);
    const auto step_value = constant(slice.children[2]);
    for (std::size_t position = 0; position < slice.children.size(); ++position) {
      const auto& bound = slice.children[position];
      const auto normalized = position == 0 ? start_value : position == 1 ? stop_value : step_value;
      if (bound.valid() && numeric_constant(bound).has_value() && !normalized.has_value()) {
        diagnose(bound.location.line, "MPF2027",
                 "slice bound or step exceeds target integer range");
      }
    }
    if ((slice.children[0].valid() && !start_value.has_value()) ||
        (slice.children[1].valid() && !stop_value.has_value()) ||
        (slice.children[2].valid() && !step_value.has_value()))
      return dynamic_extent;
    const auto step = step_value.value_or(1);
    if (step == 0) {
      diagnose(slice.location.line, "MPF2030", "slice step cannot be zero");
      return 0;
    }
    if (extent == 0) return 0;

    if (!slice.slice_stop_inclusive) {
      const auto size = static_cast<long long>(extent);
      auto start = start_value.value_or(step > 0 ? 0 : size - 1);
      auto stop = stop_value.value_or(step > 0 ? size : -1);
      if (start_value.has_value() && start < 0) start += size;
      if (stop_value.has_value() && stop < 0) stop += size;
      if (step > 0) {
        start = std::max(0LL, std::min(start, size));
        stop = std::max(0LL, std::min(stop, size));
        return start < stop ? static_cast<std::size_t>((stop - start - 1) / step + 1) : 0U;
      }
      start = std::max(-1LL, std::min(start, size - 1));
      stop = std::max(-1LL, std::min(stop, size - 1));
      return start > stop ? static_cast<std::size_t>((start - stop - 1) / (-step) + 1) : 0U;
    }

    const auto first =
        start_value.value_or(step > 0 ? static_cast<long long>(slice.index_base)
                                      : static_cast<long long>(slice.index_base + extent - 1));
    const auto stop =
        stop_value.value_or(step > 0 ? static_cast<long long>(slice.index_base + extent - 1)
                                     : static_cast<long long>(slice.index_base));
    if ((step > 0 && first > stop) || (step < 0 && first < stop)) return 0;
    const auto distance = step > 0 ? stop - first : first - stop;
    const auto count = static_cast<std::size_t>(distance / std::llabs(step) + 1);
    Expression first_expression;
    first_expression.kind = ExpressionKind::number_literal;
    first_expression.value = std::to_string(first);
    first_expression.location = slice.location;
    Expression last_expression = first_expression;
    last_expression.value = std::to_string(first + static_cast<long long>(count - 1) * step);
    validate_static_index(slice.location.line, first_expression, extent, slice.index_base,
                          slice.allow_negative_index);
    validate_static_index(slice.location.line, last_expression, extent, slice.index_base,
                          slice.allow_negative_index);
    slice.shape = {count};
    return count;
  }

  void validate_static_index(const std::size_t line, const Expression& index,
                             const std::size_t extent, const std::size_t base,
                             const bool allow_negative) {
    if (extent == dynamic_extent) return;
    const auto constant = numeric_constant(index);
    if (!constant.has_value()) return;
    if (*constant < static_cast<double>(std::numeric_limits<long long>::min()) ||
        *constant > static_cast<double>(std::numeric_limits<long long>::max())) {
      diagnose(line, "MPF2021", "constant array/list index is out of bounds");
      return;
    }
    const auto integral = static_cast<long long>(*constant);
    if (static_cast<double>(integral) != *constant) return;
    long long normalized = integral - static_cast<long long>(base);
    if (allow_negative && integral < 0) normalized = static_cast<long long>(extent) + integral;
    if (normalized < 0 || static_cast<std::size_t>(normalized) >= extent) {
      diagnose(line, "MPF2021", "constant array/list index is out of bounds");
    }
  }

  ValueType analyze_reshape(Expression& expression) {
    const bool matlab_dimensions =
        program_.language == SourceLanguage::matlab && expression.children.size() == 4;
    if ((!matlab_dimensions && expression.children.size() != 3) || expression.children.size() < 3) {
      diagnose(expression.location.line, "MPF2026",
               "RESHAPE requires a source and one shape vector or two dimensions");
      return expression.inferred_type = ValueType::unknown;
    }
    auto& source = expression.children[1];
    if (source.inferred_type != ValueType::list ||
        (!matlab_dimensions && expression.children[2].inferred_type != ValueType::list)) {
      diagnose(expression.location.line, "MPF2022",
               "RESHAPE source and shape-vector arguments must be arrays/lists");
      return expression.inferred_type = ValueType::unknown;
    }
    if (source.shape.size() > 1) {
      diagnose(expression.location.line, "MPF2028",
               "the current RESHAPE subset requires a rank-one source array/list");
      return expression.inferred_type = ValueType::unknown;
    }
    std::vector<std::size_t> dimensions;
    std::vector<const Expression*> dimension_expressions;
    if (matlab_dimensions) {
      dimension_expressions = {&expression.children[2], &expression.children[3]};
    } else {
      for (const auto& dimension : expression.children[2].children) {
        dimension_expressions.push_back(&dimension);
      }
    }
    for (const auto* dimension : dimension_expressions) {
      const auto value = numeric_constant(*dimension);
      if (!value.has_value() || *value <= 0.0 ||
          *value > static_cast<double>(std::numeric_limits<std::size_t>::max()) ||
          *value != static_cast<double>(static_cast<std::size_t>(*value))) {
        diagnose(dimension->location.line, "MPF2027",
                 "RESHAPE dimensions must be positive integer constants");
        return expression.inferred_type = ValueType::unknown;
      }
      dimensions.push_back(static_cast<std::size_t>(*value));
    }
    if (dimensions.empty() || dimensions.size() > 2) {
      diagnose(expression.location.line, "MPF2027",
               "only one- and two-dimensional RESHAPE is supported");
      return expression.inferred_type = ValueType::unknown;
    }
    std::size_t source_size = 1;
    bool source_size_known = !source.shape.empty() && known_shape(source.shape);
    for (const auto dimension : source.shape) {
      if (!source_size_known) break;
      if (dimension != 0 && source_size > std::numeric_limits<std::size_t>::max() / dimension) {
        diagnose(expression.location.line, "MPF2027", "RESHAPE source exceeds target size limits");
        return expression.inferred_type = ValueType::unknown;
      }
      source_size *= dimension;
    }
    std::size_t result_size = 1;
    for (const auto dimension : dimensions) {
      if (dimension != 0 && result_size > std::numeric_limits<std::size_t>::max() / dimension) {
        diagnose(expression.location.line, "MPF2027", "RESHAPE result exceeds target size limits");
        return expression.inferred_type = ValueType::unknown;
      }
      result_size *= dimension;
    }
    if (source_size_known && source_size != result_size) {
      diagnose(expression.location.line, "MPF2024",
               "RESHAPE source size does not match result shape");
    }
    expression.inferred_type = ValueType::list;
    expression.element_type = source.element_type;
    expression.shape = std::move(dimensions);
    expression.column_major = true;
    return expression.inferred_type;
  }

  Program& program_;
  std::vector<Scope> scopes_;
  std::vector<Diagnostic> diagnostics_;
  std::size_t loop_depth_{0};
  std::size_t function_depth_{0};
  std::vector<std::unordered_set<std::string>> function_parameters_;
  std::vector<std::vector<std::string>> function_parameter_order_;
  std::vector<std::vector<bool>> function_parameter_reads_;
  std::vector<std::vector<bool>> function_parameter_writes_;
  std::vector<std::vector<ParameterIntent>*> function_parameter_intents_;
  std::vector<std::unordered_set<std::string>> function_optional_parameters_;
  const Expression* fortran_call_expression_{nullptr};
};

}  // namespace

AnalysisResult analyze_program(hir::Program& program) {
  AnalysisResult result;
  result.diagnostics = Analyzer(program).analyze();
  hir::reindex(program);
  result.semantics = hir::extract_semantics(program);
  auto verifier_diagnostics = hir::verify_semantics(program, result.semantics, "analysis");
  result.diagnostics.insert(result.diagnostics.end(),
                            std::make_move_iterator(verifier_diagnostics.begin()),
                            std::make_move_iterator(verifier_diagnostics.end()));
  return result;
}

const char* to_string(const ValueType type) noexcept {
  switch (type) {
    case ValueType::unknown: return "unknown";
    case ValueType::integer: return "integer";
    case ValueType::real: return "real";
    case ValueType::boolean: return "boolean";
    case ValueType::string: return "string";
    case ValueType::null_value: return "null";
    case ValueType::list: return "list";
    case ValueType::tuple: return "tuple";
    case ValueType::function: return "function";
  }
  return "unknown";
}

}  // namespace mpf::detail
