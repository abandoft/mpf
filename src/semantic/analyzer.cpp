#include <algorithm>
#include <cstdlib>
#include <limits>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "analyzer_internal.hpp"
#include "compiler/function_graph_generic.hpp"

namespace mpf::detail::semantic_internal {

hir::ExpressionFacts& semantic(hir::SemanticTable& table, const Expression& expression) {
  return *table.expression(expression.id);
}

const hir::ExpressionFacts& semantic(const hir::SemanticTable& table,
                                     const Expression& expression) {
  return *table.expression(expression.id);
}

hir::StatementFacts& semantic(hir::SemanticTable& table, const Statement& statement) {
  return *table.statement(statement.id);
}

Symbol::Symbol(const ValueType symbol_type, const BindingKind symbol_binding,
               const bool symbol_assigned, const ValueType symbol_element_type,
               std::vector<std::size_t> symbol_shape)
    : type(symbol_type),
      numeric_type(default_numeric_type(symbol_type)),
      binding(symbol_binding),
      assigned(symbol_assigned),
      element_type(symbol_element_type),
      element_numeric_type(default_numeric_type(symbol_element_type)),
      array_storage(symbol_type == ValueType::list ? ArrayStorageFormat::dense
                                                   : ArrayStorageFormat::none),
      shape(std::move(symbol_shape)) {}

NumericType default_numeric_type(const ValueType type) noexcept {
  switch (type) {
    case ValueType::unknown: return unknown_numeric_type;
    case ValueType::integer: return integer_numeric_type;
    case ValueType::real: return real_numeric_type;
    case ValueType::boolean: return logical_numeric_type;
    case ValueType::string:
    case ValueType::null_value:
    case ValueType::list:
    case ValueType::tuple:
    case ValueType::function: return no_numeric_type;
  }
  return unknown_numeric_type;
}

namespace {

void finalize_array_storage(const ValueType type, ArrayStorageFormat& storage) noexcept {
  if (type == ValueType::list) {
    if (storage == ArrayStorageFormat::none) storage = ArrayStorageFormat::dense;
    return;
  }
  if (type != ValueType::unknown) storage = ArrayStorageFormat::none;
}

void finalize_array_storage_vector(const std::vector<ValueType>& types,
                                   std::vector<ArrayStorageFormat>& storage) noexcept {
  const auto size = std::min(types.size(), storage.size());
  for (std::size_t index = 0; index < size; ++index) {
    finalize_array_storage(types[index], storage[index]);
  }
}

void finalize_numeric_slot(const ValueType type, NumericType& numeric_type) noexcept {
  if (type == ValueType::unknown) {
    if (!numeric_type.valid()) numeric_type = unknown_numeric_type;
    return;
  }
  const auto fallback = default_numeric_type(type);
  if (fallback == no_numeric_type) {
    numeric_type = no_numeric_type;
  } else if (!numeric_type.valid() || numeric_type == no_numeric_type) {
    numeric_type = fallback;
  }
}

void finalize_value_numeric(const ValueType type, NumericType& numeric_type,
                            const ValueType element_type,
                            NumericType& element_numeric_type) noexcept {
  finalize_numeric_slot(type, numeric_type);
  if (type == ValueType::list) {
    finalize_numeric_slot(element_type, element_numeric_type);
  } else if (type == ValueType::unknown) {
    if (element_numeric_type != no_numeric_type) {
      finalize_numeric_slot(element_type, element_numeric_type);
    }
  } else {
    element_numeric_type = no_numeric_type;
  }
}

void finalize_value_metadata(ValueMetadata& metadata) {
  finalize_value_numeric(metadata.type, metadata.numeric_type, metadata.element_type,
                         metadata.element_numeric_type);
  finalize_array_storage(metadata.type, metadata.array_storage);
  for (auto& element : metadata.elements) finalize_value_metadata(element);
}

void finalize_assignment_pattern(AssignmentPattern& pattern) {
  finalize_value_numeric(pattern.type, pattern.numeric_type, pattern.element_type,
                         pattern.element_numeric_type);
  finalize_array_storage(pattern.type, pattern.array_storage);
  finalize_value_numeric(pattern.previous_type, pattern.previous_numeric_type,
                         pattern.previous_element_type, pattern.previous_element_numeric_type);
  finalize_array_storage(pattern.previous_type, pattern.previous_array_storage);
  for (auto& child : pattern.children) finalize_assignment_pattern(child);
}

void finalize_numeric_vector(const std::vector<ValueType>& types,
                             std::vector<NumericType>& numeric_types) noexcept {
  const auto size = std::min(types.size(), numeric_types.size());
  for (std::size_t index = 0; index < size; ++index) {
    finalize_numeric_slot(types[index], numeric_types[index]);
  }
}

void finalize_element_numeric_vector(const std::vector<ValueType>& container_types,
                                     const std::vector<ValueType>& element_types,
                                     std::vector<NumericType>& element_numeric_types) noexcept {
  const auto size =
      std::min({container_types.size(), element_types.size(), element_numeric_types.size()});
  for (std::size_t index = 0; index < size; ++index) {
    NumericType ignored = no_numeric_type;
    finalize_value_numeric(container_types[index], ignored, element_types[index],
                           element_numeric_types[index]);
  }
}

void finalize_numeric_facts(hir::SemanticTable& semantics) {
  for (auto& facts : semantics.expressions) {
    finalize_value_numeric(facts.inferred_type, facts.numeric_type, facts.element_type,
                           facts.element_numeric_type);
    finalize_array_storage(facts.inferred_type, facts.array_storage);
    finalize_numeric_vector(facts.tuple_types, facts.tuple_numeric_types);
    finalize_array_storage_vector(facts.tuple_types, facts.tuple_array_storage);
    finalize_element_numeric_vector(facts.tuple_types, facts.tuple_element_types,
                                    facts.tuple_element_numeric_types);
    for (auto& element : facts.sequence_elements) finalize_value_metadata(element);
  }
  for (auto& facts : semantics.statements) {
    finalize_value_numeric(facts.declared_type, facts.declared_numeric_type, facts.element_type,
                           facts.element_numeric_type);
    finalize_array_storage(facts.declared_type, facts.array_storage);
    finalize_value_numeric(facts.previous_type, facts.previous_numeric_type,
                           facts.previous_element_type, facts.previous_element_numeric_type);
    finalize_array_storage(facts.previous_type, facts.previous_array_storage);
    finalize_numeric_vector(facts.parameter_types, facts.parameter_numeric_types);
    finalize_array_storage_vector(facts.parameter_types, facts.parameter_array_storage);
    finalize_element_numeric_vector(facts.parameter_types, facts.parameter_element_types,
                                    facts.parameter_element_numeric_types);
    finalize_numeric_vector(facts.return_types, facts.return_numeric_types);
    finalize_array_storage_vector(facts.return_types, facts.return_array_storage);
    finalize_element_numeric_vector(facts.return_types, facts.return_element_types,
                                    facts.return_element_numeric_types);
    finalize_numeric_vector(facts.target_types, facts.target_numeric_types);
    finalize_array_storage_vector(facts.target_types, facts.target_array_storage);
    finalize_element_numeric_vector(facts.target_types, facts.target_element_types,
                                    facts.target_element_numeric_types);
    finalize_numeric_vector(facts.target_previous_types, facts.target_previous_numeric_types);
    finalize_array_storage_vector(facts.target_previous_types, facts.target_previous_array_storage);
    finalize_element_numeric_vector(facts.target_previous_types,
                                    facts.target_previous_element_types,
                                    facts.target_previous_element_numeric_types);
    finalize_assignment_pattern(facts.target_pattern);
    for (auto& element : facts.return_sequence_elements) finalize_value_metadata(element);
  }
}

}  // namespace

NumericType expression_numeric_type(const hir::ExpressionFacts& facts) noexcept {
  return facts.inferred_type == ValueType::list ? facts.element_numeric_type : facts.numeric_type;
}

NumericType join_expression_numeric_types(const hir::ExpressionFacts& left,
                                          const hir::ExpressionFacts& right) noexcept {
  return join_numeric_types(expression_numeric_type(left), expression_numeric_type(right));
}

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
      left.numeric_type != right.numeric_type ||
      left.element_numeric_type != right.element_numeric_type ||
      left.array_storage != right.array_storage || left.shape != right.shape ||
      left.sequence != right.sequence || left.list_sequence != right.list_sequence ||
      left.elements.size() != right.elements.size()) {
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

ValueMetadata expression_metadata(const Expression& expression, const hir::SemanticTable& table) {
  const auto& facts = semantic(table, expression);
  ValueMetadata metadata;
  metadata.type = facts.inferred_type;
  metadata.element_type = facts.element_type;
  metadata.numeric_type = facts.numeric_type;
  metadata.element_numeric_type = facts.element_numeric_type;
  metadata.array_storage = facts.array_storage;
  metadata.shape = facts.shape;
  metadata.sequence =
      facts.inferred_type == ValueType::tuple || facts.inferred_type == ValueType::list;
  metadata.list_sequence = facts.sequence_is_list || facts.inferred_type == ValueType::list;
  metadata.elements = facts.sequence_elements;
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
  if (expression.kind == ExpressionKind::binary && expression.children.size() == 2U) {
    const auto left = numeric_constant(expression.children[0]);
    const auto right = numeric_constant(expression.children[1]);
    if (!left.has_value() || !right.has_value()) return std::nullopt;
    switch (expression.operation) {
      case BinaryOperator::add: return *left + *right;
      case BinaryOperator::subtract: return *left - *right;
      case BinaryOperator::multiply:
      case BinaryOperator::elementwise_multiply: return *left * *right;
      case BinaryOperator::divide:
      case BinaryOperator::elementwise_divide:
        return *right == 0.0 ? std::nullopt : std::optional<double>{*left / *right};
      case BinaryOperator::left_divide:
      case BinaryOperator::elementwise_left_divide:
        return *left == 0.0 ? std::nullopt : std::optional<double>{*right / *left};
      case BinaryOperator::none:
      case BinaryOperator::floor_divide:
      case BinaryOperator::remainder:
      case BinaryOperator::power:
      case BinaryOperator::logical_and:
      case BinaryOperator::logical_or:
      case BinaryOperator::elementwise_logical_and:
      case BinaryOperator::elementwise_logical_or:
      case BinaryOperator::elementwise_power: return std::nullopt;
    }
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
    case ExpressionKind::end_index:
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

const Statement* find_statement(const std::vector<Statement>& statements, const HirNodeId id) {
  for (const auto& statement : statements) {
    if (statement.id == id) return &statement;
    if (const auto* nested = find_statement(statement.body, id)) return nested;
    if (const auto* nested = find_statement(statement.alternative, id)) return nested;
  }
  return nullptr;
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

Analyzer::Analyzer(Program& program, hir::SemanticTable& semantics, const NameTable& names,
                   const FlowTable& flow)
    : program_(program), semantics_(semantics), names_(names), flow_(flow) {}

std::vector<Diagnostic> Analyzer::analyze() {
  push_scope(names_.global_scope);
  const auto function_graph =
      build_function_dependency_graph_generic<Expression, Statement>(program_.statements);
  for (const auto index : function_graph.definition_order) {
    analyze_function(program_.statements[index]);
  }
  for (auto& statement : program_.statements) {
    if (statement.kind == StatementKind::function) continue;
    analyze_statement(statement);
  }
  if (program_.language == SourceLanguage::fortran) {
    refresh_call_intents(program_.statements);
  }
  annotate_types(program_.statements);
  finalize_numeric_facts(semantics_);
  return std::move(diagnostics_);
}

bool Analyzer::structure_changed() const noexcept {
  return structure_changed_;
}

void Analyzer::register_expression(Expression& expression, hir::ExpressionFacts facts) {
  expression.id = HirNodeId{static_cast<HirNodeId::value_type>(semantics_.nodes.size())};
  facts.origin = expression.id;
  const auto offset = semantics_.expressions.size();
  semantics_.nodes.push_back(
      {hir::SemanticNodeKind::expression, static_cast<std::uint32_t>(offset)});
  semantics_.expressions.push_back(std::move(facts));
  semantics_.hir_node_count = semantics_.nodes.size() - 1U;
}

Expression Analyzer::clone_expression(const Expression& source) {
  auto facts = semantic(semantics_, source);
  Expression result = source;
  result.children.clear();
  result.children.reserve(source.children.size());
  for (const auto& child : source.children) {
    result.children.push_back(clone_expression(child));
  }
  register_expression(result, std::move(facts));
  return result;
}

void Analyzer::diagnose(const std::size_t line, std::string code, std::string message) {
  diagnostics_.push_back(
      {DiagnosticSeverity::error, std::move(code), std::move(message), {line, 1}});
}

ScopeState Analyzer::make_scope_state(const ScopeId id) const {
  ScopeState result;
  result.id = id;
  const auto* scope = names_.scope(id);
  if (scope == nullptr) return result;
  result.symbols.resize(scope->symbols.size());
  for (const auto symbol_id : scope->symbols) {
    const auto* name = names_.symbol(symbol_id);
    if (name == nullptr || name->scope_offset >= result.symbols.size()) continue;
    auto& symbol = result.symbols[name->scope_offset];
    symbol.binding =
        name->kind == NameSymbolKind::function ? BindingKind::function : BindingKind::variable;
    symbol.assigned = name->kind == NameSymbolKind::function;
    if (name->kind == NameSymbolKind::function) symbol.type = ValueType::function;
    if (name->kind == NameSymbolKind::function) symbol.numeric_type = no_numeric_type;
    if (name->kind == NameSymbolKind::loop_variable) {
      symbol.type = ValueType::integer;
      symbol.numeric_type = integer_numeric_type;
    }
    if (name->kind == NameSymbolKind::variable && name->declaration.valid()) {
      const auto* facts = semantics_.statement(name->declaration);
      if (facts != nullptr) {
        symbol.type = facts->declared_type;
        symbol.numeric_type = facts->declared_numeric_type;
        symbol.element_type = facts->element_type;
        symbol.element_numeric_type = facts->element_numeric_type;
        symbol.array_storage = facts->array_storage;
        symbol.shape = facts->shape;
      }
    }
  }
  return result;
}

void Analyzer::push_scope(const ScopeId id) {
  scopes_.push_back(make_scope_state(id));
}

ScopeState& Analyzer::current() {
  return scopes_.back();
}
const ScopeState& Analyzer::current() const {
  return scopes_.back();
}

Symbol* Analyzer::lookup(const SymbolId id) {
  const auto* name = names_.symbol(id);
  if (name == nullptr) return nullptr;
  for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
    if (scope->id != name->scope || name->scope_offset >= scope->symbols.size()) continue;
    return &scope->symbols[name->scope_offset];
  }
  return nullptr;
}

const Symbol* Analyzer::lookup(const SymbolId id) const {
  return const_cast<Analyzer*>(this)->lookup(id);
}

Symbol* Analyzer::lookup(const Expression& expression) {
  const auto* use = names_.reference(expression.id);
  return use == nullptr ? nullptr : lookup(use->symbol);
}

const NameUse* Analyzer::definition(const Statement& statement, const NameRole role,
                                    const std::size_t ordinal) const {
  return names_.use(statement.id, role, ordinal);
}

Symbol& Analyzer::definition_state(const Statement& statement, const NameRole role,
                                   const std::size_t ordinal) {
  return *lookup(definition(statement, role, ordinal)->symbol);
}

bool Analyzer::belongs_to_current_scope(const SymbolId id) const {
  const auto* name = names_.symbol(id);
  return name != nullptr && name->scope == current().id;
}

bool Analyzer::analyze_statements(std::vector<Statement>& statements) {
  bool terminated = false;
  for (auto& statement : statements) {
    (void)analyze_statement(statement);
    const auto* facts = flow_.statement(statement.id);
    terminated = terminated || (facts != nullptr && facts->terminates);
  }
  return terminated;
}

bool Analyzer::analyze_statements_in_scope(std::vector<Statement>& statements,
                                           const ScopeId scope) {
  if (!scope.valid()) return analyze_statements(statements);
  push_scope(scope);
  const auto terminated = analyze_statements(statements);
  scopes_.pop_back();
  return terminated;
}

ValueMetadata Analyzer::materialize_sequence(ValueMetadata metadata) const {
  if (!metadata.sequence || !metadata.elements.empty() || metadata.type != ValueType::list ||
      metadata.shape.empty() || metadata.shape.front() == dynamic_extent) {
    return metadata;
  }
  ValueMetadata child;
  if (metadata.shape.size() == 1) {
    child.type = metadata.element_type;
    child.numeric_type = metadata.element_numeric_type;
  } else {
    child.type = ValueType::list;
    child.array_storage = metadata.array_storage;
    child.numeric_type = no_numeric_type;
    child.element_type = metadata.element_type;
    child.element_numeric_type = metadata.element_numeric_type;
    child.shape.assign(metadata.shape.begin() + 1, metadata.shape.end());
    child.sequence = true;
    child.list_sequence = true;
    child = materialize_sequence(std::move(child));
  }
  metadata.elements.assign(metadata.shape.front(), child);
  return metadata;
}

void Analyzer::bind_assignment_leaf(Statement& statement, AssignmentPattern& leaf,
                                    const ValueMetadata& metadata,
                                    const std::size_t target_ordinal) {
  auto& symbol = definition_state(statement, NameRole::assignment, target_ordinal);
  leaf.type = metadata.type;
  leaf.element_type = metadata.element_type;
  leaf.numeric_type = metadata.numeric_type;
  leaf.element_numeric_type = metadata.element_numeric_type;
  leaf.array_storage = metadata.array_storage;
  leaf.shape = metadata.shape;
  leaf.previous_type = symbol.type;
  leaf.previous_element_type = symbol.element_type;
  leaf.previous_numeric_type = symbol.numeric_type;
  leaf.previous_element_numeric_type = symbol.element_numeric_type;
  leaf.previous_array_storage = symbol.array_storage;
  semantic(semantics_, statement).target_previous_types.push_back(symbol.type);
  semantic(semantics_, statement).target_previous_numeric_types.push_back(symbol.numeric_type);
  semantic(semantics_, statement).target_previous_element_types.push_back(symbol.element_type);
  semantic(semantics_, statement)
      .target_previous_element_numeric_types.push_back(symbol.element_numeric_type);
  semantic(semantics_, statement).target_previous_array_storage.push_back(symbol.array_storage);
  symbol.binding = BindingKind::variable;
  symbol.type = join_types(symbol.type, metadata.type);
  symbol.numeric_type = join_numeric_types(symbol.numeric_type, metadata.numeric_type);
  symbol.element_type = join_types(symbol.element_type, metadata.element_type);
  symbol.element_numeric_type =
      join_numeric_types(symbol.element_numeric_type, metadata.element_numeric_type);
  symbol.array_storage = join_array_storage_formats(symbol.array_storage, metadata.array_storage);
  if (symbol.shape.empty()) symbol.shape = metadata.shape;
  symbol.sequence_is_list = metadata.list_sequence;
  symbol.sequence_elements = metadata.elements;
  if (metadata.type == ValueType::tuple) {
    symbol.tuple_types.clear();
    symbol.tuple_numeric_types.clear();
    symbol.tuple_element_types.clear();
    symbol.tuple_element_numeric_types.clear();
    symbol.tuple_array_storage.clear();
    symbol.tuple_shapes.clear();
    for (const auto& element : metadata.elements) {
      symbol.tuple_types.push_back(element.type);
      symbol.tuple_numeric_types.push_back(element.numeric_type);
      symbol.tuple_element_types.push_back(element.element_type);
      symbol.tuple_element_numeric_types.push_back(element.element_numeric_type);
      symbol.tuple_array_storage.push_back(element.array_storage);
      symbol.tuple_shapes.push_back(element.shape);
    }
  }
  symbol.assigned = true;
  semantic(semantics_, statement).target_types.push_back(metadata.type);
  semantic(semantics_, statement).target_numeric_types.push_back(metadata.numeric_type);
  semantic(semantics_, statement).target_element_types.push_back(metadata.element_type);
  semantic(semantics_, statement)
      .target_element_numeric_types.push_back(metadata.element_numeric_type);
  semantic(semantics_, statement).target_array_storage.push_back(metadata.array_storage);
  semantic(semantics_, statement).target_shapes.push_back(metadata.shape);
}

ValueMetadata Analyzer::captured_metadata(const std::vector<ValueMetadata>& elements) const {
  ValueMetadata result;
  result.type = ValueType::list;
  result.array_storage = ArrayStorageFormat::dense;
  result.sequence = true;
  result.list_sequence = true;
  result.elements = elements;
  result.shape = {elements.size()};
  if (elements.empty()) return result;

  bool all_lists = true;
  auto element_type = ValueType::unknown;
  auto element_numeric_type = unknown_numeric_type;
  auto child_shape = elements.front().shape;
  for (const auto& element : elements) {
    all_lists = all_lists && element.type == ValueType::list;
    const auto scalar_type = element.type == ValueType::list ? element.element_type : element.type;
    element_type = join_types(element_type, scalar_type);
    element_numeric_type = join_numeric_types(
        element_numeric_type,
        element.type == ValueType::list ? element.element_numeric_type : element.numeric_type);
    if (element.shape != child_shape) child_shape.clear();
  }
  result.element_type = element_type;
  result.numeric_type = no_numeric_type;
  result.element_numeric_type = element_numeric_type;
  if (all_lists && !child_shape.empty()) {
    result.shape.insert(result.shape.end(), child_shape.begin(), child_shape.end());
  }
  return result;
}

bool Analyzer::associate_assignment_pattern(Statement& statement, AssignmentPattern& pattern,
                                            ValueMetadata metadata,
                                            std::vector<AssignmentAccess> path,
                                            std::size_t& target_ordinal) {
  if (pattern.kind == AssignmentPatternKind::name) {
    pattern.access_path = std::move(path);
    bind_assignment_leaf(statement, pattern, metadata, target_ordinal++);
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
      bind_assignment_leaf(statement, child, captured_metadata(captured), target_ordinal++);
      continue;
    }
    const auto source_index = index < star_index ? index : index + capture_count - 1U;
    auto child_path = path;
    child_path.push_back({source_index, metadata.list_sequence});
    valid = associate_assignment_pattern(statement, child, metadata.elements[source_index],
                                         std::move(child_path), target_ordinal) &&
            valid;
  }
  return valid;
}

bool Analyzer::analyze_statement(Statement& statement) {
  switch (statement.kind) {
    case StatementKind::declaration: {
      const auto declaration_symbol = definition(statement, NameRole::declaration)->symbol;
      auto& symbol = definition_state(statement, NameRole::declaration);
      const bool was_assigned = symbol.assigned;
      const bool is_parameter =
          !function_parameters_.empty() &&
          std::find(function_parameters_.back().begin(), function_parameters_.back().end(),
                    declaration_symbol) != function_parameters_.back().end();
      if (program_.language == SourceLanguage::fortran &&
          semantic(semantics_, statement).parameter_intent != ParameterIntent::none &&
          !is_parameter) {
        diagnose(statement.line, "MPF2036",
                 "Fortran INTENT attribute is only valid for a dummy argument");
      }
      if (program_.language == SourceLanguage::fortran &&
          semantic(semantics_, statement).optional_parameter && !is_parameter) {
        diagnose(statement.line, "MPF2040",
                 "Fortran OPTIONAL attribute is only valid for a dummy argument");
      }
      if (program_.language == SourceLanguage::fortran &&
          semantic(semantics_, statement).declared_type == ValueType::list &&
          !semantic(semantics_, statement).dummy_parameter &&
          std::find(semantic(semantics_, statement).shape.begin(),
                    semantic(semantics_, statement).shape.end(),
                    dynamic_extent) != semantic(semantics_, statement).shape.end()) {
        diagnose(statement.line, "MPF2039",
                 "Fortran assumed-shape arrays are only valid as dummy arguments");
      }
      if (program_.language == SourceLanguage::fortran && is_parameter &&
          statement.has_expression) {
        diagnose(statement.line, "MPF2036",
                 "Fortran dummy argument initialization is not valid in a procedure declaration");
      }
      symbol.binding = BindingKind::variable;
      symbol.type = semantic(semantics_, statement).declared_type;
      symbol.numeric_type = semantic(semantics_, statement).declared_numeric_type;
      if (!symbol.numeric_type.known()) symbol.numeric_type = default_numeric_type(symbol.type);
      semantic(semantics_, statement).declared_numeric_type = symbol.numeric_type;
      symbol.element_type = semantic(semantics_, statement).element_type;
      symbol.element_numeric_type = semantic(semantics_, statement).element_numeric_type;
      if (!symbol.element_numeric_type.known()) {
        symbol.element_numeric_type = default_numeric_type(symbol.element_type);
      }
      semantic(semantics_, statement).element_numeric_type = symbol.element_numeric_type;
      symbol.array_storage = semantic(semantics_, statement).array_storage;
      if (symbol.type == ValueType::list && symbol.array_storage == ArrayStorageFormat::none) {
        symbol.array_storage = ArrayStorageFormat::dense;
      }
      semantic(semantics_, statement).array_storage = symbol.array_storage;
      symbol.shape = semantic(semantics_, statement).shape;
      if (statement.has_expression) {
        const auto initializer_type = analyze_expression(statement.expression);
        const auto& initializer_facts = semantic(semantics_, statement.expression);
        symbol.type = join_types(symbol.type, initializer_type);
        symbol.numeric_type =
            join_numeric_types(symbol.numeric_type, initializer_facts.numeric_type);
        if (semantic(semantics_, statement.expression).inferred_type == ValueType::list) {
          symbol.array_storage = join_array_storage_formats(
              symbol.array_storage, semantic(semantics_, statement.expression).array_storage);
          if (symbol.element_type != ValueType::unknown &&
              semantic(semantics_, statement.expression).element_type != ValueType::unknown &&
              join_types(symbol.element_type,
                         semantic(semantics_, statement.expression).element_type) ==
                  ValueType::unknown) {
            diagnose(statement.line, "MPF2020",
                     "array initializer element type is incompatible with declaration");
          }
          symbol.element_type = join_types(symbol.element_type,
                                           semantic(semantics_, statement.expression).element_type);
          symbol.element_numeric_type =
              join_numeric_types(symbol.element_numeric_type,
                                 semantic(semantics_, statement.expression).element_numeric_type);
          semantic(semantics_, statement.expression).element_type = symbol.element_type;
          semantic(semantics_, statement.expression).element_numeric_type =
              symbol.element_numeric_type;
          if (!symbol.shape.empty() && !semantic(semantics_, statement.expression).shape.empty() &&
              known_shape(symbol.shape) &&
              known_shape(semantic(semantics_, statement.expression).shape) &&
              symbol.shape != semantic(semantics_, statement.expression).shape) {
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
      const auto symbol_id = definition(statement, NameRole::assignment)->symbol;
      diagnose_fortran_parameter_write(symbol_id, statement.line);
      const auto type = analyze_expression(statement.expression);
      auto& symbol = definition_state(statement, NameRole::assignment);
      symbol.binding = BindingKind::variable;
      semantic(semantics_, statement).previous_type = symbol.type;
      semantic(semantics_, statement).previous_numeric_type = symbol.numeric_type;
      semantic(semantics_, statement).previous_element_type = symbol.element_type;
      semantic(semantics_, statement).previous_element_numeric_type = symbol.element_numeric_type;
      semantic(semantics_, statement).previous_array_storage = symbol.array_storage;
      const auto joined = join_types(symbol.type, type);
      symbol.type = joined;
      symbol.numeric_type = join_numeric_types(
          symbol.numeric_type, semantic(semantics_, statement.expression).numeric_type);
      if (type == ValueType::list) {
        symbol.array_storage = join_array_storage_formats(
            symbol.array_storage, semantic(semantics_, statement.expression).array_storage);
        const auto joined_element = join_types(
            symbol.element_type, semantic(semantics_, statement.expression).element_type);
        symbol.element_type = joined_element;
        symbol.element_numeric_type =
            join_numeric_types(symbol.element_numeric_type,
                               semantic(semantics_, statement.expression).element_numeric_type);
        if (program_.language == SourceLanguage::fortran && !symbol.shape.empty() &&
            !semantic(semantics_, statement.expression).shape.empty() &&
            known_shape(symbol.shape) &&
            known_shape(semantic(semantics_, statement.expression).shape) &&
            symbol.shape != semantic(semantics_, statement.expression).shape) {
          diagnose(statement.line, "MPF2024",
                   "Fortran array assignment shape does not match its declared shape");
        }
        if (program_.language != SourceLanguage::fortran || symbol.shape.empty()) {
          symbol.shape = semantic(semantics_, statement.expression).shape;
        }
      } else if (type == ValueType::tuple) {
        if (semantic(semantics_, statement).previous_type == ValueType::unknown) {
          symbol.tuple_types = semantic(semantics_, statement.expression).tuple_types;
          symbol.tuple_numeric_types =
              semantic(semantics_, statement.expression).tuple_numeric_types;
          symbol.tuple_element_types =
              semantic(semantics_, statement.expression).tuple_element_types;
          symbol.tuple_element_numeric_types =
              semantic(semantics_, statement.expression).tuple_element_numeric_types;
          symbol.tuple_array_storage =
              semantic(semantics_, statement.expression).tuple_array_storage;
          symbol.tuple_shapes = semantic(semantics_, statement.expression).tuple_shapes;
        } else if (symbol.tuple_types != semantic(semantics_, statement.expression).tuple_types ||
                   symbol.tuple_numeric_types !=
                       semantic(semantics_, statement.expression).tuple_numeric_types ||
                   symbol.tuple_element_types !=
                       semantic(semantics_, statement.expression).tuple_element_types ||
                   symbol.tuple_element_numeric_types !=
                       semantic(semantics_, statement.expression).tuple_element_numeric_types ||
                   symbol.tuple_array_storage !=
                       semantic(semantics_, statement.expression).tuple_array_storage ||
                   symbol.tuple_shapes != semantic(semantics_, statement.expression).tuple_shapes) {
          symbol.tuple_types.clear();
          symbol.tuple_numeric_types.clear();
          symbol.tuple_element_types.clear();
          symbol.tuple_element_numeric_types.clear();
          symbol.tuple_array_storage.clear();
          symbol.tuple_shapes.clear();
        }
      }
      if (type == ValueType::list || type == ValueType::tuple) {
        if (semantic(semantics_, statement).previous_type == ValueType::unknown ||
            (symbol.sequence_is_list ==
                 semantic(semantics_, statement.expression).sequence_is_list &&
             same_metadata(symbol.sequence_elements,
                           semantic(semantics_, statement.expression).sequence_elements))) {
          symbol.sequence_is_list = semantic(semantics_, statement.expression).sequence_is_list;
          symbol.sequence_elements = semantic(semantics_, statement.expression).sequence_elements;
        } else {
          symbol.sequence_elements.clear();
        }
      }
      symbol.assigned = true;
      return false;
    }
    case StatementKind::multi_assignment: {
      semantic(semantics_, statement.expression).requested_outputs = statement.target_names.size();
      analyze_expression(statement.expression);
      semantic(semantics_, statement).target_types.clear();
      semantic(semantics_, statement).target_numeric_types.clear();
      semantic(semantics_, statement).target_element_types.clear();
      semantic(semantics_, statement).target_element_numeric_types.clear();
      semantic(semantics_, statement).target_array_storage.clear();
      semantic(semantics_, statement).target_shapes.clear();
      semantic(semantics_, statement).target_previous_types.clear();
      semantic(semantics_, statement).target_previous_numeric_types.clear();
      semantic(semantics_, statement).target_previous_element_types.clear();
      semantic(semantics_, statement).target_previous_element_numeric_types.clear();
      semantic(semantics_, statement).target_previous_array_storage.clear();
      semantic(semantics_, statement).target_types.reserve(statement.target_names.size());
      semantic(semantics_, statement).target_numeric_types.reserve(statement.target_names.size());
      semantic(semantics_, statement).target_element_types.reserve(statement.target_names.size());
      semantic(semantics_, statement)
          .target_element_numeric_types.reserve(statement.target_names.size());
      semantic(semantics_, statement).target_array_storage.reserve(statement.target_names.size());
      semantic(semantics_, statement).target_shapes.reserve(statement.target_names.size());
      semantic(semantics_, statement).target_previous_types.reserve(statement.target_names.size());
      semantic(semantics_, statement)
          .target_previous_numeric_types.reserve(statement.target_names.size());
      semantic(semantics_, statement)
          .target_previous_element_types.reserve(statement.target_names.size());
      semantic(semantics_, statement)
          .target_previous_element_numeric_types.reserve(statement.target_names.size());
      semantic(semantics_, statement)
          .target_previous_array_storage.reserve(statement.target_names.size());
      if (program_.language == SourceLanguage::matlab) {
        if (statement.expression.kind != ExpressionKind::call ||
            !semantic(semantics_, statement.expression).multi_output_call) {
          diagnose(statement.line, "MPF2034",
                   "Matlab multi-output assignment requires a function with multiple outputs");
          return false;
        }
      } else if (program_.language == SourceLanguage::python) {
        if (!statement.has_target_pattern ||
            !semantic(semantics_, statement).target_pattern.valid()) {
          diagnose(statement.line, "MPF2042",
                   "Python unpacking requires a structured assignment pattern");
          return false;
        }
        auto metadata = expression_metadata(statement.expression, semantics_);
        if (metadata.elements.empty() &&
            semantic(semantics_, statement.expression).inferred_type == ValueType::tuple &&
            !semantic(semantics_, statement.expression).tuple_types.empty()) {
          metadata.sequence = true;
          metadata.list_sequence = false;
          for (std::size_t index = 0;
               index < semantic(semantics_, statement.expression).tuple_types.size(); ++index) {
            ValueMetadata element;
            element.type = semantic(semantics_, statement.expression).tuple_types[index];
            element.numeric_type =
                index < semantic(semantics_, statement.expression).tuple_numeric_types.size()
                    ? semantic(semantics_, statement.expression).tuple_numeric_types[index]
                    : default_numeric_type(element.type);
            element.element_type =
                index < semantic(semantics_, statement.expression).tuple_element_types.size()
                    ? semantic(semantics_, statement.expression).tuple_element_types[index]
                    : ValueType::unknown;
            element.element_numeric_type =
                index < semantic(semantics_, statement.expression)
                            .tuple_element_numeric_types.size()
                    ? semantic(semantics_, statement.expression).tuple_element_numeric_types[index]
                    : default_numeric_type(element.element_type);
            element.array_storage =
                index < semantic(semantics_, statement.expression).tuple_array_storage.size()
                    ? semantic(semantics_, statement.expression).tuple_array_storage[index]
                    : (element.type == ValueType::list ? ArrayStorageFormat::unknown
                                                       : ArrayStorageFormat::none);
            element.shape = index < semantic(semantics_, statement.expression).tuple_shapes.size()
                                ? semantic(semantics_, statement.expression).tuple_shapes[index]
                                : std::vector<std::size_t>{};
            metadata.elements.push_back(std::move(element));
          }
        }
        std::size_t target_ordinal = 0;
        if (!associate_assignment_pattern(statement, semantic(semantics_, statement).target_pattern,
                                          std::move(metadata), {}, target_ordinal)) {
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
        auto& symbol = definition_state(statement, NameRole::assignment, index);
        const auto type = index < semantic(semantics_, statement.expression).tuple_types.size()
                              ? semantic(semantics_, statement.expression).tuple_types[index]
                              : ValueType::unknown;
        const auto element_type =
            index < semantic(semantics_, statement.expression).tuple_element_types.size()
                ? semantic(semantics_, statement.expression).tuple_element_types[index]
                : ValueType::unknown;
        const auto numeric_type =
            index < semantic(semantics_, statement.expression).tuple_numeric_types.size()
                ? semantic(semantics_, statement.expression).tuple_numeric_types[index]
                : default_numeric_type(type);
        const auto element_numeric_type =
            index < semantic(semantics_, statement.expression).tuple_element_numeric_types.size()
                ? semantic(semantics_, statement.expression).tuple_element_numeric_types[index]
                : default_numeric_type(element_type);
        const auto shape = index < semantic(semantics_, statement.expression).tuple_shapes.size()
                               ? semantic(semantics_, statement.expression).tuple_shapes[index]
                               : std::vector<std::size_t>{};
        const auto array_storage =
            index < semantic(semantics_, statement.expression).tuple_array_storage.size()
                ? semantic(semantics_, statement.expression).tuple_array_storage[index]
                : (type == ValueType::list ? ArrayStorageFormat::unknown
                                           : ArrayStorageFormat::none);
        semantic(semantics_, statement).target_previous_types.push_back(symbol.type);
        semantic(semantics_, statement)
            .target_previous_numeric_types.push_back(symbol.numeric_type);
        semantic(semantics_, statement)
            .target_previous_element_types.push_back(symbol.element_type);
        semantic(semantics_, statement)
            .target_previous_element_numeric_types.push_back(symbol.element_numeric_type);
        semantic(semantics_, statement)
            .target_previous_array_storage.push_back(symbol.array_storage);
        symbol.binding = BindingKind::variable;
        symbol.type = join_types(symbol.type, type);
        symbol.numeric_type = join_numeric_types(symbol.numeric_type, numeric_type);
        symbol.element_type = join_types(symbol.element_type, element_type);
        symbol.element_numeric_type =
            join_numeric_types(symbol.element_numeric_type, element_numeric_type);
        symbol.array_storage = join_array_storage_formats(symbol.array_storage, array_storage);
        if (symbol.shape.empty()) symbol.shape = shape;
        symbol.assigned = true;
        semantic(semantics_, statement).target_types.push_back(type);
        semantic(semantics_, statement).target_numeric_types.push_back(numeric_type);
        semantic(semantics_, statement).target_element_types.push_back(element_type);
        semantic(semantics_, statement)
            .target_element_numeric_types.push_back(element_numeric_type);
        semantic(semantics_, statement).target_array_storage.push_back(array_storage);
        semantic(semantics_, statement).target_shapes.push_back(shape);
      }
      return false;
    }
    case StatementKind::indexed_assignment: {
      auto* root = root_container(statement.target_expression);
      auto* root_use = root == nullptr ? nullptr : names_.reference(root->id);
      diagnose_fortran_parameter_write(root_use == nullptr ? SymbolId{} : root_use->symbol,
                                       statement.line);
      const auto value_type = analyze_expression(statement.expression);
      bool container_already_analyzed = false;
      if (program_.language == SourceLanguage::matlab &&
          statement.target_expression.kind == ExpressionKind::call &&
          !statement.target_expression.children.empty()) {
        auto& container = statement.target_expression.children.front();
        analyze_expression(container);
        container_already_analyzed = true;
        const auto& container_facts = semantic(semantics_, container);
        if (container.kind == ExpressionKind::identifier &&
            container_facts.binding == BindingKind::variable &&
            (container_facts.inferred_type == ValueType::list ||
             container_facts.inferred_type == ValueType::unknown)) {
          statement.target_expression.kind = ExpressionKind::index;
          semantic(semantics_, statement.target_expression).index_base = 1U;
          semantic(semantics_, statement.target_expression).allow_negative_index = false;
          semantic(semantics_, statement.target_expression).column_major = true;
        }
      }
      // Matlab uses call syntax for indexed assignment. Re-resolve the storage root after the
      // semantic conversion to an index expression so type widening reaches the owning symbol.
      root = root_container(statement.target_expression);
      root_use = root == nullptr ? nullptr : names_.reference(root->id);
      const auto target_type =
          statement.target_expression.kind == ExpressionKind::index &&
                  program_.language == SourceLanguage::matlab
              ? analyze_index(statement.target_expression, container_already_analyzed, true)
              : analyze_expression(statement.target_expression);
      analyze_indexed_mutation(statement, value_type, target_type);
      const bool erase = semantic(semantics_, statement).indexed_mutation.kind ==
                         semantic::IndexedMutationKind::erase;
      if (statement.target_expression.kind != ExpressionKind::index) {
        diagnose(statement.line, "MPF2022",
                 "indexed assignment target is not an array/list element");
      } else if (erase) {
        // Matlab's [] right-hand side is a deletion operation, not a section replacement value.
      } else if (std::any_of(
                     semantic(semantics_, statement.target_expression).index_selectors.begin(),
                     semantic(semantics_, statement.target_expression).index_selectors.end(),
                     semantic::selector_preserves_dimension)) {
        analyze_section_assignment(statement, value_type);
      } else if (contains_slice(statement.target_expression)) {
        diagnose(statement.line, "MPF2029", "cannot assign through a temporary array/list section");
      } else if (target_type != ValueType::unknown && value_type != ValueType::unknown &&
                 join_types(target_type, value_type) == ValueType::unknown) {
        diagnose(statement.line, "MPF2020", "indexed assignment changes the array element type");
      }
      semantic(semantics_, statement).element_type =
          erase ? semantic(semantics_, statement.target_expression).element_type
          : std::any_of(semantic(semantics_, statement.target_expression).index_selectors.begin(),
                        semantic(semantics_, statement.target_expression).index_selectors.end(),
                        semantic::selector_preserves_dimension)
              ? semantic(semantics_, statement.target_expression).element_type
              : target_type;
      const auto target_numeric =
          expression_numeric_type(semantic(semantics_, statement.target_expression));
      const auto value_numeric =
          expression_numeric_type(semantic(semantics_, statement.expression));
      semantic(semantics_, statement).element_numeric_type =
          erase ? target_numeric : join_numeric_types(target_numeric, value_numeric);
      // Indexed assignment does not declare a value of its own. The mutated container's
      // representation lives on target_expression and its root storage; keeping it on the
      // statement would attach an array representation to declared_type=unknown.
      semantic(semantics_, statement).array_storage = ArrayStorageFormat::none;
      if (root_use != nullptr && root_use->symbol.valid()) {
        if (auto* symbol = lookup(root_use->symbol); symbol != nullptr) {
          symbol->element_numeric_type = join_numeric_types(
              symbol->element_numeric_type, semantic(semantics_, statement).element_numeric_type);
        }
      }
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
        diagnose(statement.line, "MPF2011", "continue/cycle statement is only valid inside a loop");
      }
      return true;
    case StatementKind::if_statement:
      if (const auto type = analyze_expression(statement.expression, true);
          program_.language == SourceLanguage::typescript && type != ValueType::boolean &&
          type != ValueType::unknown) {
        diagnose(statement.line, "MPF2002",
                 "TypeScript if condition currently requires a boolean value");
      }
      return analyze_branches(statement);
    case StatementKind::select_case: return analyze_select_case(statement);
    case StatementKind::case_clause:
      diagnose(statement.line, "MPF2043", "CASE clause appears outside SELECT CASE");
      return false;
    case StatementKind::while_loop: {
      const auto condition_type = analyze_expression(statement.expression, true);
      if (program_.language == SourceLanguage::typescript && condition_type != ValueType::boolean &&
          condition_type != ValueType::unknown) {
        diagnose(statement.line, "MPF2002",
                 "TypeScript while condition currently requires a boolean value");
      }
      const auto before = current();
      ++loop_depth_;
      analyze_statements_in_scope(statement.body, names_.body_scope(statement.id));
      --loop_depth_;
      current() = before;
      if (!statement.alternative.empty()) {
        analyze_statements_in_scope(statement.alternative, names_.alternative_scope(statement.id));
      }
      current() = before;
      return false;
    }
    case StatementKind::range_loop: {
      const auto loop_scope = names_.statement_scope(statement.id);
      const auto outer_index = scopes_.size() - 1U;
      const auto outer_before = scopes_[outer_index];
      const auto symbol_id = definition(statement, NameRole::loop_variable)->symbol;
      diagnose_fortran_parameter_write(symbol_id, statement.line);
      const auto start = analyze_expression(statement.expression);
      const auto stop = analyze_expression(statement.secondary_expression);
      auto step = ValueType::integer;
      if (statement.has_tertiary_expression)
        step = analyze_expression(statement.tertiary_expression);
      if (loop_scope.valid()) push_scope(loop_scope);
      auto& variable = definition_state(statement, NameRole::loop_variable);
      variable.type = join_types(join_types(start, stop), step);
      variable.numeric_type = join_numeric_types(
          join_numeric_types(semantic(semantics_, statement.expression).numeric_type,
                             semantic(semantics_, statement.secondary_expression).numeric_type),
          statement.has_tertiary_expression
              ? semantic(semantics_, statement.tertiary_expression).numeric_type
              : integer_numeric_type);
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
      variable.assigned = true;
      ++loop_depth_;
      analyze_statements_in_scope(statement.body, names_.body_scope(statement.id));
      --loop_depth_;
      const auto after_body = current();
      current() = before;
      if (loop_scope.valid()) {
        const auto outer_after_body = scopes_[outer_index];
        scopes_.pop_back();
        current() = outer_before;
        if (!statement.alternative.empty()) {
          analyze_statements_in_scope(statement.alternative,
                                      names_.alternative_scope(statement.id));
        }
        current() = definitely_nonempty ? outer_after_body : outer_before;
      } else {
        if (!statement.alternative.empty()) analyze_statements(statement.alternative);
        current() = definitely_nonempty ? after_body : before;
      }
      return false;
    }
    case StatementKind::for_loop: {
      const auto outer_before = current();
      const auto loop_scope = names_.statement_scope(statement.id);
      push_scope(loop_scope);
      const auto initializer_type = analyze_expression(statement.expression);
      auto& variable = definition_state(statement, NameRole::loop_variable);
      variable.type = join_types(semantic(semantics_, statement).declared_type, initializer_type);
      variable.numeric_type =
          join_numeric_types(semantic(semantics_, statement).declared_numeric_type,
                             semantic(semantics_, statement.expression).numeric_type);
      variable.binding = BindingKind::variable;
      variable.assigned = true;
      const auto condition_type = analyze_expression(statement.secondary_expression);
      if (condition_type != ValueType::boolean && condition_type != ValueType::unknown) {
        diagnose(statement.line, "MPF2002",
                 "TypeScript for condition currently requires a boolean value");
      }
      const auto update_type = analyze_expression(statement.tertiary_expression);
      variable.numeric_type = join_numeric_types(
          variable.numeric_type, semantic(semantics_, statement.tertiary_expression).numeric_type);
      if (variable.type != ValueType::unknown && update_type != ValueType::unknown &&
          join_types(variable.type, update_type) == ValueType::unknown) {
        diagnose(statement.line, "MPF2020",
                 "TypeScript for update changes the induction binding type");
      }
      semantic(semantics_, statement).declared_type = variable.type;
      semantic(semantics_, statement).declared_numeric_type = variable.numeric_type;
      semantic(semantics_, statement).element_type = variable.element_type;
      semantic(semantics_, statement).element_numeric_type = variable.element_numeric_type;
      semantic(semantics_, statement).shape = variable.shape;
      ++loop_depth_;
      analyze_statements_in_scope(statement.body, names_.body_scope(statement.id));
      --loop_depth_;
      scopes_.pop_back();
      current() = outer_before;
      return false;
    }
    case StatementKind::function: analyze_function(statement); return false;
  }
  return false;
}

bool Analyzer::analyze_branches(Statement& statement) {
  const auto before = current();
  const auto body_terminates =
      analyze_statements_in_scope(statement.body, names_.body_scope(statement.id));
  const auto after_body = current();
  current() = before;
  const auto alternative_terminates =
      !statement.alternative.empty() &&
      analyze_statements_in_scope(statement.alternative, names_.alternative_scope(statement.id));
  const auto after_alternative = current();
  current() = before;
  for (std::size_t index = 0; index < current().symbols.size(); ++index) {
    auto& symbol = current().symbols[index];
    const auto& body_symbol = after_body.symbols[index];
    const auto& alternative_symbol = after_alternative.symbols[index];
    symbol.assigned = body_symbol.assigned && alternative_symbol.assigned;
    symbol.type = join_types(body_symbol.type, alternative_symbol.type);
    symbol.numeric_type =
        join_numeric_types(body_symbol.numeric_type, alternative_symbol.numeric_type);
    symbol.element_type = join_types(body_symbol.element_type, alternative_symbol.element_type);
    symbol.element_numeric_type = join_numeric_types(body_symbol.element_numeric_type,
                                                     alternative_symbol.element_numeric_type);
    symbol.array_storage =
        join_array_storage_formats(body_symbol.array_storage, alternative_symbol.array_storage);
    symbol.shape = body_symbol.shape == alternative_symbol.shape ? body_symbol.shape
                                                                 : std::vector<std::size_t>{};
    if (body_symbol.tuple_types == alternative_symbol.tuple_types &&
        body_symbol.tuple_numeric_types == alternative_symbol.tuple_numeric_types &&
        body_symbol.tuple_element_types == alternative_symbol.tuple_element_types &&
        body_symbol.tuple_element_numeric_types == alternative_symbol.tuple_element_numeric_types &&
        body_symbol.tuple_array_storage == alternative_symbol.tuple_array_storage &&
        body_symbol.tuple_shapes == alternative_symbol.tuple_shapes) {
      symbol.tuple_types = body_symbol.tuple_types;
      symbol.tuple_numeric_types = body_symbol.tuple_numeric_types;
      symbol.tuple_element_types = body_symbol.tuple_element_types;
      symbol.tuple_element_numeric_types = body_symbol.tuple_element_numeric_types;
      symbol.tuple_array_storage = body_symbol.tuple_array_storage;
      symbol.tuple_shapes = body_symbol.tuple_shapes;
    } else {
      symbol.tuple_types.clear();
      symbol.tuple_numeric_types.clear();
      symbol.tuple_element_types.clear();
      symbol.tuple_element_numeric_types.clear();
      symbol.tuple_array_storage.clear();
      symbol.tuple_shapes.clear();
    }
    if (body_symbol.sequence_is_list == alternative_symbol.sequence_is_list &&
        same_metadata(body_symbol.sequence_elements, alternative_symbol.sequence_elements)) {
      symbol.sequence_is_list = body_symbol.sequence_is_list;
      symbol.sequence_elements = body_symbol.sequence_elements;
    } else {
      symbol.sequence_elements.clear();
    }
  }
  return !statement.alternative.empty() && body_terminates && alternative_terminates;
}

void Analyzer::merge_select_flows(const ScopeState& before, const std::vector<ScopeState>& flows) {
  current() = before;
  if (flows.empty()) return;
  for (std::size_t index = 0; index < current().symbols.size(); ++index) {
    auto& symbol = current().symbols[index];
    std::vector<const Symbol*> candidates;
    candidates.reserve(flows.size());
    for (const auto& flow : flows) {
      candidates.push_back(&flow.symbols[index]);
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
    const auto merge_numeric_type = [&](const auto member) {
      NumericType merged = unknown_numeric_type;
      for (const auto* candidate : candidates) {
        merged = join_numeric_types(merged, candidate->*member);
      }
      return merged;
    };
    symbol.numeric_type = merge_numeric_type(&Symbol::numeric_type);
    symbol.element_numeric_type = merge_numeric_type(&Symbol::element_numeric_type);
    symbol.array_storage = candidates.front()->array_storage;
    for (auto candidate = candidates.begin() + 1; candidate != candidates.end(); ++candidate) {
      symbol.array_storage =
          join_array_storage_formats(symbol.array_storage, (*candidate)->array_storage);
    }

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
                 candidate->tuple_numeric_types == first.tuple_numeric_types &&
                 candidate->tuple_element_types == first.tuple_element_types &&
                 candidate->tuple_element_numeric_types == first.tuple_element_numeric_types &&
                 candidate->tuple_array_storage == first.tuple_array_storage &&
                 candidate->tuple_shapes == first.tuple_shapes;
        });
    if (same_tuple) {
      symbol.tuple_types = first.tuple_types;
      symbol.tuple_numeric_types = first.tuple_numeric_types;
      symbol.tuple_element_types = first.tuple_element_types;
      symbol.tuple_element_numeric_types = first.tuple_element_numeric_types;
      symbol.tuple_array_storage = first.tuple_array_storage;
      symbol.tuple_shapes = first.tuple_shapes;
    } else {
      symbol.tuple_types.clear();
      symbol.tuple_numeric_types.clear();
      symbol.tuple_element_types.clear();
      symbol.tuple_element_numeric_types.clear();
      symbol.tuple_array_storage.clear();
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

bool Analyzer::analyze_select_case(Statement& statement) {
  const auto selector_type = analyze_expression(statement.expression);
  const bool matlab_switch = program_.language == SourceLanguage::matlab;
  if (matlab_switch) {
    if (selector_type != ValueType::unknown && selector_type != ValueType::integer &&
        selector_type != ValueType::real && selector_type != ValueType::string &&
        selector_type != ValueType::boolean) {
      diagnose(statement.line, "MPF2043",
               "Matlab switch expression must be a supported scalar value");
    }
  } else if (selector_type != ValueType::unknown && selector_type != ValueType::integer &&
             selector_type != ValueType::string && selector_type != ValueType::boolean) {
    diagnose(statement.line, "MPF2043",
             "Fortran SELECT CASE selector must be integer, character, or logical");
  }

  const auto before = current();
  std::vector<ScopeState> flows;
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
      if (matlab_switch) {
        if (value.range || !value.has_lower || value.has_upper) {
          diagnose(clause.line, "MPF2043",
                   "Matlab case clause must contain one supported scalar expression");
        }
        if (value.has_lower) {
          const auto type = analyze_expression(value.lower);
          const bool numeric_match =
              (selector_type == ValueType::integer || selector_type == ValueType::real) &&
              (type == ValueType::integer || type == ValueType::real);
          if (selector_type != ValueType::unknown && type != ValueType::unknown &&
              selector_type != type && !numeric_match) {
            diagnose(clause.line, "MPF2043",
                     "Matlab case expression type does not match the switch expression");
          }
          if (type == ValueType::list || type == ValueType::tuple) {
            diagnose(clause.line, "MPF2043",
                     "Matlab cell-array case expressions are not supported yet");
          }
        }
        continue;
      }
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
            value.range
                ? (value.has_upper ? numeric_constant(value.upper)
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
        if (lower.has_value() && upper.has_value() && fortran_string_compare(*lower, *upper) > 0) {
          diagnose(clause.line, "MPF2043",
                   "Fortran CASE range lower bound exceeds its upper bound");
        }
        for (const auto& existing : character_intervals) {
          const bool before_existing = upper.has_value() && existing.first.has_value() &&
                                       fortran_string_compare(*upper, *existing.first) < 0;
          const bool after_existing = existing.second.has_value() && lower.has_value() &&
                                      fortran_string_compare(*existing.second, *lower) < 0;
          if (!before_existing && !after_existing) {
            diagnose(clause.line, "MPF2043", "Fortran CASE selector overlaps an earlier selector");
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

void Analyzer::diagnose_fortran_parameter_write(const SymbolId symbol, const std::size_t line) {
  if (program_.language == SourceLanguage::fortran && !function_parameters_.empty()) {
    const auto parameter =
        std::find(function_parameters_.back().begin(), function_parameters_.back().end(), symbol);
    if (parameter == function_parameters_.back().end()) return;
    const auto position =
        static_cast<std::size_t>(std::distance(function_parameters_.back().begin(), parameter));
    if ((*function_parameter_intents_.back())[position] == ParameterIntent::in) {
      diagnose(line, "MPF2036", "Fortran INTENT(IN) dummy argument cannot be modified");
    }
    function_parameter_writes_.back()[position] = true;
  }
}

void Analyzer::mark_fortran_parameter_read(const SymbolId symbol) {
  if (program_.language != SourceLanguage::fortran || function_parameters_.empty()) return;
  const auto parameter =
      std::find(function_parameters_.back().begin(), function_parameters_.back().end(), symbol);
  if (parameter == function_parameters_.back().end()) return;
  const auto position =
      static_cast<std::size_t>(std::distance(function_parameters_.back().begin(), parameter));
  function_parameter_reads_.back()[position] = true;
}

void Analyzer::collect_value_returns(const std::vector<Statement>& statements,
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

void Analyzer::infer_python_tuple_returns(Statement& function) const {
  std::vector<const Expression*> returns;
  collect_value_returns(function.body, returns);
  if (returns.empty() ||
      std::any_of(returns.begin(), returns.end(), [&](const Expression* expression) {
        const auto& facts = semantic(semantics_, *expression);
        return facts.inferred_type != ValueType::tuple || facts.tuple_types.empty();
      })) {
    return;
  }
  const auto& first_return = semantic(semantics_, *returns.front());
  const auto arity = first_return.tuple_types.size();
  if (std::any_of(returns.begin(), returns.end(), [&](const Expression* expression) {
        return semantic(semantics_, *expression).tuple_types.size() != arity;
      })) {
    return;
  }

  semantic(semantics_, function).return_types = first_return.tuple_types;
  semantic(semantics_, function).return_numeric_types = first_return.tuple_numeric_types;
  semantic(semantics_, function).return_element_types = first_return.tuple_element_types;
  semantic(semantics_, function).return_element_numeric_types =
      first_return.tuple_element_numeric_types;
  semantic(semantics_, function).return_array_storage = first_return.tuple_array_storage;
  semantic(semantics_, function).return_shapes = first_return.tuple_shapes;
  semantic(semantics_, function).return_numeric_types.resize(arity, unknown_numeric_type);
  semantic(semantics_, function).return_element_types.resize(arity, ValueType::unknown);
  semantic(semantics_, function).return_element_numeric_types.resize(arity, unknown_numeric_type);
  semantic(semantics_, function).return_array_storage.resize(arity, ArrayStorageFormat::none);
  semantic(semantics_, function).return_shapes.resize(arity);
  for (std::size_t index = 0; index < arity; ++index) {
    bool type_conflict = false;
    bool element_conflict = false;
    auto type = semantic(semantics_, function).return_types[index];
    auto numeric_type = semantic(semantics_, function).return_numeric_types[index];
    auto element = semantic(semantics_, function).return_element_types[index];
    auto element_numeric_type = semantic(semantics_, function).return_element_numeric_types[index];
    auto array_storage = semantic(semantics_, function).return_array_storage[index];
    auto shape = semantic(semantics_, function).return_shapes[index];
    for (std::size_t path = 1; path < returns.size(); ++path) {
      const auto& next = semantic(semantics_, *returns[path]);
      const auto next_type = next.tuple_types[index];
      const auto joined_type = join_types(type, next_type);
      if (type != ValueType::unknown && next_type != ValueType::unknown &&
          joined_type == ValueType::unknown) {
        type_conflict = true;
      }
      type = joined_type;
      numeric_type = join_numeric_types(numeric_type, index < next.tuple_numeric_types.size()
                                                          ? next.tuple_numeric_types[index]
                                                          : unknown_numeric_type);
      const auto next_element = index < next.tuple_element_types.size()
                                    ? next.tuple_element_types[index]
                                    : ValueType::unknown;
      const auto joined_element = join_types(element, next_element);
      if (element != ValueType::unknown && next_element != ValueType::unknown &&
          joined_element == ValueType::unknown) {
        element_conflict = true;
      }
      element = joined_element;
      element_numeric_type =
          join_numeric_types(element_numeric_type, index < next.tuple_element_numeric_types.size()
                                                       ? next.tuple_element_numeric_types[index]
                                                       : unknown_numeric_type);
      array_storage = join_array_storage_formats(
          array_storage, index < next.tuple_array_storage.size()
                             ? next.tuple_array_storage[index]
                             : (next_type == ValueType::list ? ArrayStorageFormat::unknown
                                                             : ArrayStorageFormat::none));
      const auto next_shape =
          index < next.tuple_shapes.size() ? next.tuple_shapes[index] : std::vector<std::size_t>{};
      if (shape != next_shape) shape.clear();
    }
    semantic(semantics_, function).return_types[index] = type_conflict ? ValueType::unknown : type;
    semantic(semantics_, function).return_numeric_types[index] = numeric_type;
    semantic(semantics_, function).return_element_types[index] =
        element_conflict ? ValueType::unknown : element;
    semantic(semantics_, function).return_element_numeric_types[index] = element_numeric_type;
    semantic(semantics_, function).return_array_storage[index] = array_storage;
    semantic(semantics_, function).return_shapes[index] = std::move(shape);
  }
}

void Analyzer::infer_python_sequence_metadata(Statement& function) const {
  std::vector<const Expression*> returns;
  collect_value_returns(function.body, returns);
  if (returns.empty()) return;
  const auto first = expression_metadata(*returns.front(), semantics_);
  if (!first.sequence) return;
  for (std::size_t index = 1; index < returns.size(); ++index) {
    if (!same_metadata(first, expression_metadata(*returns[index], semantics_))) return;
  }
  semantic(semantics_, function).element_type = first.element_type;
  semantic(semantics_, function).element_numeric_type = first.element_numeric_type;
  semantic(semantics_, function).array_storage = first.array_storage;
  semantic(semantics_, function).shape = first.shape;
  semantic(semantics_, function).return_sequence_is_list = first.list_sequence;
  semantic(semantics_, function).return_sequence_elements = first.elements;
}

void Analyzer::analyze_function(Statement& function) {
  if (program_.semantics.emit_parameter_defaults) {
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
                 "default parameters currently require an immutable scalar literal");
      }
      analyze_expression(default_value);
    }
  }
  push_scope(names_.function_scope(function.id));
  semantic(semantics_, function).parameter_intents.clear();
  if (program_.language == SourceLanguage::fortran) {
    semantic(semantics_, function)
        .parameter_intents.assign(function.parameters.size(), ParameterIntent::none);
    semantic(semantics_, function).parameter_optional.assign(function.parameters.size(), false);
  }
  function_parameters_.emplace_back();
  function_parameters_.back().reserve(function.parameters.size());
  for (std::size_t index = 0; index < function.parameters.size(); ++index) {
    function_parameters_.back().push_back(definition(function, NameRole::parameter, index)->symbol);
  }
  for (auto& statement : function.body) {
    if (program_.language != SourceLanguage::fortran) break;
    if (statement.kind != StatementKind::declaration) continue;
    const auto declaration_symbol = definition(statement, NameRole::declaration)->symbol;
    const auto parameter = std::find(function_parameters_.back().begin(),
                                     function_parameters_.back().end(), declaration_symbol);
    if (parameter == function_parameters_.back().end()) continue;
    semantic(semantics_, statement).dummy_parameter = true;
    const auto position =
        static_cast<std::size_t>(std::distance(function_parameters_.back().begin(), parameter));
    if (semantic(semantics_, statement).optional_parameter) {
      semantic(semantics_, function).parameter_optional[position] = true;
    }
    if (semantic(semantics_, statement).parameter_intent != ParameterIntent::none) {
      if (semantic(semantics_, function).parameter_intents[position] != ParameterIntent::none &&
          semantic(semantics_, function).parameter_intents[position] !=
              semantic(semantics_, statement).parameter_intent) {
        diagnose(statement.line, "MPF2036",
                 "Fortran dummy argument has conflicting INTENT declarations");
      }
      semantic(semantics_, function).parameter_intents[position] =
          semantic(semantics_, statement).parameter_intent;
    }
  }
  function_parameter_reads_.emplace_back(function.parameters.size(), false);
  function_parameter_writes_.emplace_back(function.parameters.size(), false);
  function_parameter_intents_.push_back(&semantic(semantics_, function).parameter_intents);
  function_optional_parameters_.emplace_back();
  for (std::size_t index = 0; index < function.parameters.size(); ++index) {
    if (index < semantic(semantics_, function).parameter_optional.size() &&
        semantic(semantics_, function).parameter_optional[index]) {
      function_optional_parameters_.back().insert(function_parameters_.back()[index]);
    }
  }
  for (std::size_t index = 0; index < function.parameters.size(); ++index) {
    const auto annotated_type = index < semantic(semantics_, function).parameter_types.size()
                                    ? semantic(semantics_, function).parameter_types[index]
                                    : ValueType::unknown;
    const auto default_type =
        program_.semantics.emit_parameter_defaults && index < function.parameter_defaults.size() &&
                function.parameter_defaults[index].valid()
            ? semantic(semantics_, function.parameter_defaults[index]).inferred_type
            : ValueType::unknown;
    const auto default_numeric =
        program_.semantics.emit_parameter_defaults && index < function.parameter_defaults.size() &&
                function.parameter_defaults[index].valid()
            ? semantic(semantics_, function.parameter_defaults[index]).numeric_type
            : unknown_numeric_type;
    const auto default_storage =
        program_.semantics.emit_parameter_defaults && index < function.parameter_defaults.size() &&
                function.parameter_defaults[index].valid()
            ? semantic(semantics_, function.parameter_defaults[index]).array_storage
            : ArrayStorageFormat::none;
    const auto type = join_types(annotated_type, default_type);
    const auto element_type = index < semantic(semantics_, function).parameter_element_types.size()
                                  ? semantic(semantics_, function).parameter_element_types[index]
                                  : ValueType::unknown;
    const auto shape = index < semantic(semantics_, function).parameter_shapes.size()
                           ? semantic(semantics_, function).parameter_shapes[index]
                           : std::vector<std::size_t>{};
    definition_state(function, NameRole::parameter, index) = {
        type, BindingKind::variable,
        program_.language != SourceLanguage::fortran ||
            semantic(semantics_, function).parameter_intents[index] != ParameterIntent::out,
        element_type, shape};
    auto& parameter_state = definition_state(function, NameRole::parameter, index);
    parameter_state.numeric_type =
        index < semantic(semantics_, function).parameter_numeric_types.size()
            ? join_numeric_types(semantic(semantics_, function).parameter_numeric_types[index],
                                 default_numeric)
            : default_numeric;
    if (!parameter_state.numeric_type.known() && type != ValueType::unknown) {
      parameter_state.numeric_type = default_numeric_type(type);
    }
    parameter_state.element_numeric_type =
        index < semantic(semantics_, function).parameter_element_numeric_types.size()
            ? semantic(semantics_, function).parameter_element_numeric_types[index]
            : default_numeric_type(element_type);
    if (!parameter_state.element_numeric_type.known() && element_type != ValueType::unknown) {
      parameter_state.element_numeric_type = default_numeric_type(element_type);
    }
    parameter_state.array_storage =
        index < semantic(semantics_, function).parameter_array_storage.size()
            ? join_array_storage_formats(
                  semantic(semantics_, function).parameter_array_storage[index], default_storage)
            : default_storage;
    if (type == ValueType::list && parameter_state.array_storage == ArrayStorageFormat::none) {
      parameter_state.array_storage = ArrayStorageFormat::unknown;
    }
  }
  for (std::size_t index = 0; index < function.return_names.size(); ++index) {
    const auto type = index < semantic(semantics_, function).return_types.size()
                          ? semantic(semantics_, function).return_types[index]
                          : ValueType::unknown;
    definition_state(function, NameRole::result, index) = {
        type, BindingKind::variable, false, ValueType::unknown, {}};
    auto& result_state = definition_state(function, NameRole::result, index);
    result_state.numeric_type = index < semantic(semantics_, function).return_numeric_types.size()
                                    ? semantic(semantics_, function).return_numeric_types[index]
                                    : default_numeric_type(type);
    result_state.array_storage =
        index < semantic(semantics_, function).return_array_storage.size()
            ? semantic(semantics_, function).return_array_storage[index]
            : (type == ValueType::list ? ArrayStorageFormat::unknown : ArrayStorageFormat::none);
  }
  ++function_depth_;
  const auto saved_loop_depth = loop_depth_;
  loop_depth_ = 0;
  const auto body_terminates = analyze_statements(function.body);
  loop_depth_ = saved_loop_depth;
  --function_depth_;
  annotate_types(function.body);
  semantic(semantics_, function).parameter_types.clear();
  semantic(semantics_, function).parameter_numeric_types.clear();
  semantic(semantics_, function).parameter_element_types.clear();
  semantic(semantics_, function).parameter_element_numeric_types.clear();
  semantic(semantics_, function).parameter_array_storage.clear();
  semantic(semantics_, function).parameter_shapes.clear();
  semantic(semantics_, function).parameter_types.reserve(function.parameters.size());
  semantic(semantics_, function).parameter_numeric_types.reserve(function.parameters.size());
  semantic(semantics_, function).parameter_element_types.reserve(function.parameters.size());
  semantic(semantics_, function)
      .parameter_element_numeric_types.reserve(function.parameters.size());
  semantic(semantics_, function).parameter_array_storage.reserve(function.parameters.size());
  semantic(semantics_, function).parameter_shapes.reserve(function.parameters.size());
  for (std::size_t index = 0; index < function.parameters.size(); ++index) {
    const auto* parameter = lookup(definition(function, NameRole::parameter, index)->symbol);
    semantic(semantics_, function)
        .parameter_types.push_back(parameter == nullptr ? ValueType::unknown : parameter->type);
    semantic(semantics_, function)
        .parameter_numeric_types.push_back(parameter == nullptr ? unknown_numeric_type
                                                                : parameter->numeric_type);
    semantic(semantics_, function)
        .parameter_element_types.push_back(parameter == nullptr ? ValueType::unknown
                                                                : parameter->element_type);
    semantic(semantics_, function)
        .parameter_element_numeric_types.push_back(
            parameter == nullptr ? unknown_numeric_type : parameter->element_numeric_type);
    semantic(semantics_, function)
        .parameter_array_storage.push_back(parameter == nullptr ? ArrayStorageFormat::none
                                                                : parameter->array_storage);
    semantic(semantics_, function)
        .parameter_shapes.push_back(parameter == nullptr ? std::vector<std::size_t>{}
                                                         : parameter->shape);
  }
  for (std::size_t index = 0;
       program_.language == SourceLanguage::fortran && index < function.parameters.size();
       ++index) {
    auto& intent = semantic(semantics_, function).parameter_intents[index];
    if (intent == ParameterIntent::none) {
      intent = function_parameter_writes_.back()[index]
                   ? (function_parameter_reads_.back()[index] ? ParameterIntent::inout
                                                              : ParameterIntent::out)
                   : ParameterIntent::in;
    }
    const bool optional = index < semantic(semantics_, function).parameter_optional.size() &&
                          semantic(semantics_, function).parameter_optional[index];
    if (intent == ParameterIntent::out && !optional) {
      const auto* parameter = lookup(definition(function, NameRole::parameter, index)->symbol);
      if (parameter == nullptr || !parameter->assigned) {
        diagnose(function.line, "MPF2036",
                 "Fortran INTENT(OUT) dummy argument '" + function.parameters[index] +
                     "' is not definitely assigned");
      }
    }
  }
  bool has_value_return = false;
  bool has_empty_return = false;
  bool incompatible_returns = false;
  const auto annotated_return_type = semantic(semantics_, function).declared_type;
  semantic(semantics_, function).declared_type =
      collect_return_type(function.body, has_value_return, has_empty_return, incompatible_returns);
  if (function.return_names.empty()) {
    std::vector<const Expression*> value_returns;
    collect_value_returns(function.body, value_returns);
    if (value_returns.empty()) {
      semantic(semantics_, function).declared_numeric_type = no_numeric_type;
    } else {
      auto numeric_type = semantic(semantics_, *value_returns.front()).numeric_type;
      for (std::size_t index = 1; index < value_returns.size(); ++index) {
        const auto candidate = semantic(semantics_, *value_returns[index]).numeric_type;
        numeric_type = numeric_type == unknown_numeric_type || candidate == unknown_numeric_type
                           ? unknown_numeric_type
                           : join_numeric_types(numeric_type, candidate);
      }
      if (numeric_type == unknown_numeric_type && annotated_return_type != ValueType::unknown) {
        numeric_type = default_numeric_type(annotated_return_type);
      }
      semantic(semantics_, function).declared_numeric_type = numeric_type;
    }
  }
  semantic(semantics_, function).has_value_return = has_value_return;
  (void)body_terminates;
  (void)has_value_return;
  (void)has_empty_return;
  (void)incompatible_returns;
  std::vector<ValueType> output_types;
  std::vector<NumericType> output_numeric_types;
  std::vector<ValueType> output_element_types;
  std::vector<NumericType> output_element_numeric_types;
  std::vector<ArrayStorageFormat> output_array_storage;
  std::vector<std::vector<std::size_t>> output_shapes;
  output_types.reserve(function.return_names.size());
  output_numeric_types.reserve(function.return_names.size());
  output_element_types.reserve(function.return_names.size());
  output_element_numeric_types.reserve(function.return_names.size());
  output_array_storage.reserve(function.return_names.size());
  output_shapes.reserve(function.return_names.size());
  for (std::size_t index = 0; index < function.return_names.size(); ++index) {
    const auto& result = function.return_names[index];
    const auto* symbol = lookup(definition(function, NameRole::result, index)->symbol);
    if (symbol == nullptr || !symbol->assigned) {
      diagnose(function.line, "MPF2004",
               "function result '" + result + "' is not definitely assigned");
      output_types.push_back(ValueType::unknown);
      output_numeric_types.push_back(unknown_numeric_type);
      output_element_types.push_back(ValueType::unknown);
      output_element_numeric_types.push_back(unknown_numeric_type);
      output_array_storage.push_back(ArrayStorageFormat::none);
      output_shapes.emplace_back();
    } else {
      output_types.push_back(symbol->type);
      output_numeric_types.push_back(symbol->numeric_type);
      output_element_types.push_back(symbol->element_type);
      output_element_numeric_types.push_back(symbol->element_numeric_type);
      output_array_storage.push_back(symbol->array_storage);
      output_shapes.push_back(symbol->shape);
    }
  }
  semantic(semantics_, function).return_types = output_types;
  semantic(semantics_, function).return_numeric_types = output_numeric_types;
  semantic(semantics_, function).return_element_types = output_element_types;
  semantic(semantics_, function).return_element_numeric_types = output_element_numeric_types;
  semantic(semantics_, function).return_array_storage = output_array_storage;
  semantic(semantics_, function).return_shapes = output_shapes;
  if (output_types.size() == 1) {
    semantic(semantics_, function).declared_type = output_types.front();
    semantic(semantics_, function).declared_numeric_type = output_numeric_types.front();
    semantic(semantics_, function).array_storage = output_array_storage.front();
  } else if (output_types.size() > 1) {
    semantic(semantics_, function).declared_type = ValueType::tuple;
    semantic(semantics_, function).declared_numeric_type = no_numeric_type;
  }
  if (program_.semantics.emit_parameter_defaults &&
      semantic(semantics_, function).declared_type == ValueType::tuple) {
    infer_python_tuple_returns(function);
  }
  if (program_.semantics.emit_parameter_defaults &&
      (semantic(semantics_, function).declared_type == ValueType::tuple ||
       semantic(semantics_, function).declared_type == ValueType::list)) {
    infer_python_sequence_metadata(function);
  }
  function_parameter_intents_.pop_back();
  function_optional_parameters_.pop_back();
  function_parameter_writes_.pop_back();
  function_parameter_reads_.pop_back();
  function_parameters_.pop_back();
  scopes_.pop_back();
}

ValueType Analyzer::collect_return_type(const std::vector<Statement>& statements, bool& has_value,
                                        bool& has_empty, bool& incompatible) const {
  ValueType result = ValueType::unknown;
  for (const auto& statement : statements) {
    if (statement.kind == StatementKind::function) continue;
    if (statement.kind == StatementKind::return_statement) {
      if (!statement.has_expression) {
        has_empty = true;
      } else {
        has_value = true;
        const auto joined =
            join_types(result, semantic(semantics_, statement.expression).inferred_type);
        if (result != ValueType::unknown &&
            semantic(semantics_, statement.expression).inferred_type != ValueType::unknown &&
            joined == ValueType::unknown) {
          incompatible = true;
        }
        result = joined;
      }
    }
    const auto body_type = collect_return_type(statement.body, has_value, has_empty, incompatible);
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

void Analyzer::annotate_types(std::vector<Statement>& statements) {
  for (auto& statement : statements) {
    if (statement.kind == StatementKind::assignment ||
        statement.kind == StatementKind::range_loop || statement.kind == StatementKind::for_loop) {
      const auto role = statement.kind == StatementKind::assignment ? NameRole::assignment
                                                                    : NameRole::loop_variable;
      const auto* use = definition(statement, role);
      const auto* symbol = use == nullptr ? nullptr : lookup(use->symbol);
      if (symbol != nullptr) {
        semantic(semantics_, statement).declared_type = symbol->type;
        semantic(semantics_, statement).declared_numeric_type = symbol->numeric_type;
        semantic(semantics_, statement).element_type = symbol->element_type;
        semantic(semantics_, statement).element_numeric_type = symbol->element_numeric_type;
        semantic(semantics_, statement).array_storage = symbol->array_storage;
        semantic(semantics_, statement).shape = symbol->shape;
        if (statement.kind == StatementKind::assignment &&
            statement.expression.kind == ExpressionKind::list) {
          semantic(semantics_, statement.expression).element_type = symbol->element_type;
          semantic(semantics_, statement.expression).element_numeric_type =
              symbol->element_numeric_type;
          semantic(semantics_, statement.expression).array_storage = symbol->array_storage;
        }
      }
    }
    if (statement.kind != StatementKind::function) {
      annotate_types(statement.body);
      annotate_types(statement.alternative);
    }
  }
}

void Analyzer::refresh_expression_call_intents(Expression& expression) {
  if (expression.kind == ExpressionKind::call && !expression.children.empty()) {
    const auto& callee = expression.children.front();
    if (callee.kind == ExpressionKind::identifier &&
        semantic(semantics_, callee).binding == BindingKind::function) {
      const auto* use = names_.reference(callee.id);
      const auto* symbol = use == nullptr ? nullptr : names_.symbol(use->symbol);
      const auto* function =
          symbol == nullptr ? nullptr : find_statement(program_.statements, symbol->declaration);
      if (function != nullptr && function->kind == StatementKind::function) {
        semantic(semantics_, expression).argument_intents =
            semantic(semantics_, *function).parameter_intents;
        semantic(semantics_, expression).procedure_has_result =
            !function->return_names.empty() || semantic(semantics_, *function).has_value_return;
      }
    }
  }
  for (auto& child : expression.children) refresh_expression_call_intents(child);
}

void Analyzer::refresh_call_intents(std::vector<Statement>& statements) {
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

}  // namespace mpf::detail::semantic_internal

namespace mpf::detail {

AnalysisResult analyze_program(hir::Program& program, hir::SemanticTable semantic_seed) {
  AnalysisResult result;
  result.semantics = std::move(semantic_seed);
  result.diagnostics = hir::verify_semantics(program, result.semantics, "frontend-seed");
  if (!result.diagnostics.empty()) return result;
  auto name_result = analyze_names(program);
  result.diagnostics = std::move(name_result.diagnostics);
  auto flow_result = analyze_flow(program);
  semantic_internal::Analyzer analyzer(program, result.semantics, name_result.names,
                                       flow_result.flow);
  auto analyzer_diagnostics = analyzer.analyze();
  result.diagnostics.insert(result.diagnostics.end(),
                            std::make_move_iterator(analyzer_diagnostics.begin()),
                            std::make_move_iterator(analyzer_diagnostics.end()));
  if (analyzer.structure_changed()) ++program.revision;
  result.semantics = hir::reindex_semantics(program, std::move(result.semantics));
  if (analyzer.structure_changed()) {
    name_result = analyze_names(program);
    result.diagnostics.insert(result.diagnostics.end(),
                              std::make_move_iterator(name_result.diagnostics.begin()),
                              std::make_move_iterator(name_result.diagnostics.end()));
    flow_result = analyze_flow(program);
  }
  result.names = std::move(name_result.names);
  result.flow = std::move(flow_result.flow);
  result.diagnostics.insert(result.diagnostics.end(),
                            std::make_move_iterator(flow_result.diagnostics.begin()),
                            std::make_move_iterator(flow_result.diagnostics.end()));
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
