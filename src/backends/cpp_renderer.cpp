#include "cpp_renderer.hpp"

#include <algorithm>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "cpp_lir.hpp"
#include "identifier_mangler.hpp"
#include "mpf/version.hpp"

namespace mpf::detail {
namespace {

using Expression = cpp::lir::Expression;
using Statement = cpp::lir::Statement;
using Program = cpp::lir::SemanticProgram;

struct Declaration {
  Declaration(std::string declaration_name, const Expression* declaration_initializer,
              const ValueType declaration_type, const ValueType declaration_element_type,
              std::vector<std::size_t> declaration_shape,
              const std::size_t declaration_tuple_index = dynamic_extent,
              const AssignmentPattern* declaration_pattern_leaf = nullptr)
      : name(std::move(declaration_name)),
        initializer(declaration_initializer),
        explicit_type(declaration_type),
        element_type(declaration_element_type),
        shape(std::move(declaration_shape)),
        tuple_index(declaration_tuple_index),
        pattern_leaf(declaration_pattern_leaf) {}

  std::string name;
  const Expression* initializer{nullptr};
  ValueType explicit_type{ValueType::unknown};
  ValueType element_type{ValueType::unknown};
  std::vector<std::size_t> shape;
  std::size_t tuple_index{dynamic_extent};
  const AssignmentPattern* pattern_leaf{nullptr};
};

void collect_declarations(const std::vector<Statement>& statements,
                          const std::set<std::string>& excluded, std::set<std::string>& found,
                          std::vector<Declaration>& declarations) {
  for (const auto& statement : statements) {
    if (statement.kind == StatementKind::declaration && excluded.count(statement.name) == 0U &&
        found.insert(statement.name).second) {
      declarations.push_back({statement.name,
                              statement.has_expression ? &statement.expression : nullptr,
                              statement.declared_type, statement.element_type, statement.shape});
    } else if (statement.kind == StatementKind::assignment &&
               excluded.count(statement.name) == 0U && found.insert(statement.name).second) {
      declarations.push_back({statement.name, &statement.expression, statement.declared_type,
                              statement.element_type, statement.shape});
    } else if (statement.kind == StatementKind::multi_assignment) {
      if (statement.has_target_pattern) {
        std::vector<const AssignmentPattern*> leaves;
        collect_assignment_leaves(statement.target_pattern, leaves);
        for (const auto* leaf : leaves) {
          if (excluded.count(leaf->name) != 0U || !found.insert(leaf->name).second) continue;
          declarations.emplace_back(leaf->name, &statement.expression, leaf->type,
                                    leaf->element_type, leaf->shape, dynamic_extent, leaf);
        }
      } else {
        for (std::size_t index = 0; index < statement.target_names.size(); ++index) {
          const auto& name = statement.target_names[index];
          if (excluded.count(name) != 0U || !found.insert(name).second) continue;
          const auto type = index < statement.target_types.size() ? statement.target_types[index]
                                                                  : ValueType::unknown;
          const auto element_type = index < statement.target_element_types.size()
                                        ? statement.target_element_types[index]
                                        : ValueType::unknown;
          const auto shape = index < statement.target_shapes.size() ? statement.target_shapes[index]
                                                                    : std::vector<std::size_t>{};
          declarations.emplace_back(name, &statement.expression, type, element_type, shape, index);
        }
      }
    } else if (statement.kind == StatementKind::if_statement ||
               statement.kind == StatementKind::while_loop) {
      collect_declarations(statement.body, excluded, found, declarations);
      collect_declarations(statement.alternative, excluded, found, declarations);
    } else if (statement.kind == StatementKind::select_case ||
               statement.kind == StatementKind::case_clause) {
      collect_declarations(statement.body, excluded, found, declarations);
    } else if (statement.kind == StatementKind::range_loop) {
      if (excluded.count(statement.name) == 0U && found.insert(statement.name).second) {
        declarations.push_back({statement.name, &statement.expression, statement.declared_type,
                                statement.element_type, statement.shape});
      }
      auto loop_excluded = excluded;
      loop_excluded.insert(statement.name);
      collect_declarations(statement.body, loop_excluded, found, declarations);
      collect_declarations(statement.alternative, excluded, found, declarations);
    }
  }
}

bool has_executable_statements(const Program& program) {
  for (const auto& statement : program.statements) {
    if (statement.kind != StatementKind::function) return true;
  }
  return false;
}

bool expression_has_direct_slice(const Expression& expression) {
  return expression.kind == ExpressionKind::index && expression.children.size() > 1 &&
         std::any_of(expression.children.begin() + 1, expression.children.end(),
                     [](const Expression& child) { return child.kind == ExpressionKind::slice; });
}

class Renderer final {
 public:
  explicit Renderer(const TranspileOptions& options) : options_(options) {}

  RenderedOutput render(const Program& program) {
    emission_ = program.emission;
    mangler_ = std::make_unique<IdentifierMangler>(program.identifiers);
    if (options_.emit_source_banner) {
      output_ << "// Generated by MPF " << MPF_VERSION_STRING << " from "
              << to_string(program.source_language) << " as portable C++17.\n";
    }
    output_ << "#include <algorithm>\n"
               "#include <cmath>\n"
               "#include <cstddef>\n"
               "#include <cstdint>\n"
               "#include <iostream>\n"
               "#include <limits>\n"
               "#include <numeric>\n"
               "#include <optional>\n"
               "#include <stdexcept>\n"
               "#include <string>\n"
               "#include <type_traits>\n"
               "#include <tuple>\n"
               "#include <utility>\n"
               "#include <vector>\n\n";
    emit_runtime(program.emission.dynamic_truthiness);
    output_ << "namespace mpf_generated {\n";
    indent_ = 1;
    const auto& function_graph = program.function_graph;
    if (program.emission.module_top_level) {
      emit_scope_declarations(program.statements, {}, "[[maybe_unused]] static ");
      if (has_executable_statements(program)) output_ << '\n';
    }
    bool emitted_declaration = false;
    for (const auto index : function_graph.definition_order) {
      if (!function_graph.recursive[index] ||
          !has_explicit_recursive_return(program.statements[index]))
        continue;
      emit_function_declaration(program.statements[index]);
      emitted_declaration = true;
    }
    if (emitted_declaration) output_ << '\n';
    for (const auto index : function_graph.definition_order) {
      emit_function(program.statements[index], function_graph.recursive[index]);
      output_ << '\n';
    }
    if (has_executable_statements(program)) {
      indentation();
      output_ << "int run() {\n";
      indent_ = 2;
      if (program.emission.entry_function_top_level) {
        emit_scope_declarations(program.statements, {});
      }
      for (const auto& statement : program.statements) {
        if (statement.kind != StatementKind::function) emit_statement(statement);
      }
      indentation();
      output_ << "return 0;\n";
      indent_ = 1;
      indentation();
      output_ << "}\n";
    }
    indent_ = 0;
    output_ << "}  // namespace mpf_generated\n";
    if (has_executable_statements(program)) {
      output_ << "\nint main() { return mpf_generated::run(); }\n";
    }
    return {output_.str(), std::move(markers_)};
  }

 private:
  void emit_runtime(const bool include_python_runtime) {
    output_
        << "namespace mpf_runtime {\n"
           "template <typename T> class optional_argument {\n"
           " public:\n"
           "  optional_argument() = default;\n"
           "  optional_argument(std::nullopt_t) noexcept {}\n"
           "  optional_argument(T& value) noexcept : pointer_(&value) {}\n"
           "  optional_argument(const T& value) : owned_(value), pointer_(&owned_.value()) {}\n"
           "  optional_argument(T&& value) : owned_(std::move(value)), pointer_(&owned_.value()) "
           "{}\n"
           "  optional_argument(const optional_argument& other) : owned_(other.owned_) {\n"
           "    pointer_ = other.owns_value() ? &owned_.value() : other.pointer_;\n"
           "  }\n"
           "  optional_argument(optional_argument&& other) {\n"
           "    const bool owned = other.owns_value();\n"
           "    owned_ = std::move(other.owned_);\n"
           "    pointer_ = owned ? &owned_.value() : other.pointer_;\n"
           "  }\n"
           "  optional_argument& operator=(const optional_argument& other) {\n"
           "    if (this == &other) return *this;\n"
           "    const bool owned = other.owns_value();\n"
           "    owned_ = other.owned_;\n"
           "    pointer_ = owned ? &owned_.value() : other.pointer_;\n"
           "    return *this;\n"
           "  }\n"
           "  optional_argument& operator=(optional_argument&& other) {\n"
           "    if (this == &other) return *this;\n"
           "    const bool owned = other.owns_value();\n"
           "    owned_ = std::move(other.owned_);\n"
           "    pointer_ = owned ? &owned_.value() : other.pointer_;\n"
           "    return *this;\n"
           "  }\n"
           "  bool has_value() const noexcept { return pointer_ != nullptr; }\n"
           "  T& value() { if (pointer_ == nullptr) throw std::bad_optional_access{}; return "
           "*pointer_; }\n"
           "  const T& value() const {\n"
           "    if (pointer_ == nullptr) throw std::bad_optional_access{};\n"
           "    return *pointer_;\n"
           "  }\n"
           " private:\n"
           "  bool owns_value() const noexcept {\n"
           "    return owned_.has_value() && pointer_ == &owned_.value();\n"
           "  }\n"
           "  std::optional<T> owned_;\n"
           "  T* pointer_{nullptr};\n"
           "};\n\n"
           "template <typename T> void print_value(const T& value);\n"
           "template <typename T> void print_value(const std::vector<T>& values);\n"
           "template <typename... T> void print_value(const std::tuple<T...>& values);\n\n"
           "template <typename T> void print_value(const T& value) { std::cout << value; }\n"
           "template <typename T> void print_value(const std::vector<T>& values) {\n"
           "  std::cout << '[';\n"
           "  bool first = true;\n"
           "  for (const auto& value : values) {\n"
           "    if (!first) std::cout << \", \";\n"
           "    print_value(value);\n"
           "    first = false;\n"
           "  }\n"
           "  std::cout << ']';\n"
           "}\n"
           "template <typename... T> void print_value(const std::tuple<T...>& values) {\n"
           "  std::cout << '(';\n"
           "  bool first = true;\n"
           "  std::apply([&](const auto&... value) {\n"
           "    ((std::cout << (first ? \"\" : \", \"), print_value(value), first = false), ...);\n"
           "  }, values);\n"
           "  std::cout << ')';\n"
           "}\n"
           "inline void print() { std::cout << '\\n'; }\n"
           "template <typename... T> void print(const T&... value) {\n"
           "  bool first = true;\n"
           "  ((std::cout << (first ? \"\" : \" \"), print_value(value), first = false), ...);\n"
           "  std::cout << '\\n';\n"
           "}\n"
           "inline int fortran_compare(const std::string& left, const std::string& right) {\n"
           "  const auto width = std::max(left.size(), right.size());\n"
           "  for (std::size_t index = 0; index < width; ++index) {\n"
           "    const auto left_character = static_cast<unsigned char>(index < left.size() ? "
           "left[index] : ' ');\n"
           "    const auto right_character = static_cast<unsigned char>(index < right.size() ? "
           "right[index] : ' ');\n"
           "    if (left_character < right_character) return -1;\n"
           "    if (left_character > right_character) return 1;\n"
           "  }\n"
           "  return 0;\n"
           "}\n"
           "inline std::size_t normalize_index(std::size_t size, std::int64_t index, "
           "std::size_t base, bool allow_negative) {\n"
           "  std::int64_t normalized = index - static_cast<std::int64_t>(base);\n"
           "  if (allow_negative && index < 0) normalized = static_cast<std::int64_t>(size) + "
           "index;\n"
           "  if (normalized < 0 || static_cast<std::size_t>(normalized) >= size) "
           "throw std::out_of_range(\"MPF index out of bounds\");\n"
           "  return static_cast<std::size_t>(normalized);\n"
           "}\n"
           "template <typename T> T& index(std::vector<T>& values, std::int64_t position, "
           "std::size_t base, bool allow_negative) {\n"
           "  return values.at(normalize_index(values.size(), position, base, allow_negative));\n"
           "}\n"
           "template <typename T> const T& index(const std::vector<T>& values, "
           "std::int64_t position, std::size_t base, bool allow_negative) {\n"
           "  return values.at(normalize_index(values.size(), position, base, allow_negative));\n"
           "}\n"
           "inline std::vector<std::size_t> slice_indices("
           "std::size_t size, std::optional<std::int64_t> start_value, "
           "std::optional<std::int64_t> stop_value, std::optional<std::int64_t> step_value, "
           "std::size_t base, bool allow_negative, bool inclusive) {\n"
           "  const auto step = step_value.value_or(1);\n"
           "  if (step == 0) throw std::invalid_argument(\"MPF slice step cannot be zero\");\n"
           "  std::vector<std::size_t> result;\n"
           "  if (size == 0) return result;\n"
           "  if (size > static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max())) "
           "throw std::length_error(\"MPF array exceeds signed index range\");\n"
           "  const auto signed_size = static_cast<std::int64_t>(size);\n"
           "  if (!inclusive) {\n"
           "    auto start = start_value.value_or(step > 0 ? 0 : signed_size - 1);\n"
           "    auto stop = stop_value.value_or(step > 0 ? signed_size : -1);\n"
           "    if (start_value.has_value() && start < 0) start += signed_size;\n"
           "    if (stop_value.has_value() && stop < 0) stop += signed_size;\n"
           "    if (step > 0) {\n"
           "      start = std::max<std::int64_t>(0, std::min(start, signed_size));\n"
           "      stop = std::max<std::int64_t>(0, std::min(stop, signed_size));\n"
           "      for (auto position = start; position < stop; position += step) "
           "result.push_back(static_cast<std::size_t>(position));\n"
           "    } else {\n"
           "      start = std::max<std::int64_t>(-1, std::min(start, signed_size - 1));\n"
           "      stop = std::max<std::int64_t>(-1, std::min(stop, signed_size - 1));\n"
           "      for (auto position = start; position > stop; position += step) "
           "result.push_back(static_cast<std::size_t>(position));\n"
           "    }\n"
           "    return result;\n"
           "  }\n"
           "  auto position = start_value.value_or("
           "step > 0 ? static_cast<std::int64_t>(base) "
           ": static_cast<std::int64_t>(base + size - 1));\n"
           "  const auto stop = stop_value.value_or("
           "step > 0 ? static_cast<std::int64_t>(base + size - 1) "
           ": static_cast<std::int64_t>(base));\n"
           "  while (step > 0 ? position <= stop : position >= stop) {\n"
           "    result.push_back(normalize_index(size, position, base, allow_negative));\n"
           "    if (result.size() > size) throw std::out_of_range(\"MPF slice exceeds array "
           "extent\");\n"
           "    if ((step > 0 && position > std::numeric_limits<std::int64_t>::max() - step) ||\n"
           "        (step < 0 && position < std::numeric_limits<std::int64_t>::min() - step)) "
           "break;\n"
           "    position += step;\n"
           "  }\n"
           "  return result;\n"
           "}\n"
           "template <typename T> std::vector<T> slice("
           "const std::vector<T>& values, std::optional<std::int64_t> start, "
           "std::optional<std::int64_t> stop, std::optional<std::int64_t> step, "
           "std::size_t base, bool allow_negative, bool inclusive) {\n"
           "  std::vector<T> result;\n"
           "  for (const auto position : slice_indices(values.size(), start, stop, step, base, "
           "allow_negative, inclusive)) result.push_back(values.at(position));\n"
           "  return result;\n"
           "}\n"
           "inline std::pair<std::size_t, std::size_t> python_slice_window("
           "std::size_t size, std::optional<std::int64_t> start_value, "
           "std::optional<std::int64_t> stop_value) {\n"
           "  const auto signed_size = static_cast<std::int64_t>(size);\n"
           "  auto start = start_value.value_or(0);\n"
           "  auto stop = stop_value.value_or(signed_size);\n"
           "  if (start < 0) start += signed_size;\n"
           "  if (stop < 0) stop += signed_size;\n"
           "  start = std::max<std::int64_t>(0, std::min(start, signed_size));\n"
           "  stop = std::max<std::int64_t>(0, std::min(stop, signed_size));\n"
           "  if (stop < start) stop = start;\n"
           "  return {static_cast<std::size_t>(start), static_cast<std::size_t>(stop)};\n"
           "}\n"
           "template <typename T, typename U> void assign_slice("
           "std::vector<T>& values, std::optional<std::int64_t> start, "
           "std::optional<std::int64_t> stop, std::optional<std::int64_t> step, "
           "std::size_t base, bool allow_negative, bool inclusive, "
           "const std::vector<U>& replacement, bool python_resize) {\n"
           "  if (python_resize && step.value_or(1) == 1) {\n"
           "    const auto window = python_slice_window(values.size(), start, stop);\n"
           "    values.erase(values.begin() + static_cast<std::ptrdiff_t>(window.first), "
           "values.begin() + static_cast<std::ptrdiff_t>(window.second));\n"
           "    values.insert(values.begin() + static_cast<std::ptrdiff_t>(window.first), "
           "replacement.begin(), replacement.end());\n"
           "    return;\n"
           "  }\n"
           "  const auto indices = slice_indices(values.size(), start, stop, step, base, "
           "allow_negative, inclusive);\n"
           "  if (indices.size() != replacement.size()) "
           "throw std::invalid_argument(\"MPF section replacement size mismatch\");\n"
           "  for (std::size_t position = 0; position < indices.size(); ++position) "
           "values.at(indices[position]) = static_cast<T>(replacement[position]);\n"
           "}\n"
           "template <typename T, typename U> void assign_slice("
           "std::vector<T>& values, std::optional<std::int64_t> start, "
           "std::optional<std::int64_t> stop, std::optional<std::int64_t> step, "
           "std::size_t base, bool allow_negative, bool inclusive, "
           "const U& replacement, bool) {\n"
           "  for (const auto position : slice_indices(values.size(), start, stop, step, base, "
           "allow_negative, inclusive)) values.at(position) = static_cast<T>(replacement);\n"
           "}\n"
           "template <typename T, typename U> void assign_column("
           "std::vector<std::vector<T>>& values, std::optional<std::int64_t> row_start, "
           "std::optional<std::int64_t> row_stop, std::optional<std::int64_t> row_step, "
           "std::int64_t column_position, std::size_t base, bool allow_negative, bool inclusive, "
           "const std::vector<U>& replacement) {\n"
           "  const auto rows = slice_indices(values.size(), row_start, row_stop, row_step, base, "
           "allow_negative, inclusive);\n"
           "  if (rows.size() != replacement.size()) "
           "throw std::invalid_argument(\"MPF column replacement size mismatch\");\n"
           "  for (std::size_t position = 0; position < rows.size(); ++position) "
           "index(values.at(rows[position]), column_position, base, allow_negative) = "
           "static_cast<T>(replacement[position]);\n"
           "}\n"
           "template <typename T, typename U> void assign_column("
           "std::vector<std::vector<T>>& values, std::optional<std::int64_t> row_start, "
           "std::optional<std::int64_t> row_stop, std::optional<std::int64_t> row_step, "
           "std::int64_t column_position, std::size_t base, bool allow_negative, bool inclusive, "
           "const U& replacement) {\n"
           "  for (const auto row : slice_indices(values.size(), row_start, row_stop, row_step, "
           "base, "
           "allow_negative, inclusive)) "
           "index(values.at(row), column_position, base, allow_negative) = "
           "static_cast<T>(replacement);\n"
           "}\n"
           "template <typename T, typename U> void assign_block("
           "std::vector<std::vector<T>>& values, std::optional<std::int64_t> row_start, "
           "std::optional<std::int64_t> row_stop, std::optional<std::int64_t> row_step, "
           "std::optional<std::int64_t> column_start, std::optional<std::int64_t> column_stop, "
           "std::optional<std::int64_t> column_step, std::size_t base, bool allow_negative, "
           "bool inclusive, const std::vector<std::vector<U>>& replacement) {\n"
           "  const auto rows = slice_indices(values.size(), row_start, row_stop, row_step, base, "
           "allow_negative, inclusive);\n"
           "  if (rows.size() != replacement.size()) "
           "throw std::invalid_argument(\"MPF block row count mismatch\");\n"
           "  for (std::size_t position = 0; position < rows.size(); ++position) "
           "assign_slice(values.at(rows[position]), column_start, column_stop, column_step, base, "
           "allow_negative, inclusive, replacement[position], false);\n"
           "}\n"
           "template <typename T, typename U> void assign_block("
           "std::vector<std::vector<T>>& values, std::optional<std::int64_t> row_start, "
           "std::optional<std::int64_t> row_stop, std::optional<std::int64_t> row_step, "
           "std::optional<std::int64_t> column_start, std::optional<std::int64_t> column_stop, "
           "std::optional<std::int64_t> column_step, std::size_t base, bool allow_negative, "
           "bool inclusive, const U& replacement) {\n"
           "  for (const auto row : slice_indices(values.size(), row_start, row_stop, row_step, "
           "base, "
           "allow_negative, inclusive)) "
           "assign_slice(values.at(row), column_start, column_stop, column_step, base, "
           "allow_negative, inclusive, replacement, false);\n"
           "}\n"
           "template <typename T, typename U> void assign_linear_column_major("
           "std::vector<std::vector<T>>& values, std::optional<std::int64_t> start, "
           "std::optional<std::int64_t> stop, std::optional<std::int64_t> step, "
           "std::size_t base, bool inclusive, const std::vector<U>& replacement) {\n"
           "  if (values.empty()) return;\n"
           "  const auto indices = slice_indices(values.size() * values.front().size(), start, "
           "stop, "
           "step, base, false, inclusive);\n"
           "  if (indices.size() != replacement.size()) "
           "throw std::invalid_argument(\"MPF linear replacement size mismatch\");\n"
           "  for (std::size_t position = 0; position < indices.size(); ++position) {\n"
           "    const auto linear = indices[position];\n"
           "    values.at(linear % values.size()).at(linear / values.size()) = "
           "static_cast<T>(replacement[position]);\n"
           "  }\n"
           "}\n"
           "template <typename T, typename U> void assign_linear_column_major("
           "std::vector<std::vector<T>>& values, std::optional<std::int64_t> start, "
           "std::optional<std::int64_t> stop, std::optional<std::int64_t> step, "
           "std::size_t base, bool inclusive, const U& replacement) {\n"
           "  if (values.empty()) return;\n"
           "  for (const auto linear : slice_indices(values.size() * values.front().size(), start, "
           "stop, "
           "step, base, false, inclusive)) "
           "values.at(linear % values.size()).at(linear / values.size()) = "
           "static_cast<T>(replacement);\n"
           "}\n"
           "template <typename T> std::vector<T> column("
           "const std::vector<std::vector<T>>& values, std::int64_t position, "
           "std::size_t base, bool allow_negative) {\n"
           "  std::vector<T> result;\n"
           "  result.reserve(values.size());\n"
           "  for (const auto& row : values) result.push_back(index(row, position, base, "
           "allow_negative));\n"
           "  return result;\n"
           "}\n"
           "template <typename T> std::vector<std::vector<T>> columns("
           "const std::vector<std::vector<T>>& values, std::optional<std::int64_t> start, "
           "std::optional<std::int64_t> stop, std::optional<std::int64_t> step, "
           "std::size_t base, bool allow_negative, bool inclusive) {\n"
           "  std::vector<std::vector<T>> result;\n"
           "  result.reserve(values.size());\n"
           "  for (const auto& row : values) "
           "result.push_back(slice(row, start, stop, step, base, allow_negative, inclusive));\n"
           "  return result;\n"
           "}\n"
           "template <typename T> std::vector<T> flatten_column_major("
           "const std::vector<std::vector<T>>& values) {\n"
           "  std::vector<T> result;\n"
           "  if (values.empty()) return result;\n"
           "  const auto columns_count = values.front().size();\n"
           "  result.reserve(values.size() * columns_count);\n"
           "  for (std::size_t column_position = 0; column_position < columns_count; "
           "++column_position)\n"
           "    for (const auto& row : values) {\n"
           "      if (row.size() != columns_count) "
           "throw std::invalid_argument(\"MPF matrix must be rectangular\");\n"
           "      result.push_back(row.at(column_position));\n"
           "    }\n"
           "  return result;\n"
           "}\n"
           "template <typename T> T& matrix_linear_index(std::vector<std::vector<T>>& values, "
           "std::int64_t position, std::size_t base) {\n"
           "  if (values.empty() || values.front().empty()) "
           "throw std::out_of_range(\"MPF index out of bounds\");\n"
           "  const auto rows = values.size();\n"
           "  const auto columns = values.front().size();\n"
           "  const auto linear = normalize_index(rows * columns, position, base, false);\n"
           "  return values.at(linear % rows).at(linear / rows);\n"
           "}\n"
           "template <typename T> const T& matrix_linear_index("
           "const std::vector<std::vector<T>>& values, std::int64_t position, std::size_t base) {\n"
           "  if (values.empty() || values.front().empty()) "
           "throw std::out_of_range(\"MPF index out of bounds\");\n"
           "  const auto rows = values.size();\n"
           "  const auto columns = values.front().size();\n"
           "  const auto linear = normalize_index(rows * columns, position, base, false);\n"
           "  return values.at(linear % rows).at(linear / rows);\n"
           "}\n"
           "template <typename T> struct scalar_type { using type = T; };\n"
           "template <typename T> struct scalar_type<std::vector<T>> { "
           "using type = typename scalar_type<T>::type; };\n"
           "template <typename T> using scalar_type_t = typename scalar_type<T>::type;\n"
           "template <typename T> T sum_value(const T& value) { return value; }\n"
           "template <typename T> scalar_type_t<std::vector<T>> "
           "sum_value(const std::vector<T>& values) {\n"
           "  scalar_type_t<std::vector<T>> total{};\n"
           "  for (const auto& value : values) total += sum_value(value);\n"
           "  return total;\n"
           "}\n"
           "template <typename T> scalar_type_t<std::vector<T>> sum(const std::vector<T>& values) "
           "{\n"
           "  return sum_value(values);\n"
           "}\n"
           "template <typename T> std::size_t numel_value(const T&) { return 1; }\n"
           "template <typename T> std::size_t numel_value(const std::vector<T>& values) {\n"
           "  std::size_t total = 0;\n"
           "  for (const auto& value : values) total += numel_value(value);\n"
           "  return total;\n"
           "}\n"
           "template <typename T> std::size_t numel(const std::vector<T>& values) {\n"
           "  return numel_value(values);\n"
           "}\n"
           "template <typename T> struct is_vector : std::false_type {};\n"
           "template <typename T> struct is_vector<std::vector<T>> : std::true_type {};\n"
           "template <typename T> std::size_t length(const std::vector<T>& values) {\n"
           "  if constexpr (is_vector<T>::value) {\n"
           "    return values.empty() ? 0 : std::max(values.size(), values.front().size());\n"
           "  } else {\n"
           "    return values.size();\n"
           "  }\n"
           "}\n"
           "template <typename T> std::vector<std::vector<T>> reshape_column_major("
           "const std::vector<T>& values, std::size_t rows, std::size_t columns) {\n"
           "  if (rows * columns != values.size()) "
           "throw std::invalid_argument(\"MPF RESHAPE size mismatch\");\n"
           "  std::vector<std::vector<T>> result(rows, std::vector<T>(columns));\n"
           "  for (std::size_t column = 0; column < columns; ++column)\n"
           "    for (std::size_t row = 0; row < rows; ++row)\n"
           "      result[row][column] = values[row + column * rows];\n"
           "  return result;\n"
           "}\n";
    if (include_python_runtime) {
      output_
          << "inline bool truthy(std::nullptr_t) { return false; }\n"
             "inline bool truthy(bool value) { return value; }\n"
             "template <typename T, typename = std::enable_if_t<std::is_arithmetic<T>::value>> "
             "bool truthy(T value) { return value != static_cast<T>(0); }\n"
             "inline bool truthy(const std::string& value) { return !value.empty(); }\n"
             "template <typename T> bool truthy(const std::vector<T>& value) { return "
             "!value.empty(); }\n"
             "template <typename... T> bool truthy(const std::tuple<T...>&) { return sizeof...(T) "
             "!= 0; }\n"
             "template <typename T> bool py_not(const T& value) { return !truthy(value); }\n"
             "template <typename LeftFunction, typename RightFunction> auto py_and("
             "LeftFunction&& left_function, RightFunction&& right_function) {\n"
             "  auto left = left_function();\n"
             "  using Result = std::common_type_t<std::decay_t<decltype(left)>, "
             "std::decay_t<decltype(right_function())>>;\n"
             "  if (!truthy(left)) return static_cast<Result>(left);\n"
             "  return static_cast<Result>(right_function());\n"
             "}\n"
             "template <typename LeftFunction, typename RightFunction> auto py_or("
             "LeftFunction&& left_function, RightFunction&& right_function) {\n"
             "  auto left = left_function();\n"
             "  using Result = std::common_type_t<std::decay_t<decltype(left)>, "
             "std::decay_t<decltype(right_function())>>;\n"
             "  if (truthy(left)) return static_cast<Result>(left);\n"
             "  return static_cast<Result>(right_function());\n"
             "}\n"
             "template <typename T, typename = std::enable_if_t<std::is_arithmetic<T>::value>> "
             "double py_float(T value) { return static_cast<double>(value); }\n"
             "inline double py_float(const std::string& value) {\n"
             "  std::size_t consumed = 0;\n"
             "  const auto result = std::stod(value, &consumed);\n"
             "  if (value.find_first_not_of(\" \\t\\n\\r\\f\\v\", consumed) != std::string::npos) "
             "throw std::invalid_argument(\"MPF float string is invalid\");\n"
             "  return result;\n"
             "}\n";
    }
    output_ << "}  // namespace mpf_runtime\n\n";
  }

  void indentation() {
    for (std::size_t level = 0; level < indent_; ++level) output_ << "  ";
  }

  static int precedence(const Expression& expression) noexcept {
    if (expression.kind == ExpressionKind::conditional) return 0;
    if (expression.kind == ExpressionKind::binary) {
      if (expression.value == "||") return 1;
      if (expression.value == "&&") return 2;
      if (expression.value == "===" || expression.value == "!==" || expression.value == "<" ||
          expression.value == "<=" || expression.value == ">" || expression.value == ">=")
        return 3;
      if (expression.value == "+" || expression.value == "-") return 4;
      if (expression.value == "*" || expression.value == "/" || expression.value == "%") return 5;
    }
    if (expression.kind == ExpressionKind::unary) return 6;
    if (expression.kind == ExpressionKind::call || expression.kind == ExpressionKind::member ||
        expression.kind == ExpressionKind::index)
      return 9;
    return 10;
  }

  static const char* cpp_comparison_operator(const std::string& operation) noexcept {
    if (operation == "===") return "==";
    if (operation == "!==") return "!=";
    return operation.c_str();
  }

  void emit_comparison_chain(const Expression& expression) {
    std::vector<std::string> operands;
    operands.reserve(expression.children.size());
    output_ << "([&]() { ";
    for (std::size_t index = 0; index < expression.children.size(); ++index) {
      operands.push_back(mangler_->temporary("comparison"));
      output_ << "auto&& " << operands.back() << " = ";
      emit_expression(expression.children[index]);
      output_ << "; ";
      if (index == 0) continue;
      const auto* operation = cpp_comparison_operator(expression.operators[index - 1]);
      if (index < expression.children.size() - 1)
        output_ << "if (!(";
      else
        output_ << "return ";
      output_ << operands[index - 1] << ' ' << operation << ' ' << operands[index];
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
    const auto selector_count = target.children.size() - 1;
    if (selector_count == 1) {
      const auto& slice = target.children[1];
      if (target.column_major && target.children[0].shape.size() > 1) {
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
    if (row.kind != ExpressionKind::slice) {
      output_ << "mpf_runtime::assign_slice(";
      emit_runtime_index(target.children[0], row, target);
      output_ << ", ";
      emit_slice_parameters(column);
      output_ << ", ";
      emit_section_replacement(target, replacement);
      output_ << ", false)";
    } else if (column.kind != ExpressionKind::slice) {
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
    if (expression.kind != ExpressionKind::call) return false;
    for (std::size_t index = 1; index < expression.children.size(); ++index) {
      const auto intent_index = index - 1;
      const auto intent = intent_index < expression.argument_intents.size()
                              ? expression.argument_intents[intent_index]
                              : ParameterIntent::in;
      if ((intent == ParameterIntent::out || intent == ParameterIntent::inout) &&
          expression_has_direct_slice(expression.children[index])) {
        return true;
      }
    }
    return false;
  }

  void emit_section_reference_call(const Expression& expression) {
    auto raw_call = expression;
    raw_call.argument_intents.clear();
    std::vector<std::string> temporaries(expression.children.size());
    output_ << "([&]() { ";
    for (std::size_t index = 1; index < expression.children.size(); ++index) {
      const auto intent_index = index - 1;
      const auto intent = intent_index < expression.argument_intents.size()
                              ? expression.argument_intents[intent_index]
                              : ParameterIntent::in;
      if (intent != ParameterIntent::out && intent != ParameterIntent::inout) continue;
      if (!expression_has_direct_slice(expression.children[index])) continue;
      temporaries[index] = mangler_->temporary("section_reference");
      output_ << "auto " << temporaries[index] << " = ";
      emit_expression(expression.children[index]);
      output_ << "; ";
      raw_call.children[index] = expression.children[index];
      raw_call.children[index].kind = ExpressionKind::identifier;
      raw_call.children[index].value = temporaries[index];
      raw_call.children[index].children.clear();
      raw_call.children[index].binding = BindingKind::variable;
    }
    std::string result;
    if (expression.procedure_has_result) {
      result = mangler_->temporary("call_result");
      output_ << "auto " << result << " = ";
    }
    emit_expression(raw_call);
    output_ << "; ";
    for (std::size_t index = 1; index < temporaries.size(); ++index) {
      if (temporaries[index].empty()) continue;
      Expression replacement = expression.children[index];
      replacement.kind = ExpressionKind::identifier;
      replacement.value = temporaries[index];
      replacement.children.clear();
      replacement.binding = BindingKind::variable;
      emit_section_assignment(expression.children[index], replacement);
      output_ << "; ";
    }
    if (!result.empty()) output_ << "return " << result << "; ";
    output_ << "})()";
  }

  void emit_expression(const Expression& expression, const int parent = 0) {
    mark(expression.location, expression.origin);
    if (has_writable_section_actual(expression)) {
      emit_section_reference_call(expression);
      return;
    }
    if (expression.kind == ExpressionKind::call && expression.multi_output_call &&
        expression.requested_outputs == 1) {
      auto raw_call = expression;
      raw_call.multi_output_call = false;
      output_ << "std::get<0>(";
      emit_expression(raw_call);
      output_ << ')';
      return;
    }
    const auto own = precedence(expression);
    const bool parentheses = own < parent;
    if (parentheses) output_ << '(';
    switch (expression.kind) {
      case ExpressionKind::invalid: output_ << "0"; break;
      case ExpressionKind::omitted_argument: output_ << "std::nullopt"; break;
      case ExpressionKind::identifier:
        output_ << (expression.binding == BindingKind::builtin ? mapped_identifier(expression)
                                                               : mangler_->name(expression.value));
        if (expression.binding != BindingKind::builtin &&
            active_optional_parameters_.count(expression.value) != 0U) {
          output_ << ".value()";
        }
        break;
      case ExpressionKind::number_literal:
      case ExpressionKind::boolean_literal: output_ << expression.value; break;
      case ExpressionKind::string_literal:
        output_ << "std::string{" << expression.value << '}';
        break;
      case ExpressionKind::null_literal: output_ << "nullptr"; break;
      case ExpressionKind::unary:
        if (emission_.dynamic_truthiness && expression.value == "!") {
          output_ << "mpf_runtime::py_not(";
          if (!expression.children.empty()) emit_expression(expression.children.front());
          output_ << ')';
          break;
        }
        output_ << expression.value;
        if (!expression.children.empty()) emit_expression(expression.children.front(), own);
        break;
      case ExpressionKind::binary:
        if (emission_.operand_logical_result &&
            (expression.value == "&&" || expression.value == "||")) {
          output_ << (expression.value == "&&" ? "mpf_runtime::py_and" : "mpf_runtime::py_or")
                  << "([&]() { return (";
          emit_expression(expression.children[0]);
          output_ << "); }, [&]() { return (";
          emit_expression(expression.children[1]);
          output_ << "); })";
        } else if (expression.value == "**") {
          output_ << "std::pow(";
          emit_expression(expression.children[0]);
          output_ << ", ";
          emit_expression(expression.children[1]);
          output_ << ')';
        } else if (expression.value == "//") {
          output_ << "static_cast<std::int64_t>(std::floor(static_cast<double>(";
          emit_expression(expression.children[0]);
          output_ << ") / static_cast<double>(";
          emit_expression(expression.children[1]);
          output_ << ")))";
        } else if (expression.value == "/" && emission_.real_division) {
          output_ << "static_cast<double>(";
          emit_expression(expression.children[0]);
          output_ << ") / static_cast<double>(";
          emit_expression(expression.children[1]);
          output_ << ')';
        } else {
          emit_expression(expression.children[0], own);
          const auto cpp_operator = expression.value == "==="   ? "=="
                                    : expression.value == "!==" ? "!="
                                                                : expression.value;
          output_ << ' ' << cpp_operator << ' ';
          emit_expression(expression.children[1], own + 1);
        }
        break;
      case ExpressionKind::comparison_chain: emit_comparison_chain(expression); break;
      case ExpressionKind::conditional:
        output_ << "(mpf_runtime::truthy(";
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
              output_ << "mpf_runtime::py_float(";
              emit_expression(expression.children[1]);
              output_ << ')';
              break;
            }
            if (callee.binding == BindingKind::builtin &&
                callee.intrinsic == IntrinsicId::python_length && expression.children.size() == 2) {
              output_ << "static_cast<std::int64_t>(";
              emit_expression(expression.children[1]);
              output_ << ".size())";
              break;
            }
            if (callee.binding == BindingKind::builtin &&
                callee.intrinsic == IntrinsicId::matlab_length && expression.children.size() == 2) {
              output_ << "static_cast<std::int64_t>(mpf_runtime::length(";
              emit_expression(expression.children[1]);
              output_ << "))";
              break;
            }
            if (callee.binding == BindingKind::builtin &&
                callee.intrinsic == IntrinsicId::element_count && expression.children.size() == 2) {
              output_ << "static_cast<std::int64_t>(mpf_runtime::numel(";
              emit_expression(expression.children[1]);
              output_ << "))";
              break;
            }
            if (callee.binding == BindingKind::builtin && callee.intrinsic == IntrinsicId::sum &&
                expression.children.size() == 2) {
              output_ << "mpf_runtime::sum(";
              emit_expression(expression.children[1]);
              output_ << ')';
              break;
            }
            if (callee.binding == BindingKind::builtin &&
                callee.intrinsic == IntrinsicId::present && expression.children.size() == 2) {
              output_ << '(' << mangler_->name(expression.children[1].value) << ".has_value())";
              break;
            }
            if (callee.binding == BindingKind::builtin &&
                callee.intrinsic == IntrinsicId::reshape &&
                (expression.children.size() == 3 || expression.children.size() == 4)) {
              if (expression.shape.size() == 1) {
                emit_expression(expression.children[1]);
              } else {
                output_ << "mpf_runtime::reshape_column_major(";
                emit_expression(expression.children[1]);
                output_ << ", " << expression.shape[0] << ", " << expression.shape[1] << ')';
              }
              break;
            }
            output_ << (callee.binding == BindingKind::builtin ? mapped_identifier(callee)
                                                               : mangler_->name(callee.value));
          } else
            emit_expression(callee, own);
        }
        output_ << '(';
        for (std::size_t index = 1; index < expression.children.size(); ++index) {
          if (index != 1) output_ << ", ";
          const auto intent_index = index - 1;
          if (intent_index < expression.argument_optional_forward.size() &&
              expression.argument_optional_forward[intent_index] &&
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
          const auto selector_count = expression.children.size() - 1;
          if (selector_count == 1) {
            output_ << "mpf_runtime::slice(";
            if (expression.column_major && expression.children[0].shape.size() > 1) {
              output_ << "mpf_runtime::flatten_column_major(";
              emit_expression(expression.children[0]);
              output_ << ')';
            } else {
              emit_expression(expression.children[0]);
            }
            output_ << ", ";
            emit_slice_parameters(expression.children[1]);
            output_ << ')';
          } else if (selector_count == 2) {
            const auto& row = expression.children[1];
            const auto& column = expression.children[2];
            if (row.kind != ExpressionKind::slice) {
              output_ << "mpf_runtime::slice(";
              emit_runtime_index(expression.children[0], row, expression);
              output_ << ", ";
              emit_slice_parameters(column);
              output_ << ')';
            } else if (column.kind != ExpressionKind::slice) {
              output_ << "mpf_runtime::column(";
              emit_row_slice(expression, row);
              output_ << ", static_cast<std::int64_t>(";
              emit_expression(column);
              output_ << "), " << expression.index_base << ", "
                      << (expression.allow_negative_index ? "true" : "false") << ')';
            } else {
              output_ << "mpf_runtime::columns(";
              emit_row_slice(expression, row);
              output_ << ", ";
              emit_slice_parameters(column);
              output_ << ')';
            }
          } else {
            output_ << "0";
          }
        } else if (expression.column_major && expression.children.size() == 2 &&
                   expression.children[0].shape.size() > 1) {
          output_ << "mpf_runtime::matrix_linear_index(";
          emit_expression(expression.children[0]);
          output_ << ", static_cast<std::int64_t>(";
          emit_expression(expression.children[1]);
          output_ << "), " << expression.index_base << ')';
        } else {
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
        }
        break;
      case ExpressionKind::slice: output_ << '0'; break;
      case ExpressionKind::member:
        if (!expression.children.empty()) emit_expression(expression.children.front(), own);
        output_ << '.' << expression.value;
        break;
      case ExpressionKind::list:
        output_ << cpp_container_type(expression.element_type, expression.shape.size()) << '{';
        for (std::size_t index = 0; index < expression.children.size(); ++index) {
          if (index != 0) output_ << ", ";
          if (expression.element_type == ValueType::real &&
              expression.children[index].inferred_type != ValueType::list) {
            output_ << "static_cast<double>(";
            emit_expression(expression.children[index]);
            output_ << ')';
          } else {
            emit_expression(expression.children[index]);
          }
        }
        output_ << '}';
        break;
      case ExpressionKind::tuple:
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

  void emit_scope_declarations(const std::vector<Statement>& statements,
                               const std::vector<std::string>& parameters,
                               const std::string& prefix = {}) {
    const std::set<std::string> excluded(parameters.begin(), parameters.end());
    std::set<std::string> found;
    std::vector<Declaration> declarations;
    collect_declarations(statements, excluded, found, declarations);
    for (const auto& declaration : declarations) {
      indentation();
      output_ << prefix;
      if (declaration.explicit_type == ValueType::list) {
        const bool starred = declaration.pattern_leaf != nullptr &&
                             declaration.pattern_leaf->kind == AssignmentPatternKind::starred_name;
        const bool concrete_container = !declaration.shape.empty() &&
                                        (declaration.element_type != ValueType::unknown || starred);
        if (declaration.initializer != nullptr && !concrete_container) {
          output_ << "std::decay_t<decltype(";
          emit_declaration_type_expression(declaration);
          output_ << ")>";
        } else {
          output_ << cpp_container_type(declaration.element_type, declaration.shape.size());
        }
      } else if (primitive_type(declaration.explicit_type)) {
        output_ << cpp_type(declaration.explicit_type);
      } else if (declaration.initializer != nullptr) {
        output_ << "std::decay_t<decltype(";
        emit_declaration_type_expression(declaration);
        output_ << ")>";
      } else {
        output_ << "double";
      }
      output_ << ' ' << mangler_->name(declaration.name);
      if (declaration.explicit_type == ValueType::list && declaration.initializer == nullptr &&
          !declaration.shape.empty()) {
        output_ << '(' << declaration.shape[0];
        for (std::size_t dimension = 1; dimension < declaration.shape.size(); ++dimension) {
          output_ << ", "
                  << cpp_container_type(declaration.element_type,
                                        declaration.shape.size() - dimension)
                  << '(' << declaration.shape[dimension];
        }
        for (std::size_t dimension = 1; dimension < declaration.shape.size(); ++dimension) {
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

  void emit_declaration_type_expression(const Declaration& declaration) {
    if (declaration.pattern_leaf != nullptr &&
        declaration.pattern_leaf->kind == AssignmentPatternKind::name) {
      emit_pattern_access(declaration.pattern_leaf->access_path,
                          [&] { emit_expression(*declaration.initializer); });
      return;
    }
    if (declaration.tuple_index != dynamic_extent) {
      if (declaration.initializer->inferred_type == ValueType::list) {
        output_ << '(';
        emit_expression(*declaration.initializer);
        output_ << ").at(" << declaration.tuple_index << ')';
      } else {
        output_ << "std::get<" << declaration.tuple_index << ">(";
        emit_expression(*declaration.initializer);
        output_ << ')';
      }
      return;
    }
    emit_expression(*declaration.initializer);
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

  static std::string cpp_parameter_type(const Statement& statement, const std::size_t index) {
    const auto type = index < statement.parameter_types.size() ? statement.parameter_types[index]
                                                               : ValueType::unknown;
    if (type != ValueType::list) return cpp_type(type);
    const auto element = index < statement.parameter_element_types.size()
                             ? statement.parameter_element_types[index]
                             : ValueType::unknown;
    const auto rank =
        index < statement.parameter_shapes.size() ? statement.parameter_shapes[index].size() : 0U;
    return cpp_container_type(element, rank);
  }

  static bool primitive_type(const ValueType type) noexcept {
    return type == ValueType::integer || type == ValueType::real || type == ValueType::boolean ||
           type == ValueType::string || type == ValueType::null_value;
  }

  bool explicit_recursive_return(const Statement& statement, std::string& type) const {
    if (statement.return_names.empty() && !statement.has_value_return) {
      type = "void";
      return true;
    }
    const bool tuple_return =
        statement.return_names.size() > 1 ||
        (statement.declared_type == ValueType::tuple && !statement.return_types.empty());
    if (tuple_return) {
      if ((!statement.return_names.empty() &&
           statement.return_types.size() != statement.return_names.size()) ||
          !std::all_of(statement.return_types.begin(), statement.return_types.end(),
                       [](const ValueType value) { return primitive_type(value); })) {
        return false;
      }
      type = "std::tuple<";
      for (std::size_t index = 0; index < statement.return_types.size(); ++index) {
        if (index != 0) type += ", ";
        type += cpp_type(statement.return_types[index]);
      }
      type += '>';
      return true;
    }
    if (!primitive_type(statement.declared_type)) return false;
    type = cpp_type(statement.declared_type);
    return true;
  }

  bool has_explicit_recursive_return(const Statement& statement) const {
    std::string type;
    return explicit_recursive_return(statement, type);
  }

  void emit_function_signature(const Statement& statement, const bool recursive) {
    std::size_t templated_parameters = 0;
    for (std::size_t index = 0; index < statement.parameters.size(); ++index) {
      if (index >= statement.parameter_optional.size() || !statement.parameter_optional[index]) {
        ++templated_parameters;
      }
    }
    if (templated_parameters != 0) {
      indentation();
      output_ << "template <";
      bool first = true;
      for (std::size_t index = 0; index < statement.parameters.size(); ++index) {
        if (index < statement.parameter_optional.size() && statement.parameter_optional[index]) {
          continue;
        }
        if (!first) output_ << ", ";
        output_ << "typename T" << index;
        first = false;
      }
      output_ << ">\n";
    }
    indentation();
    std::string return_type;
    if (!recursive || !explicit_recursive_return(statement, return_type)) {
      return_type = "auto";
    }
    output_ << return_type << ' ' << mangler_->name(statement.name) << '(';
    for (std::size_t index = 0; index < statement.parameters.size(); ++index) {
      if (index != 0) output_ << ", ";
      const auto intent = index < statement.parameter_intents.size()
                              ? statement.parameter_intents[index]
                              : ParameterIntent::none;
      const bool optional =
          index < statement.parameter_optional.size() && statement.parameter_optional[index];
      if (optional) {
        output_ << "mpf_runtime::optional_argument<" << cpp_parameter_type(statement, index) << "> "
                << mangler_->name(statement.parameters[index]);
        continue;
      }
      if (intent == ParameterIntent::in) output_ << "const ";
      output_ << 'T' << index;
      if (intent == ParameterIntent::in || intent == ParameterIntent::out ||
          intent == ParameterIntent::inout) {
        output_ << '&';
      }
      output_ << ' ' << mangler_->name(statement.parameters[index]);
    }
    output_ << ')';
  }

  void emit_function_declaration(const Statement& statement) {
    emit_function_signature(statement, true);
    output_ << ";\n";
  }

  void emit_function(const Statement& statement, const bool recursive) {
    const auto function_indent = indent_;
    emit_function_signature(statement, recursive);
    output_ << " {\n";
    const auto saved_optional_parameters = active_optional_parameters_;
    active_optional_parameters_.clear();
    for (std::size_t index = 0; index < statement.parameters.size(); ++index) {
      if (index < statement.parameter_optional.size() && statement.parameter_optional[index]) {
        active_optional_parameters_.insert(statement.parameters[index]);
      }
    }
    indent_ = function_indent + 1;
    emit_scope_declarations(statement.body, statement.parameters);
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
    const auto selector = mangler_->temporary("select");
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
      emit_case_condition(clause, selector,
                          statement.expression.inferred_type == ValueType::string);
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
        const auto temporary = mangler_->temporary("outputs");
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
            statement.alternative.empty() ? std::string{} : mangler_->temporary("loop_completed");
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
        const auto start = mangler_->temporary("start");
        const auto stop = mangler_->temporary("stop");
        const auto step = mangler_->temporary("step");
        auto variable = mangler_->name(statement.name);
        if (active_optional_parameters_.count(statement.name) != 0U) {
          variable += ".value()";
        }
        const auto cursor =
            statement.retain_last_loop_value ? mangler_->temporary("cursor") : variable;
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
            statement.alternative.empty() ? std::string{} : mangler_->temporary("loop_completed");
        if (!completion.empty()) {
          indentation();
          output_ << "bool " << completion << " = true;\n";
        }
        indentation();
        output_ << "for (";
        if (statement.retain_last_loop_value) output_ << "auto ";
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

  const TranspileOptions& options_;
  std::ostringstream output_;
  std::size_t indent_{0};
  cpp::lir::EmissionPlan emission_{};
  std::unique_ptr<IdentifierMangler> mangler_;
  std::vector<std::string> loop_completion_flags_;
  std::set<std::string> active_optional_parameters_;
  std::vector<RenderMarker> markers_;
};

}  // namespace

RenderedOutput render_cpp(const cpp::lir::SemanticProgram& program,
                          const TranspileOptions& options) {
  return Renderer(options).render(program);
}

}  // namespace mpf::detail
