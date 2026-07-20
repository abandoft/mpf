#include "mir.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

#include "mir_opcode.hpp"
#include "semantic/name_analysis.hpp"

namespace mpf::detail::mir {
namespace {

std::uint64_t type_key(const ValueType type, const ValueType element_type,
                       const NumericType numeric_type, const NumericType element_numeric_type,
                       const ArrayStorageFormat array_storage) noexcept {
  return (static_cast<std::uint64_t>(type) << 48U) |
         (static_cast<std::uint64_t>(element_type) << 40U) |
         (static_cast<std::uint64_t>(numeric_type.value_class) << 32U) |
         (static_cast<std::uint64_t>(numeric_type.complexity) << 24U) |
         (static_cast<std::uint64_t>(element_numeric_type.value_class) << 16U) |
         (static_cast<std::uint64_t>(element_numeric_type.complexity) << 8U) |
         static_cast<std::uint64_t>(array_storage);
}

std::string composite_type_key(const char prefix, const std::vector<TypeId>& first,
                               const std::vector<TypeId>& second) {
  std::string key(1, prefix);
  const auto append = [&](const std::vector<TypeId>& values) {
    key.push_back(':');
    key += std::to_string(values.size());
    for (const auto value : values) {
      key.push_back(':');
      key += std::to_string(value.value());
    }
  };
  append(first);
  append(second);
  return key;
}

std::string shape_key(const std::vector<std::size_t>& shape, const semantic::IndexLayout layout) {
  std::string key = layout == semantic::IndexLayout::column_major ? "c" : "r";
  for (const auto extent : shape) {
    key.push_back(':');
    key += std::to_string(extent);
  }
  return key;
}

bool contains_slice(const Program& program, const MirExpressionId expression_id) {
  const auto* node = expression(program, expression_id);
  if (node == nullptr) return false;
  if (node->kind == ExpressionKind::slice) return true;
  return std::any_of(node->children.begin(), node->children.end(),
                     [&](const MirExpressionId child) { return contains_slice(program, child); });
}

class Builder final {
 public:
  Builder(Program& program, const hir::SemanticTable& semantics, const NameTable& names)
      : program_(program), semantics_(semantics), names_(names) {}

  void begin_function(std::string name, const HirNodeId origin, const SymbolId symbol = {},
                      const bool exported = false) {
    storages_.clear();
    storage_values_.clear();
    Function function;
    function.id = function_ids_.next();
    function.origin = origin;
    function.symbol = symbol;
    function.name = std::move(name);
    function.exported = exported;
    function.entry = make_block();
    function.blocks.push_back(function.entry);
    program_.functions.push_back(std::move(function));
    current_function_ = program_.functions.back().id;
    current_block_ = program_.functions.back().entry;
  }

  void finish_function() {
    auto& block = current_block();
    if (block.terminator.kind == TerminatorKind::none) {
      block.terminator.kind = TerminatorKind::return_value;
    }
    if (!current_function().signature.valid()) {
      current_function().signature = intern_function_type({}, {});
    }
    current_function_ = {};
    current_block_ = {};
  }

  void link_calls() {
    std::unordered_map<SymbolId, MirFunctionId> functions;
    for (std::size_t index = 1; index < program_.functions.size(); ++index) {
      if (program_.functions[index].symbol.valid()) {
        functions.emplace(program_.functions[index].symbol, program_.functions[index].id);
      }
    }
    program_.calls.reserve(unresolved_calls_.size());
    for (auto& unresolved : unresolved_calls_) {
      CallSite call;
      call.instruction = unresolved.instruction;
      call.origin = unresolved.origin;
      call.caller = unresolved.caller;
      const auto found = functions.find(unresolved.callee_symbol);
      if (found != functions.end()) {
        call.callee = found->second;
        program_.instructions[call.instruction.value()].callee = call.callee;
      }
      call.arguments = std::move(unresolved.arguments);
      call.result_type = unresolved.result_type;
      call.requested_results = unresolved.requested_results;
      program_.calls.push_back(std::move(call));
    }
  }

  MirStatementId lower_statement(hir::Statement&& source) {
    ensure_open_block();
    Statement result;
    StatementAttributes result_attributes;
    result.id = statement_ids_.next();
    if (program_.statements.size() <= result.id.value()) {
      program_.statements.resize(static_cast<std::size_t>(result.id.value()) + 1U);
      program_.attributes.statements.resize(static_cast<std::size_t>(result.id.value()) + 1U);
    }
    result_attributes.origin = result.id;
    const auto* semantic_facts = semantics_.statement(source.id);
    result.origin = source.id;
    result.kind = source.kind;
    result.line = source.line;
    result.name = std::move(source.name);
    const auto definition_role = [&]() {
      switch (result.kind) {
        case StatementKind::function:
        case StatementKind::declaration: return NameRole::declaration;
        case StatementKind::assignment:
        case StatementKind::multi_assignment: return NameRole::assignment;
        case StatementKind::range_loop:
        case StatementKind::for_loop: return NameRole::loop_variable;
        case StatementKind::indexed_assignment:
        case StatementKind::print:
        case StatementKind::return_statement:
        case StatementKind::break_statement:
        case StatementKind::continue_statement:
        case StatementKind::expression:
        case StatementKind::if_statement:
        case StatementKind::select_case:
        case StatementKind::case_clause:
        case StatementKind::while_loop: return NameRole::reference;
      }
      return NameRole::reference;
    }();
    if (definition_role != NameRole::reference) {
      const auto* use = names_.use(source.id, definition_role);
      result.symbol_id = use == nullptr ? SymbolId{} : use->symbol;
    }

    const bool for_loop = source.kind == StatementKind::for_loop;
    const bool loop =
        source.kind == StatementKind::while_loop || source.kind == StatementKind::range_loop;
    BlockId loop_condition;
    BlockId loop_body;
    BlockId loop_else;
    BlockId loop_exit;
    BlockId loop_preheader;
    StorageVersions loop_entry_versions;
    if (loop) {
      loop_preheader = current_block_;
      loop_entry_versions = storage_values_;
      loop_condition = make_function_block();
      loop_body = make_function_block();
      loop_else = make_function_block();
      loop_exit = make_function_block();
      set_branch(loop_condition, source.id);
      current_block_ = loop_condition;
    }

    result.expression = lower_expression(std::move(source.expression));
    result.has_expression = source.has_expression;
    result_attributes.procedure_call = source.procedure_call;
    result.has_secondary_expression = source.has_secondary_expression;
    result.has_tertiary_expression = source.has_tertiary_expression;
    if (!for_loop) {
      result.secondary_expression = lower_expression(std::move(source.secondary_expression));
      result.tertiary_expression = lower_expression(std::move(source.tertiary_expression));
    }
    result_attributes.inclusive_stop = source.inclusive_stop;
    result_attributes.retain_last_loop_value = source.retain_last_loop_value;
    if (semantic_facts != nullptr) {
      result_attributes.previous_type = intern_type(
          semantic_facts->previous_type, semantic_facts->previous_element_type,
          semantic_facts->previous_numeric_type, semantic_facts->previous_element_numeric_type,
          semantic_facts->previous_array_storage);
      result_attributes.indexed_mutation.contract = semantic_facts->indexed_mutation;
      if (semantic_facts->indexed_mutation.valid()) {
        const bool column_major = program_.source_language == SourceLanguage::matlab;
        result_attributes.indexed_mutation.input_shape =
            intern_shape(semantic_facts->mutation_input_shape, column_major);
        result_attributes.indexed_mutation.result_shape =
            intern_shape(semantic_facts->mutation_result_shape, column_major);
      }
      const auto& sparse = semantic_facts->sparse_mutation;
      result_attributes.sparse_mutation.kind = sparse.kind;
      result_attributes.sparse_mutation.replacement = sparse.replacement;
      result_attributes.sparse_mutation.duplicate_policy = sparse.duplicate_policy;
      result_attributes.sparse_mutation.zero_policy = sparse.zero_policy;
      result_attributes.sparse_mutation.source_storage = sparse.source_storage;
      result_attributes.sparse_mutation.replacement_storage = sparse.replacement_storage;
      result_attributes.sparse_mutation.result_storage = sparse.result_storage;
      if (sparse.valid()) {
        const bool column_major = program_.source_language == SourceLanguage::matlab;
        result_attributes.sparse_mutation.input_shape =
            intern_shape(sparse.input_shape, column_major);
        result_attributes.sparse_mutation.selection_shape =
            intern_shape(sparse.selection_shape, column_major);
        result_attributes.sparse_mutation.replacement_shape =
            intern_shape(sparse.replacement_shape, column_major);
        result_attributes.sparse_mutation.result_shape =
            intern_shape(sparse.result_shape, column_major);
      }
    }
    result.target_expression = lower_expression(std::move(source.target_expression));
    result.has_target_expression = source.has_target_expression;
    result.parameters = std::move(source.parameters);
    result.parameter_symbols.reserve(result.parameters.size());
    for (std::size_t index = 0; index < result.parameters.size(); ++index) {
      const auto* use = names_.use(source.id, NameRole::parameter, index);
      result.parameter_symbols.push_back(use == nullptr ? SymbolId{} : use->symbol);
    }
    result.parameter_kinds = std::move(source.parameter_kinds);
    result.parameter_defaults.reserve(source.parameter_defaults.size());
    for (auto& expression : source.parameter_defaults) {
      result.parameter_defaults.push_back(lower_expression(std::move(expression)));
    }
    result.return_names = std::move(source.return_names);
    result.return_symbols.reserve(result.return_names.size());
    for (std::size_t index = 0; index < result.return_names.size(); ++index) {
      const auto* use = names_.use(source.id, NameRole::result, index);
      result.return_symbols.push_back(use == nullptr ? SymbolId{} : use->symbol);
    }
    result.target_names = std::move(source.target_names);
    result.target_symbols.reserve(result.target_names.size());
    for (std::size_t index = 0; index < result.target_names.size(); ++index) {
      const auto* use = names_.use(source.id, NameRole::assignment, index);
      result.target_symbols.push_back(use == nullptr ? SymbolId{} : use->symbol);
    }
    result.has_target_pattern = source.has_target_pattern;
    if (semantic_facts != nullptr) {
      result_attributes.target_pattern = intern_assignment_pattern(semantic_facts->target_pattern);
      result_attributes.targets.reserve(semantic_facts->target_types.size());
      for (std::size_t index = 0; index < semantic_facts->target_types.size(); ++index) {
        const auto target_type = semantic_facts->target_types[index];
        const auto target_element = index < semantic_facts->target_element_types.size()
                                        ? semantic_facts->target_element_types[index]
                                        : ValueType::unknown;
        const auto target_shape = index < semantic_facts->target_shapes.size()
                                      ? semantic_facts->target_shapes[index]
                                      : std::vector<std::size_t>{};
        const auto previous_type = index < semantic_facts->target_previous_types.size()
                                       ? semantic_facts->target_previous_types[index]
                                       : ValueType::unknown;
        const auto previous_element = index < semantic_facts->target_previous_element_types.size()
                                          ? semantic_facts->target_previous_element_types[index]
                                          : ValueType::unknown;
        const auto target_numeric = index < semantic_facts->target_numeric_types.size()
                                        ? semantic_facts->target_numeric_types[index]
                                        : unknown_numeric_type;
        const auto target_element_numeric =
            index < semantic_facts->target_element_numeric_types.size()
                ? semantic_facts->target_element_numeric_types[index]
                : unknown_numeric_type;
        const auto previous_numeric = index < semantic_facts->target_previous_numeric_types.size()
                                          ? semantic_facts->target_previous_numeric_types[index]
                                          : unknown_numeric_type;
        const auto previous_element_numeric =
            index < semantic_facts->target_previous_element_numeric_types.size()
                ? semantic_facts->target_previous_element_numeric_types[index]
                : unknown_numeric_type;
        const auto target_array_storage = index < semantic_facts->target_array_storage.size()
                                              ? semantic_facts->target_array_storage[index]
                                              : ArrayStorageFormat::none;
        const auto previous_array_storage =
            index < semantic_facts->target_previous_array_storage.size()
                ? semantic_facts->target_previous_array_storage[index]
                : ArrayStorageFormat::none;
        result_attributes.targets.push_back(
            {intern_type(target_type, target_element, target_numeric, target_element_numeric,
                         target_array_storage),
             intern_shape(target_shape, false),
             intern_type(previous_type, previous_element, previous_numeric,
                         previous_element_numeric, previous_array_storage)});
      }
    }
    result.case_selectors.reserve(source.case_selectors.size());
    for (auto& selector : source.case_selectors) {
      result.case_selectors.push_back(lower_selector(std::move(selector)));
    }
    result.default_case = source.default_case;

    if (result.kind == StatementKind::function) {
      initialize_function_signature(result, semantic_facts);
    }

    emit_statement_instruction(result, result_attributes, semantic_facts);

    if (for_loop) {
      const auto for_preheader = current_block_;
      const auto for_entry_versions = storage_values_;
      const auto for_condition = make_function_block();
      const auto for_body = make_function_block();
      const auto for_update = make_function_block();
      const auto for_exit = make_function_block();
      set_branch(for_condition, source.id);

      current_block_ = for_condition;
      storage_values_ = for_entry_versions;
      result.secondary_expression = lower_expression(std::move(source.secondary_expression));
      const auto* condition = expression(program_, result.secondary_expression);
      const auto condition_exit = current_block_;
      const auto condition_versions = storage_values_;
      set_conditional(condition == nullptr ? ValueId{} : condition->value_id, for_body, for_exit,
                      source.id);

      loops_.push_back({for_update, for_exit, {}, {}});
      current_block_ = for_body;
      storage_values_ = condition_versions;
      lower_statement_list(std::move(source.body), result.body);
      auto context = std::move(loops_.back());
      loops_.pop_back();
      if (current_block().terminator.kind == TerminatorKind::none) {
        set_branch(for_update, source.id);
      }
      if (std::find(current_block().terminator.successors.begin(),
                    current_block().terminator.successors.end(),
                    for_update) != current_block().terminator.successors.end() &&
          std::none_of(context.continue_edges.begin(), context.continue_edges.end(),
                       [&](const ControlEdge& edge) { return edge.block == current_block_; })) {
        context.continue_edges.push_back({current_block_, storage_values_});
      }

      current_block_ = for_update;
      if (context.continue_edges.empty()) {
        storage_values_ = for_entry_versions;
      } else {
        storage_values_ =
            merge_storage_versions(for_update, context.continue_edges, for_entry_versions);
      }
      result.tertiary_expression = lower_expression(std::move(source.tertiary_expression));
      const auto* update = expression(program_, result.tertiary_expression);
      const auto* initializer =
          result.instruction.valid() && result.instruction.value() < program_.instructions.size()
              ? &program_.instructions[result.instruction.value()]
              : nullptr;
      if (initializer != nullptr && initializer->storage.valid() && update != nullptr &&
          update->value_id.valid()) {
        Instruction store;
        store.id = instruction_ids_.next();
        store.opcode = Opcode::store;
        store.origin = result.origin;
        store.location = {result.line, 1};
        store.result = value_ids_.next();
        store.type = initializer->type;
        store.shape = initializer->shape;
        store.storage = initializer->storage;
        store.operands.push_back(update->value_id);
        storage_values_[store.storage] = store.result;
        append_instruction(std::move(store), {make_memory_access(initializer->storage,
                                                                 full_region(initializer->storage),
                                                                 MemoryAccessMode::write)});
      }
      const auto update_versions = storage_values_;
      const auto update_exit = current_block_;
      if (context.continue_edges.empty()) {
        current_block().terminator.kind = TerminatorKind::unreachable;
        current_block().terminator.origin = source.id;
      } else {
        set_branch(for_condition, source.id);
      }

      std::vector<ControlEdge> header_incoming{{for_preheader, for_entry_versions}};
      if (!context.continue_edges.empty()) {
        header_incoming.push_back({update_exit, update_versions});
      }
      (void)merge_storage_versions(for_condition, header_incoming, for_entry_versions);
      std::vector<ControlEdge> exit_incoming = std::move(context.break_edges);
      exit_incoming.push_back({condition_exit, condition_versions});
      current_block_ = for_exit;
      storage_values_ = merge_storage_versions(for_exit, exit_incoming, for_entry_versions);
    } else if (source.kind == StatementKind::if_statement) {
      const auto entry_versions = storage_values_;
      const auto then_block = make_function_block();
      const auto else_block = make_function_block();
      const auto merge_block = make_function_block();
      const auto* condition = expression(program_, result.expression);
      set_conditional(condition == nullptr ? ValueId{} : condition->value_id, then_block,
                      else_block, source.id);
      current_block_ = then_block;
      lower_statement_list(std::move(source.body), result.body);
      if (current_block().terminator.kind == TerminatorKind::none) {
        set_branch(merge_block, source.id);
      }
      std::vector<ControlEdge> incoming;
      if (std::find(current_block().terminator.successors.begin(),
                    current_block().terminator.successors.end(),
                    merge_block) != current_block().terminator.successors.end()) {
        incoming.push_back({current_block_, storage_values_});
      }
      storage_values_ = entry_versions;
      current_block_ = else_block;
      lower_statement_list(std::move(source.alternative), result.alternative);
      if (current_block().terminator.kind == TerminatorKind::none) {
        set_branch(merge_block, source.id);
      }
      if (std::find(current_block().terminator.successors.begin(),
                    current_block().terminator.successors.end(),
                    merge_block) != current_block().terminator.successors.end()) {
        incoming.push_back({current_block_, storage_values_});
      }
      current_block_ = merge_block;
      if (incoming.empty()) {
        current_block().terminator.kind = TerminatorKind::unreachable;
        current_block().terminator.origin = source.id;
        storage_values_ = entry_versions;
      } else {
        storage_values_ = merge_storage_versions(merge_block, incoming, entry_versions);
      }
    } else if (source.kind == StatementKind::select_case) {
      const auto entry_versions = storage_values_;
      const auto merge_block = make_function_block();
      std::vector<ControlEdge> incoming;
      result.body.reserve(source.body.size());
      bool has_default = false;
      for (std::size_t index = 0; index < source.body.size(); ++index) {
        auto& clause = source.body[index];
        const auto clause_block = make_function_block();
        BlockId next_decision = merge_block;
        if (!clause.default_case && index + 1U < source.body.size()) {
          next_decision = make_function_block();
        }
        if (clause.default_case) {
          has_default = true;
          set_branch(clause_block, clause.id);
        } else {
          const auto* selector = expression(program_, result.expression);
          const auto predicate =
              emit_case_predicate(clause, selector == nullptr ? ValueId{} : selector->value_id);
          set_conditional(predicate, clause_block, next_decision, clause.id);
          if (next_decision == merge_block) {
            incoming.push_back({current_block_, entry_versions});
          }
        }
        storage_values_ = entry_versions;
        current_block_ = clause_block;
        result.body.push_back(lower_statement(std::move(clause)));
        if (current_block().terminator.kind == TerminatorKind::none) {
          set_branch(merge_block, result.origin);
        }
        if (std::find(current_block().terminator.successors.begin(),
                      current_block().terminator.successors.end(),
                      merge_block) != current_block().terminator.successors.end()) {
          incoming.push_back({current_block_, storage_values_});
        }
        if (has_default) break;
        current_block_ = next_decision;
      }
      current_block_ = merge_block;
      if (incoming.empty()) {
        current_block().terminator.kind = TerminatorKind::unreachable;
        current_block().terminator.origin = source.id;
        storage_values_ = entry_versions;
      } else {
        storage_values_ = merge_storage_versions(merge_block, incoming, entry_versions);
      }
    } else if (loop) {
      const auto* condition = expression(program_, result.expression);
      set_conditional(condition == nullptr ? ValueId{} : condition->value_id, loop_body, loop_else,
                      source.id);
      loops_.push_back({loop_condition, loop_exit, {}, {}});
      current_block_ = loop_body;
      lower_statement_list(std::move(source.body), result.body);
      auto context = std::move(loops_.back());
      loops_.pop_back();
      if (current_block().terminator.kind == TerminatorKind::none) {
        set_branch(loop_condition, source.id);
      }
      if (std::find(current_block().terminator.successors.begin(),
                    current_block().terminator.successors.end(),
                    loop_condition) != current_block().terminator.successors.end() &&
          std::none_of(context.continue_edges.begin(), context.continue_edges.end(),
                       [&](const ControlEdge& edge) { return edge.block == current_block_; })) {
        context.continue_edges.push_back({current_block_, storage_values_});
      }
      std::vector<ControlEdge> header_incoming{{loop_preheader, loop_entry_versions}};
      header_incoming.insert(header_incoming.end(), context.continue_edges.begin(),
                             context.continue_edges.end());
      storage_values_ =
          merge_storage_versions(loop_condition, header_incoming, loop_entry_versions);
      current_block_ = loop_else;
      lower_statement_list(std::move(source.alternative), result.alternative);
      if (current_block().terminator.kind == TerminatorKind::none) {
        set_branch(loop_exit, source.id);
      }
      std::vector<ControlEdge> exit_incoming = std::move(context.break_edges);
      if (std::find(current_block().terminator.successors.begin(),
                    current_block().terminator.successors.end(),
                    loop_exit) != current_block().terminator.successors.end()) {
        exit_incoming.push_back({current_block_, storage_values_});
      }
      current_block_ = loop_exit;
      if (exit_incoming.empty()) {
        current_block().terminator.kind = TerminatorKind::unreachable;
        current_block().terminator.origin = source.id;
        storage_values_ = loop_entry_versions;
      } else {
        storage_values_ = merge_storage_versions(loop_exit, exit_incoming, loop_entry_versions);
      }
    } else {
      lower_statement_list(std::move(source.body), result.body);
      lower_statement_list(std::move(source.alternative), result.alternative);
    }

    if (result.kind == StatementKind::return_statement) {
      auto& terminator = current_block().terminator;
      terminator.kind = TerminatorKind::return_value;
      terminator.origin = result.origin;
      const auto* returned = expression(program_, result.expression);
      if (returned != nullptr && returned->value_id.valid()) {
        terminator.operands.push_back(returned->value_id);
      }
    }
    if (result.kind == StatementKind::break_statement && !loops_.empty()) {
      loops_.back().break_edges.push_back({current_block_, storage_values_});
      set_branch(loops_.back().break_target, result.origin);
    } else if (result.kind == StatementKind::continue_statement && !loops_.empty()) {
      loops_.back().continue_edges.push_back({current_block_, storage_values_});
      set_branch(loops_.back().continue_target, result.origin);
    }
    const auto id = result.id;
    program_.statements[id.value()] = std::move(result);
    program_.attributes.statements[id.value()] = std::move(result_attributes);
    return id;
  }

  [[nodiscard]] TypeId intern_type(
      const ValueType type, const ValueType element_type,
      const NumericType numeric_type = unknown_numeric_type,
      const NumericType element_numeric_type = unknown_numeric_type,
      const ArrayStorageFormat array_storage = ArrayStorageFormat::none) {
    const auto normalized_element_numeric_type =
        type == ValueType::list || type == ValueType::unknown ? element_numeric_type
                                                              : no_numeric_type;
    const auto normalized_array_storage =
        type == ValueType::list
            ? (array_storage == ArrayStorageFormat::none ? ArrayStorageFormat::dense
                                                         : array_storage)
        : type == ValueType::unknown && array_storage == ArrayStorageFormat::unknown
            ? ArrayStorageFormat::unknown
            : ArrayStorageFormat::none;
    const auto key = type_key(type, element_type, numeric_type, normalized_element_numeric_type,
                              normalized_array_storage);
    const auto found = types_.find(key);
    if (found != types_.end()) return found->second;
    const auto id = TypeId{static_cast<TypeId::value_type>(program_.types.size())};
    const auto kind = type == ValueType::list       ? TypeKind::sequence
                      : type == ValueType::tuple    ? TypeKind::tuple
                      : type == ValueType::function ? TypeKind::function
                                                    : TypeKind::scalar;
    program_.types.push_back({kind,
                              type,
                              element_type,
                              {},
                              {},
                              {},
                              {},
                              ParameterIntent::none,
                              numeric_type,
                              normalized_element_numeric_type,
                              normalized_array_storage});
    types_.emplace(key, id);
    return id;
  }

  [[nodiscard]] TypeId intern_tuple_type(const std::vector<TypeId>& elements) {
    auto key = composite_type_key('t', elements, {});
    const auto found = composite_types_.find(key);
    if (found != composite_types_.end()) return found->second;
    const auto id = TypeId{static_cast<TypeId::value_type>(program_.types.size())};
    program_.types.push_back({TypeKind::tuple,
                              ValueType::tuple,
                              ValueType::unknown,
                              elements,
                              {},
                              {},
                              {},
                              ParameterIntent::none,
                              no_numeric_type,
                              no_numeric_type});
    composite_types_.emplace(std::move(key), id);
    return id;
  }

  [[nodiscard]] TypeId intern_reference_type(const TypeId referent, const ParameterIntent intent) {
    std::string key = "r:" + std::to_string(referent.value()) + ':' +
                      std::to_string(static_cast<unsigned>(intent));
    const auto found = composite_types_.find(key);
    if (found != composite_types_.end()) return found->second;
    const auto id = TypeId{static_cast<TypeId::value_type>(program_.types.size())};
    program_.types.push_back({TypeKind::reference,
                              ValueType::unknown,
                              ValueType::unknown,
                              {},
                              {},
                              {},
                              referent,
                              intent,
                              unknown_numeric_type,
                              unknown_numeric_type});
    composite_types_.emplace(std::move(key), id);
    return id;
  }

  [[nodiscard]] TypeId intern_function_type(const std::vector<TypeId>& parameters,
                                            const std::vector<TypeId>& results) {
    auto key = composite_type_key('f', parameters, results);
    const auto found = composite_types_.find(key);
    if (found != composite_types_.end()) return found->second;
    const auto id = TypeId{static_cast<TypeId::value_type>(program_.types.size())};
    program_.types.push_back({TypeKind::function,
                              ValueType::function,
                              ValueType::unknown,
                              {},
                              parameters,
                              results,
                              {},
                              ParameterIntent::none,
                              no_numeric_type,
                              no_numeric_type});
    composite_types_.emplace(std::move(key), id);
    return id;
  }

  [[nodiscard]] TypeId intern_expression_type(const hir::ExpressionFacts* facts,
                                              const std::vector<MirExpressionId>& children) {
    if (facts == nullptr) return intern_type(ValueType::unknown, ValueType::unknown);
    if (facts->inferred_type != ValueType::tuple && facts->tuple_types.empty()) {
      return intern_type(facts->inferred_type, facts->element_type, facts->numeric_type,
                         facts->element_numeric_type, facts->array_storage);
    }
    std::vector<TypeId> elements;
    if (!facts->tuple_types.empty()) {
      elements.reserve(facts->tuple_types.size());
      for (std::size_t index = 0; index < facts->tuple_types.size(); ++index) {
        elements.push_back(intern_type(
            facts->tuple_types[index],
            index < facts->tuple_element_types.size() ? facts->tuple_element_types[index]
                                                      : ValueType::unknown,
            index < facts->tuple_numeric_types.size() ? facts->tuple_numeric_types[index]
                                                      : unknown_numeric_type,
            index < facts->tuple_element_numeric_types.size()
                ? facts->tuple_element_numeric_types[index]
                : unknown_numeric_type,
            index < facts->tuple_array_storage.size() ? facts->tuple_array_storage[index]
                                                      : ArrayStorageFormat::none));
      }
    } else {
      elements.reserve(children.size());
      for (const auto child : children) {
        const auto* node = mir::expression(program_, child);
        elements.push_back(node == nullptr ? TypeId{} : node->type_id);
      }
    }
    return intern_tuple_type(elements);
  }

  [[nodiscard]] ShapeId intern_shape(const std::vector<std::size_t>& shape,
                                     const bool column_major) {
    const auto layout =
        column_major ? semantic::IndexLayout::column_major : semantic::IndexLayout::row_major;
    const auto key = shape_key(shape, layout);
    const auto found = shapes_.find(key);
    if (found != shapes_.end()) return found->second;
    const auto id = ShapeId{static_cast<ShapeId::value_type>(program_.shapes.size())};
    std::vector<std::size_t> strides(shape.size(), 1U);
    std::size_t stride = 1U;
    for (std::size_t offset = 0; offset < shape.size(); ++offset) {
      const auto index =
          layout == semantic::IndexLayout::column_major ? offset : shape.size() - offset - 1U;
      strides[index] = stride;
      const auto extent = shape[index];
      if (extent == dynamic_extent || stride == dynamic_extent ||
          (extent != 0 && stride > std::numeric_limits<std::size_t>::max() / extent)) {
        stride = dynamic_extent;
      } else {
        stride *= extent;
      }
    }
    program_.shapes.push_back({shape, std::move(strides), layout, false});
    shapes_.emplace(key, id);
    return id;
  }

  [[nodiscard]] ValueMetadata intern_value_metadata(const detail::ValueMetadata& source) {
    ValueMetadata result;
    result.type = intern_type(source.type, source.element_type, source.numeric_type,
                              source.element_numeric_type, source.array_storage);
    result.shape = intern_shape(source.shape, false);
    result.sequence = source.sequence;
    result.list_sequence = source.list_sequence;
    result.elements.reserve(source.elements.size());
    for (const auto& element : source.elements) {
      result.elements.push_back(intern_value_metadata(element));
    }
    return result;
  }

  [[nodiscard]] AssignmentPattern intern_assignment_pattern(
      const detail::AssignmentPattern& source) {
    AssignmentPattern result;
    result.kind = source.kind;
    result.location = source.location;
    result.name = source.name;
    result.type = intern_type(source.type, source.element_type, source.numeric_type,
                              source.element_numeric_type, source.array_storage);
    result.shape = intern_shape(source.shape, false);
    result.previous_type = intern_type(
        source.previous_type, source.previous_element_type, source.previous_numeric_type,
        source.previous_element_numeric_type, source.previous_array_storage);
    result.access_path = source.access_path;
    result.captured_paths = source.captured_paths;
    result.children.reserve(source.children.size());
    for (const auto& child : source.children) {
      result.children.push_back(intern_assignment_pattern(child));
    }
    return result;
  }

 private:
  using StorageVersions = std::unordered_map<StorageId, ValueId>;
  struct UnresolvedCall {
    InstructionId instruction{};
    HirNodeId origin{};
    MirFunctionId caller{};
    SymbolId callee_symbol{};
    std::vector<CallSite::Argument> arguments;
    TypeId result_type{};
    std::size_t requested_results{1};
  };
  struct ControlEdge {
    BlockId block{};
    StorageVersions versions;
  };
  struct LoopContext {
    BlockId continue_target{};
    BlockId break_target{};
    std::vector<ControlEdge> continue_edges;
    std::vector<ControlEdge> break_edges;
  };

  ValueId emit_truthiness(const Expression& operand, const HirNodeId origin,
                          const std::optional<semantic::Truthiness> truthiness = std::nullopt) {
    Instruction instruction;
    instruction.id = instruction_ids_.next();
    instruction.opcode = Opcode::truthiness;
    instruction.origin = origin;
    instruction.location = operand.location;
    instruction.result = value_ids_.next();
    instruction.type = intern_type(ValueType::boolean, ValueType::unknown, logical_numeric_type);
    instruction.shape = intern_shape({}, false);
    instruction.truthiness = truthiness.value_or(program_.semantics.truthiness);
    instruction.operands.push_back(operand.value_id);
    const auto result = instruction.result;
    append_instruction(std::move(instruction));
    return result;
  }

  ValueId emit_comparison(const Expression& left, const Expression& right,
                          const ComparisonOperator comparison, const HirNodeId origin,
                          const SourceLocation location) {
    Instruction instruction;
    instruction.id = instruction_ids_.next();
    instruction.opcode = Opcode::compare;
    instruction.origin = origin;
    instruction.location = location;
    instruction.result = value_ids_.next();
    instruction.type = intern_type(ValueType::boolean, ValueType::unknown, logical_numeric_type);
    instruction.shape = intern_shape({}, false);
    instruction.comparison = comparison;
    instruction.operands = {left.value_id, right.value_id};
    const auto result = instruction.result;
    append_instruction(std::move(instruction));
    return result;
  }

  void append_edge_argument(const BlockId source, const BlockId target, const ValueId value) {
    auto& terminator = program_.blocks[source.value()].terminator;
    const auto found =
        std::find(terminator.successors.begin(), terminator.successors.end(), target);
    if (found == terminator.successors.end()) return;
    const auto index = static_cast<std::size_t>(found - terminator.successors.begin());
    if (terminator.successor_arguments.size() < terminator.successors.size()) {
      terminator.successor_arguments.resize(terminator.successors.size());
    }
    terminator.successor_arguments[index].push_back(value);
  }

  MirExpressionId lower_expression(hir::Expression&& source) {
    Expression result;
    ExpressionAttributes result_attributes;
    if (!source.valid()) return {};
    result.id = expression_ids_.next();
    if (program_.expressions.size() <= result.id.value()) {
      program_.expressions.resize(static_cast<std::size_t>(result.id.value()) + 1U);
      program_.attributes.expressions.resize(static_cast<std::size_t>(result.id.value()) + 1U);
    }
    result_attributes.origin = result.id;
    const auto* semantic_facts = semantics_.expression(source.id);
    result.origin = source.id;
    result.location = source.location;
    result.kind = source.kind;
    result_attributes.spelling = std::move(source.value);
    result_attributes.unary_operation = source.unary_operation;
    result_attributes.operation = source.operation;
    result_attributes.comparison = source.comparison;
    result_attributes.comparisons = std::move(source.comparisons);
    if (semantic_facts != nullptr) {
      result_attributes.logical_evaluation = semantic_facts->logical_evaluation;
      result_attributes.array_operation = semantic_facts->array_operation;
      if (semantic_facts->broadcast.valid) {
        result_attributes.broadcast.valid = true;
        result_attributes.broadcast.shape_source = semantic_facts->broadcast.shape_source;
        result_attributes.broadcast.left_shape =
            intern_shape(semantic_facts->broadcast.left_shape, false);
        result_attributes.broadcast.right_shape =
            intern_shape(semantic_facts->broadcast.right_shape, false);
        result_attributes.broadcast.result_shape =
            intern_shape(semantic_facts->broadcast.result_shape, false);
        result_attributes.broadcast.axes = semantic_facts->broadcast.axes;
      }
      if (semantic_facts->sparse_arithmetic.valid()) {
        result_attributes.sparse_arithmetic.operation =
            semantic_facts->sparse_arithmetic.operation;
        result_attributes.sparse_arithmetic.storage_policy =
            semantic_facts->sparse_arithmetic.storage_policy;
        result_attributes.sparse_arithmetic.shape_source =
            semantic_facts->sparse_arithmetic.shape_source;
        result_attributes.sparse_arithmetic.left_storage =
            semantic_facts->sparse_arithmetic.left_storage;
        result_attributes.sparse_arithmetic.right_storage =
            semantic_facts->sparse_arithmetic.right_storage;
        result_attributes.sparse_arithmetic.result_storage =
            semantic_facts->sparse_arithmetic.result_storage;
        if (!semantic_facts->sparse_arithmetic.left_shape.empty()) {
          result_attributes.sparse_arithmetic.left_shape =
              intern_shape(semantic_facts->sparse_arithmetic.left_shape, false);
        }
        if (!semantic_facts->sparse_arithmetic.right_shape.empty()) {
          result_attributes.sparse_arithmetic.right_shape =
              intern_shape(semantic_facts->sparse_arithmetic.right_shape, false);
        }
        result_attributes.sparse_arithmetic.result_shape =
            intern_shape(semantic_facts->sparse_arithmetic.result_shape, false);
        result_attributes.sparse_arithmetic.axes = semantic_facts->sparse_arithmetic.axes;
      }
      if (semantic_facts->sparse_elementwise.valid()) {
        result_attributes.sparse_elementwise.operation =
            semantic_facts->sparse_elementwise.operation;
        result_attributes.sparse_elementwise.storage_policy =
            semantic_facts->sparse_elementwise.storage_policy;
        result_attributes.sparse_elementwise.shape_source =
            semantic_facts->sparse_elementwise.shape_source;
        result_attributes.sparse_elementwise.left_storage =
            semantic_facts->sparse_elementwise.left_storage;
        result_attributes.sparse_elementwise.right_storage =
            semantic_facts->sparse_elementwise.right_storage;
        result_attributes.sparse_elementwise.result_storage =
            semantic_facts->sparse_elementwise.result_storage;
        if (!semantic_facts->sparse_elementwise.left_shape.empty()) {
          result_attributes.sparse_elementwise.left_shape =
              intern_shape(semantic_facts->sparse_elementwise.left_shape, false);
        }
        if (!semantic_facts->sparse_elementwise.right_shape.empty()) {
          result_attributes.sparse_elementwise.right_shape =
              intern_shape(semantic_facts->sparse_elementwise.right_shape, false);
        }
        result_attributes.sparse_elementwise.result_shape =
            intern_shape(semantic_facts->sparse_elementwise.result_shape, false);
        result_attributes.sparse_elementwise.axes = semantic_facts->sparse_elementwise.axes;
      }
      if (semantic_facts->sparse_logical.valid()) {
        result_attributes.sparse_logical.operation = semantic_facts->sparse_logical.operation;
        result_attributes.sparse_logical.storage_policy =
            semantic_facts->sparse_logical.storage_policy;
        result_attributes.sparse_logical.shape_source = semantic_facts->sparse_logical.shape_source;
        result_attributes.sparse_logical.left_storage = semantic_facts->sparse_logical.left_storage;
        result_attributes.sparse_logical.right_storage =
            semantic_facts->sparse_logical.right_storage;
        result_attributes.sparse_logical.result_storage =
            semantic_facts->sparse_logical.result_storage;
        if (!semantic_facts->sparse_logical.left_shape.empty()) {
          result_attributes.sparse_logical.left_shape =
              intern_shape(semantic_facts->sparse_logical.left_shape, false);
        }
        if (!semantic_facts->sparse_logical.right_shape.empty()) {
          result_attributes.sparse_logical.right_shape =
              intern_shape(semantic_facts->sparse_logical.right_shape, false);
        }
        result_attributes.sparse_logical.result_shape =
            intern_shape(semantic_facts->sparse_logical.result_shape, false);
        result_attributes.sparse_logical.axes = semantic_facts->sparse_logical.axes;
      }
      if (semantic_facts->matrix_operation.valid()) {
        result_attributes.matrix_operation.operation = semantic_facts->matrix_operation.operation;
        result_attributes.matrix_operation.solve = semantic_facts->matrix_operation.solve;
        result_attributes.matrix_operation.numeric_domain =
            semantic_facts->matrix_operation.numeric_domain;
        result_attributes.matrix_operation.condition_policy =
            semantic_facts->matrix_operation.condition_policy;
        result_attributes.matrix_operation.factorization_policy =
            semantic_facts->matrix_operation.factorization_policy;
        result_attributes.matrix_operation.structure_policy =
            semantic_facts->matrix_operation.structure_policy;
        result_attributes.matrix_operation.storage_policy =
            semantic_facts->matrix_operation.storage_policy;
        result_attributes.matrix_operation.left_storage =
            semantic_facts->matrix_operation.left_storage;
        result_attributes.matrix_operation.right_storage =
            semantic_facts->matrix_operation.right_storage;
        result_attributes.matrix_operation.result_storage =
            semantic_facts->matrix_operation.result_storage;
        if (!semantic_facts->matrix_operation.left_shape.empty()) {
          result_attributes.matrix_operation.left_shape =
              intern_shape(semantic_facts->matrix_operation.left_shape, false);
        }
        if (!semantic_facts->matrix_operation.right_shape.empty()) {
          result_attributes.matrix_operation.right_shape =
              intern_shape(semantic_facts->matrix_operation.right_shape, false);
        }
        result_attributes.matrix_operation.result_shape =
            intern_shape(semantic_facts->matrix_operation.result_shape, false);
      }
      if (semantic_facts->reduction.valid()) {
        result_attributes.reduction.operation = semantic_facts->reduction.operation;
        result_attributes.reduction.axis_policy = semantic_facts->reduction.axis_policy;
        result_attributes.reduction.shape_source = semantic_facts->reduction.shape_source;
        result_attributes.reduction.input_shape =
            intern_shape(semantic_facts->reduction.input_shape, false);
        result_attributes.reduction.result_shape =
            intern_shape(semantic_facts->reduction.result_shape, false);
        result_attributes.reduction.output_shape =
            intern_shape(semantic_facts->reduction.output_shape, false);
        result_attributes.reduction.axes = semantic_facts->reduction.axes;
        result_attributes.reduction.scalar_result = semantic_facts->reduction.scalar_result;
        result_attributes.reduction.storage_policy = semantic_facts->reduction.storage_policy;
        result_attributes.reduction.input_storage = semantic_facts->reduction.input_storage;
        result_attributes.reduction.result_storage = semantic_facts->reduction.result_storage;
      }
      if (semantic_facts->sparse_construction.valid()) {
        result_attributes.sparse_construction.kind = semantic_facts->sparse_construction.kind;
        result_attributes.sparse_construction.result_shape =
            intern_shape(semantic_facts->sparse_construction.result_shape, false);
        result_attributes.sparse_construction.triplet_element_counts =
            semantic_facts->sparse_construction.triplet_element_counts;
        result_attributes.sparse_construction.reserve_hint =
            semantic_facts->sparse_construction.reserve_hint;
        result_attributes.sparse_construction.value_domain =
            semantic_facts->sparse_construction.value_domain;
        result_attributes.sparse_construction.duplicate_policy =
            semantic_facts->sparse_construction.duplicate_policy;
      }
      if (semantic_facts->sparse_index.valid()) {
        result_attributes.sparse_index.kind = semantic_facts->sparse_index.kind;
        result_attributes.sparse_index.source_storage = semantic_facts->sparse_index.source_storage;
        result_attributes.sparse_index.result_storage = semantic_facts->sparse_index.result_storage;
      }
      if (semantic_facts->sparse_reshape.valid()) {
        result_attributes.sparse_reshape.kind = semantic_facts->sparse_reshape.kind;
        result_attributes.sparse_reshape.dimension_form =
            semantic_facts->sparse_reshape.dimension_form;
        result_attributes.sparse_reshape.inference = semantic_facts->sparse_reshape.inference;
        result_attributes.sparse_reshape.inferred_axis =
            semantic_facts->sparse_reshape.inferred_axis;
        result_attributes.sparse_reshape.source_storage =
            semantic_facts->sparse_reshape.source_storage;
        result_attributes.sparse_reshape.result_storage =
            semantic_facts->sparse_reshape.result_storage;
        result_attributes.sparse_reshape.requested_shape =
            intern_shape(semantic_facts->sparse_reshape.requested_shape, false);
      }
      result_attributes.binding = semantic_facts->binding;
      result_attributes.intrinsic = semantic_facts->intrinsic;
      result_attributes.tuple_shapes.reserve(semantic_facts->tuple_shapes.size());
      for (const auto& tuple_shape : semantic_facts->tuple_shapes) {
        result_attributes.tuple_shapes.push_back(intern_shape(tuple_shape, false));
      }
      result_attributes.sequence_elements.reserve(semantic_facts->sequence_elements.size());
      for (const auto& element : semantic_facts->sequence_elements) {
        result_attributes.sequence_elements.push_back(intern_value_metadata(element));
      }
      result_attributes.requested_results = semantic_facts->requested_outputs;
      result_attributes.multi_result_call = semantic_facts->multi_output_call;
      result_attributes.procedure_has_result = semantic_facts->procedure_has_result;
      result_attributes.index_base = semantic_facts->index_base;
      result_attributes.allow_negative_index = semantic_facts->allow_negative_index;
      result_attributes.slice_stop_inclusive = semantic_facts->slice_stop_inclusive;
      result_attributes.index_extent = semantic_facts->index_extent;
      result_attributes.index_selectors = semantic_facts->index_selectors;
      result_attributes.index_extents = semantic_facts->index_extents;
      result_attributes.storage_region = semantic_facts->storage_region;
    }
    result.children.reserve(source.children.size());
    std::vector<ValueId> operands;
    operands.reserve(source.children.size());
    struct LazyEdge {
      BlockId block{};
      StorageVersions versions;
      ValueId value{};
    };
    std::vector<LazyEdge> lazy_edges;
    BlockId lazy_merge;
    StorageVersions lazy_fallback;
    const auto lower_child = [&](hir::Expression&& child) {
      const auto lowered = lower_expression(std::move(child));
      result.children.push_back(lowered);
      return mir::expression(program_, lowered);
    };
    const bool lazy_conditional =
        result.kind == ExpressionKind::conditional && source.children.size() == 3U;
    const bool lazy_logical = result.kind == ExpressionKind::binary &&
                              source.children.size() == 2U &&
                              semantic::short_circuits(result_attributes.logical_evaluation);
    const bool lazy_comparison =
        result.kind == ExpressionKind::comparison_chain && source.children.size() >= 3U &&
        result_attributes.comparisons.size() + 1U == source.children.size();
    if (lazy_conditional) {
      result_attributes.lazy_cfg = true;
      const auto* condition = lower_child(std::move(source.children[0]));
      const auto condition_value = emit_truthiness(*condition, result.origin);
      lazy_fallback = storage_values_;
      const auto true_block = make_function_block();
      const auto false_block = make_function_block();
      lazy_merge = make_function_block();
      set_conditional(condition_value, true_block, false_block, result.origin);

      current_block_ = true_block;
      storage_values_ = lazy_fallback;
      const auto* when_true = lower_child(std::move(source.children[1]));
      const auto true_exit = current_block_;
      const auto true_versions = storage_values_;
      set_branch(lazy_merge, result.origin);
      lazy_edges.push_back({true_exit, true_versions, when_true->value_id});

      current_block_ = false_block;
      storage_values_ = lazy_fallback;
      const auto* when_false = lower_child(std::move(source.children[2]));
      const auto false_exit = current_block_;
      const auto false_versions = storage_values_;
      set_branch(lazy_merge, result.origin);
      lazy_edges.push_back({false_exit, false_versions, when_false->value_id});
    } else if (lazy_logical) {
      result_attributes.lazy_cfg = true;
      const auto* left = lower_child(std::move(source.children[0]));
      const auto logical_truthiness =
          program_.source_language == SourceLanguage::matlab &&
                  result_attributes.logical_evaluation ==
                      semantic::LogicalEvaluation::short_circuit_boolean
              ? std::optional<semantic::Truthiness>{semantic::Truthiness::matlab_scalar}
              : std::nullopt;
      const auto condition_value = emit_truthiness(*left, result.origin, logical_truthiness);
      lazy_fallback = storage_values_;
      const auto right_block = make_function_block();
      const auto bypass_block = make_function_block();
      lazy_merge = make_function_block();
      const bool logical_and =
          result_attributes.operation == BinaryOperator::logical_and ||
          result_attributes.operation == BinaryOperator::elementwise_logical_and;
      set_conditional(condition_value, logical_and ? right_block : bypass_block,
                      logical_and ? bypass_block : right_block, result.origin);

      current_block_ = bypass_block;
      storage_values_ = lazy_fallback;
      set_branch(lazy_merge, result.origin);
      lazy_edges.push_back({bypass_block, storage_values_,
                            result_attributes.logical_evaluation ==
                                    semantic::LogicalEvaluation::short_circuit_operand
                                ? left->value_id
                                : condition_value});

      current_block_ = right_block;
      storage_values_ = lazy_fallback;
      const auto* right = lower_child(std::move(source.children[1]));
      const auto right_value =
          result_attributes.logical_evaluation == semantic::LogicalEvaluation::short_circuit_operand
              ? right->value_id
              : emit_truthiness(*right, result.origin, logical_truthiness);
      const auto right_exit = current_block_;
      const auto right_versions = storage_values_;
      set_branch(lazy_merge, result.origin);
      lazy_edges.push_back({right_exit, right_versions, right_value});
    } else if (lazy_comparison) {
      result_attributes.lazy_cfg = true;
      const auto* first = lower_child(std::move(source.children[0]));
      auto left_id = first->id;
      lazy_fallback = storage_values_;
      lazy_merge = make_function_block();
      for (std::size_t index = 0; index < result_attributes.comparisons.size(); ++index) {
        const auto* right = lower_child(std::move(source.children[index + 1U]));
        const auto* left = mir::expression(program_, left_id);
        const auto compared = emit_comparison(*left, *right, result_attributes.comparisons[index],
                                              result.origin, result.location);
        const auto comparison_exit = current_block_;
        const auto comparison_versions = storage_values_;
        if (index + 1U == result_attributes.comparisons.size()) {
          set_branch(lazy_merge, result.origin);
          lazy_edges.push_back({comparison_exit, comparison_versions, compared});
        } else {
          const auto continuation = make_function_block();
          set_conditional(compared, continuation, lazy_merge, result.origin);
          lazy_edges.push_back({comparison_exit, comparison_versions, compared});
          current_block_ = continuation;
          storage_values_ = comparison_versions;
        }
        left_id = right->id;
      }
    } else {
      for (auto& child : source.children) {
        const auto* node = lower_child(std::move(child));
        if (node != nullptr && node->value_id.valid()) operands.push_back(node->value_id);
      }
    }
    if (result.kind == ExpressionKind::identifier) {
      const auto* use = names_.reference(result.origin);
      result.symbol_id = use == nullptr ? SymbolId{} : use->symbol;
    }

    result.type_id = intern_expression_type(semantic_facts, result.children);
    result.shape_id =
        intern_shape(semantic_facts == nullptr ? std::vector<std::size_t>{} : semantic_facts->shape,
                     semantic_facts != nullptr && semantic_facts->column_major);
    if (result_attributes.sparse_index.valid()) {
      const auto* source_expression =
          result.children.empty() ? nullptr : mir::expression(program_, result.children.front());
      result_attributes.sparse_index.input_shape =
          source_expression == nullptr ? ShapeId{} : source_expression->shape_id;
      result_attributes.sparse_index.result_shape = result.shape_id;
    }
    if (result_attributes.sparse_reshape.valid()) {
      const auto* source_expression =
          result.children.size() < 2U ? nullptr : mir::expression(program_, result.children[1]);
      result_attributes.sparse_reshape.input_shape =
          source_expression == nullptr ? ShapeId{} : source_expression->shape_id;
      result_attributes.sparse_reshape.result_shape = result.shape_id;
    }
    if (result_attributes.lazy_cfg) {
      std::vector<ControlEdge> control_edges;
      control_edges.reserve(lazy_edges.size());
      for (const auto& edge : lazy_edges) control_edges.push_back({edge.block, edge.versions});
      storage_values_ = merge_storage_versions(lazy_merge, control_edges, lazy_fallback);
      current_block_ = lazy_merge;
      const auto merged_value = value_ids_.next();
      program_.blocks[lazy_merge.value()].arguments.push_back(
          {merged_value, result.type_id, result.shape_id, {}});
      for (const auto& edge : lazy_edges) {
        append_edge_argument(edge.block, lazy_merge, edge.value);
      }
      operands.push_back(merged_value);
    }
    if (result.kind == ExpressionKind::identifier &&
        result_attributes.binding == BindingKind::variable) {
      result.storage_id = storage_for(result.symbol_id, result_attributes.spelling, result.origin,
                                      result.type_id, result.shape_id);
    } else if ((result.kind == ExpressionKind::index || result.kind == ExpressionKind::slice) &&
               !result.children.empty()) {
      const auto* base = mir::expression(program_, result.children.front());
      if (base != nullptr && base->storage_id.valid()) {
        const auto section = std::any_of(
            result.children.begin(), result.children.end(),
            [&](const MirExpressionId child) { return contains_slice(program_, child); });
        result.storage_id = make_view_storage(base->storage_id, result.type_id, result.shape_id,
                                              result.origin, section);
      }
    }
    result.value_id = value_ids_.next();
    Instruction instruction;
    instruction.opcode = expression_opcode(result.kind, result_attributes.binding);
    instruction.origin = result.origin;
    const auto* callee =
        result.children.empty() ? nullptr : mir::expression(program_, result.children.front());
    const auto* callee_attributes =
        result.children.empty() ? nullptr : mir::attributes(program_, result.children.front());
    if (result.kind == ExpressionKind::call && callee_attributes != nullptr &&
        callee_attributes->binding == BindingKind::builtin) {
      instruction.intrinsic = callee_attributes->intrinsic;
    }
    instruction.location = result.location;
    instruction.result = result.value_id;
    instruction.type = result.type_id;
    instruction.shape = result.shape_id;
    instruction.storage = result.storage_id;
    std::vector<MemoryAccess> instruction_accesses;
    if ((instruction.opcode == Opcode::load || instruction.opcode == Opcode::index ||
         instruction.opcode == Opcode::slice) &&
        instruction.storage.valid()) {
      instruction_accesses.push_back(make_memory_access(
          instruction.storage, result_attributes.storage_region, MemoryAccessMode::read));
    }
    struct PendingWriteback {
      StorageId target{};
      ValueId temporary{};
      TypeId type{};
      ShapeId shape{};
      ArgumentTransfer transfer{ArgumentTransfer::value};
      StorageRegion region;
    };
    std::optional<UnresolvedCall> unresolved_call;
    std::vector<PendingWriteback> pending_writebacks;
    if (result.kind == ExpressionKind::call && callee != nullptr && callee_attributes != nullptr &&
        callee_attributes->binding == BindingKind::function) {
      UnresolvedCall call;
      call.origin = result.origin;
      call.caller = current_function_;
      call.callee_symbol = callee->symbol_id;
      call.result_type = result.type_id;
      call.requested_results = program_.source_language == SourceLanguage::matlab
                                   ? result_attributes.requested_results
                                   : 1U;
      call.arguments.reserve(result.children.size() - 1U);
      for (std::size_t index = 1; index < result.children.size(); ++index) {
        const auto intent_index = index - 1U;
        const auto intent =
            semantic_facts != nullptr && intent_index < semantic_facts->argument_intents.size()
                ? semantic_facts->argument_intents[intent_index]
                : ParameterIntent::none;
        const auto optional_forward =
            semantic_facts != nullptr &&
            intent_index < semantic_facts->argument_optional_forward.size() &&
            semantic_facts->argument_optional_forward[intent_index];
        const auto* argument = mir::expression(program_, result.children[index]);
        if (argument != nullptr) {
          auto contract = make_call_argument(*argument, intent, optional_forward);
          if (argument_transfer_copies(contract.transfer)) {
            Instruction copy;
            copy.id = instruction_ids_.next();
            copy.opcode = Opcode::copy;
            copy.origin = result.origin;
            copy.location = argument->location;
            copy.result = value_ids_.next();
            copy.type = argument->type_id;
            copy.shape = argument->shape_id;
            copy.storage = make_temporary_storage(copy.type, copy.shape, result.origin);
            copy.transfer = contract.transfer;
            if (contract.transfer == ArgumentTransfer::copy_in_out) {
              copy.operands.push_back(argument->value_id);
            }
            if (index < operands.size()) operands[index] = copy.result;
            pending_writebacks.push_back({contract.storage, copy.result, copy.type, copy.shape,
                                          contract.transfer, contract.region});
            std::vector<MemoryAccess> copy_accesses;
            if (contract.transfer == ArgumentTransfer::copy_in_out && contract.storage.valid()) {
              copy_accesses.push_back(
                  make_memory_access(contract.storage, contract.region, MemoryAccessMode::read));
            }
            append_instruction(std::move(copy), std::move(copy_accesses));
          }
          call.arguments.push_back(contract);
        }
      }
      unresolved_call = std::move(call);
    }
    instruction.id = instruction_ids_.next();
    result.instruction = instruction.id;
    instruction.operands = std::move(operands);
    if (unresolved_call.has_value()) unresolved_call->instruction = instruction.id;
    append_instruction(std::move(instruction), std::move(instruction_accesses));
    if (unresolved_call.has_value()) unresolved_calls_.push_back(std::move(*unresolved_call));
    for (const auto& pending : pending_writebacks) {
      Instruction writeback;
      writeback.id = instruction_ids_.next();
      writeback.opcode = Opcode::writeback;
      writeback.origin = result.origin;
      writeback.location = result.location;
      writeback.result = value_ids_.next();
      writeback.type = pending.type;
      writeback.shape = pending.shape;
      writeback.storage = pending.target;
      writeback.transfer = pending.transfer;
      writeback.operands.push_back(pending.temporary);
      storage_values_[pending.target] = writeback.result;
      append_instruction(std::move(writeback), {make_memory_access(pending.target, pending.region,
                                                                   MemoryAccessMode::write)});
    }
    const auto id = result.id;
    program_.expressions[id.value()] = std::move(result);
    program_.attributes.expressions[id.value()] = std::move(result_attributes);
    return id;
  }

  CaseSelector lower_selector(hir::CaseSelector&& source) {
    CaseSelector result;
    result.lower = lower_expression(std::move(source.lower));
    result.has_lower = source.has_lower;
    result.upper = lower_expression(std::move(source.upper));
    result.has_upper = source.has_upper;
    result.range = source.range;
    return result;
  }

  ValueId emit_case_predicate(const hir::Statement& clause, const ValueId selector) {
    Instruction instruction;
    instruction.opcode = Opcode::selection;
    instruction.origin = clause.id;
    instruction.location = {clause.line, 1};
    instruction.type = intern_type(ValueType::boolean, ValueType::unknown, logical_numeric_type);
    instruction.shape = intern_shape({}, false);
    if (selector.valid()) instruction.operands.push_back(selector);
    for (const auto& source_selector : clause.case_selectors) {
      if (source_selector.has_lower) {
        auto copy = source_selector.lower;
        const auto lowered = lower_expression(std::move(copy));
        const auto* node = mir::expression(program_, lowered);
        if (node != nullptr && node->value_id.valid())
          instruction.operands.push_back(node->value_id);
      }
      if (source_selector.has_upper) {
        auto copy = source_selector.upper;
        const auto lowered = lower_expression(std::move(copy));
        const auto* node = mir::expression(program_, lowered);
        if (node != nullptr && node->value_id.valid())
          instruction.operands.push_back(node->value_id);
      }
    }
    instruction.id = instruction_ids_.next();
    instruction.result = value_ids_.next();
    const auto result = instruction.result;
    append_instruction(std::move(instruction));
    return result;
  }

  void lower_statement_list(std::vector<hir::Statement>&& source,
                            std::vector<MirStatementId>& result) {
    result.reserve(source.size());
    for (auto& statement : source) {
      result.push_back(lower_statement(std::move(statement)));
    }
  }

  void emit_statement_instruction(Statement& statement, StatementAttributes& attributes,
                                  const hir::StatementFacts* facts) {
    if (statement.kind == StatementKind::multi_assignment && !statement.target_names.empty()) {
      const auto* value = mir::expression(program_, statement.expression);
      for (std::size_t index = 0; index < statement.target_names.size(); ++index) {
        auto& target = attributes.targets[index];
        target.storage = storage_for(
            index < statement.target_symbols.size() ? statement.target_symbols[index] : SymbolId{},
            statement.target_names[index], statement.origin, target.type, target.shape);
        Instruction store;
        store.id = instruction_ids_.next();
        if (index == 0U) statement.instruction = store.id;
        store.opcode = Opcode::store;
        store.origin = statement.origin;
        store.location = {statement.line, 1};
        store.storage = target.storage;
        store.type = target.type;
        store.shape = target.shape;
        store.result_index = index;
        if (value != nullptr && value->value_id.valid()) {
          store.operands.push_back(value->value_id);
          store.result = value_ids_.next();
          storage_values_[store.storage] = store.result;
        }
        append_instruction(std::move(store),
                           {make_memory_access(target.storage, full_region(target.storage),
                                               MemoryAccessMode::write)});
      }
      return;
    }
    Instruction instruction;
    instruction.id = instruction_ids_.next();
    statement.instruction = instruction.id;
    instruction.opcode = statement.kind == StatementKind::for_loop
                             ? Opcode::store
                             : statement_opcode(statement.kind, statement.has_expression);
    instruction.origin = statement.origin;
    instruction.location = {statement.line, 1};
    const auto append_operand = [&](const MirExpressionId expression_id) {
      const auto* node = mir::expression(program_, expression_id);
      if (node != nullptr && node->value_id.valid()) instruction.operands.push_back(node->value_id);
    };
    append_operand(statement.expression);
    append_operand(statement.secondary_expression);
    append_operand(statement.tertiary_expression);
    append_operand(statement.target_expression);
    if ((statement.kind == StatementKind::declaration ||
         statement.kind == StatementKind::assignment ||
         statement.kind == StatementKind::range_loop ||
         statement.kind == StatementKind::for_loop) &&
        !statement.name.empty()) {
      instruction.storage = storage_for(
          statement.symbol_id, statement.name, statement.origin,
          intern_type(facts == nullptr ? ValueType::unknown : facts->declared_type,
                      facts == nullptr ? ValueType::unknown : facts->element_type,
                      facts == nullptr ? unknown_numeric_type : facts->declared_numeric_type,
                      facts == nullptr ? unknown_numeric_type : facts->element_numeric_type,
                      facts == nullptr ? ArrayStorageFormat::none : facts->array_storage),
          intern_shape(facts == nullptr ? std::vector<std::size_t>{} : facts->shape, false));
    } else if (statement.kind == StatementKind::indexed_assignment) {
      const auto* target = mir::expression(program_, statement.target_expression);
      if (target != nullptr) instruction.storage = target->storage_id;
    }
    if (instruction.storage.valid()) {
      const auto& storage = program_.storages[instruction.storage.value()];
      instruction.type = storage.type;
      instruction.shape = storage.shape;
    }
    const auto* value = mir::expression(program_, statement.expression);
    if ((statement.kind == StatementKind::declaration ||
         statement.kind == StatementKind::assignment ||
         statement.kind == StatementKind::indexed_assignment ||
         statement.kind == StatementKind::for_loop) &&
        instruction.storage.valid() && value != nullptr && value->value_id.valid()) {
      instruction.result = value_ids_.next();
      storage_values_[instruction.storage] = instruction.result;
    }
    std::vector<MemoryAccess> memory_accesses;
    const bool writes_storage =
        statement.kind == StatementKind::assignment ||
        statement.kind == StatementKind::indexed_assignment ||
        statement.kind == StatementKind::range_loop || statement.kind == StatementKind::for_loop ||
        (statement.kind == StatementKind::declaration && statement.has_expression);
    if (writes_storage && instruction.storage.valid()) {
      auto region = full_region(instruction.storage);
      if (statement.kind == StatementKind::indexed_assignment) {
        if (!attributes.indexed_mutation.contract.changes_shape()) {
          const auto* target_attributes = mir::attributes(program_, statement.target_expression);
          if (target_attributes != nullptr) region = target_attributes->storage_region;
        }
      }
      memory_accesses.push_back(
          make_memory_access(instruction.storage, std::move(region), MemoryAccessMode::write));
    }
    append_instruction(std::move(instruction), std::move(memory_accesses));
  }

  StorageId storage_root(StorageId storage) const noexcept {
    for (std::size_t depth = 0; depth < program_.storages.size(); ++depth) {
      if (!storage.valid() || storage.value() >= program_.storages.size()) return {};
      const auto& metadata = program_.storages[storage.value()];
      if (metadata.kind != StorageKind::view) return storage;
      storage = metadata.base;
    }
    return {};
  }

  ArgumentTransfer argument_transfer(const ParameterIntent intent, const StorageId storage,
                                     const bool optional_forward,
                                     const bool omitted) const noexcept {
    if (omitted) return ArgumentTransfer::omitted;
    if (optional_forward) {
      if (intent == ParameterIntent::out) return ArgumentTransfer::optional_forward_out;
      if (intent == ParameterIntent::inout) return ArgumentTransfer::optional_forward_inout;
      return ArgumentTransfer::optional_forward_in;
    }
    if (intent == ParameterIntent::none) return ArgumentTransfer::value;
    if (intent == ParameterIntent::in) {
      return storage.valid() ? ArgumentTransfer::read_only_borrow : ArgumentTransfer::value;
    }
    const auto section = storage.valid() && storage.value() < program_.storages.size() &&
                         program_.storages[storage.value()].view == StorageViewKind::section;
    if (intent == ParameterIntent::out) {
      return section ? ArgumentTransfer::copy_out : ArgumentTransfer::mutable_borrow_out;
    }
    return section ? ArgumentTransfer::copy_in_out : ArgumentTransfer::mutable_borrow_inout;
  }

  CallSite::Argument make_call_argument(const Expression& expression, const ParameterIntent intent,
                                        const bool optional_forward) const {
    CallSite::Argument result;
    result.type = expression.type_id;
    result.storage = expression.storage_id;
    result.root = storage_root(result.storage);
    result.intent = intent;
    result.transfer = argument_transfer(intent, result.storage, optional_forward,
                                        expression.kind == ExpressionKind::omitted_argument);
    const auto* expression_attributes = mir::attributes(program_, expression.id);
    if (expression_attributes != nullptr) result.region = expression_attributes->storage_region;
    if (result.storage.valid() && result.storage.value() < program_.storages.size()) {
      const auto& metadata = program_.storages[result.storage.value()];
      result.view = metadata.view;
      result.lifetime = metadata.lifetime;
      result.writable = metadata.writable;
    }
    return result;
  }

  StorageId storage_for(const SymbolId symbol, const std::string& name, const HirNodeId origin,
                        const TypeId type, const ShapeId shape,
                        const StorageKind kind = StorageKind::local,
                        const ParameterIntent intent = ParameterIntent::none,
                        const bool optional = false) {
    auto storage_kind = kind;
    if (kind == StorageKind::local) {
      const auto* name_symbol = names_.symbol(symbol);
      if (name_symbol != nullptr && name_symbol->scope == names_.global_scope) {
        storage_kind = StorageKind::global;
      }
    }
    auto& inventory = storage_kind == StorageKind::global ? global_storages_ : storages_;
    const auto found = inventory.find(symbol);
    if (found != inventory.end()) return found->second;
    const auto id = StorageId{static_cast<StorageId::value_type>(program_.storages.size())};
    const auto parameter = storage_kind == StorageKind::parameter;
    const auto module = storage_kind == StorageKind::global;
    program_.storages.push_back({name,
                                 symbol,
                                 origin,
                                 type,
                                 shape,
                                 intent != ParameterIntent::in,
                                 optional,
                                 storage_kind,
                                 parameter ? StorageLifetime::borrowed
                                 : module  ? StorageLifetime::module
                                           : StorageLifetime::function,
                                 {},
                                 StorageViewKind::none,
                                 intent});
    inventory.emplace(symbol, id);
    return id;
  }

  StorageId make_view_storage(const StorageId base, const TypeId type, const ShapeId shape,
                              const HirNodeId origin, const bool section) {
    const auto id = StorageId{static_cast<StorageId::value_type>(program_.storages.size())};
    program_.storages.push_back(
        {"$view" + std::to_string(id.value()), program_.storages[base.value()].symbol, origin, type,
         shape, program_.storages[base.value()].writable, false, StorageKind::view,
         StorageLifetime::expression, base,
         section ? StorageViewKind::section : StorageViewKind::element, ParameterIntent::none});
    return id;
  }

  StorageId make_temporary_storage(const TypeId type, const ShapeId shape, const HirNodeId origin) {
    const auto id = StorageId{static_cast<StorageId::value_type>(program_.storages.size())};
    program_.storages.push_back({"$temporary" + std::to_string(id.value()),
                                 {},
                                 origin,
                                 type,
                                 shape,
                                 true,
                                 false,
                                 StorageKind::temporary,
                                 StorageLifetime::expression,
                                 {},
                                 StorageViewKind::none,
                                 ParameterIntent::none});
    return id;
  }

  void initialize_function_signature(const Statement& statement, const hir::StatementFacts* facts) {
    auto& function = current_function();
    function.parameter_types.reserve(statement.parameters.size());
    function.parameter_shapes.reserve(statement.parameters.size());
    function.parameter_optional =
        facts == nullptr ? std::vector<bool>{} : facts->parameter_optional;
    function.parameter_optional.resize(statement.parameters.size(), false);
    std::vector<TypeId> signature_parameters;
    signature_parameters.reserve(statement.parameters.size());
    for (std::size_t index = 0; index < statement.parameters.size(); ++index) {
      const auto type = intern_type(
          facts != nullptr && index < facts->parameter_types.size() ? facts->parameter_types[index]
                                                                    : ValueType::unknown,
          facts != nullptr && index < facts->parameter_element_types.size()
              ? facts->parameter_element_types[index]
              : ValueType::unknown,
          facts != nullptr && index < facts->parameter_numeric_types.size()
              ? facts->parameter_numeric_types[index]
              : unknown_numeric_type,
          facts != nullptr && index < facts->parameter_element_numeric_types.size()
              ? facts->parameter_element_numeric_types[index]
              : unknown_numeric_type,
          facts != nullptr && index < facts->parameter_array_storage.size()
              ? facts->parameter_array_storage[index]
              : ArrayStorageFormat::none);
      const auto parameter_shape = facts != nullptr && index < facts->parameter_shapes.size()
                                       ? facts->parameter_shapes[index]
                                       : std::vector<std::size_t>{};
      const auto shape = intern_shape(parameter_shape, false);
      const auto intent = facts != nullptr && index < facts->parameter_intents.size()
                              ? facts->parameter_intents[index]
                              : ParameterIntent::none;
      const auto storage = storage_for(
          index < statement.parameter_symbols.size() ? statement.parameter_symbols[index]
                                                     : SymbolId{},
          statement.parameters[index], statement.origin, type, shape, StorageKind::parameter,
          intent, function.parameter_optional[index]);
      const auto value = value_ids_.next();
      current_block().arguments.push_back({value, type, shape, storage});
      storage_values_[storage] = value;
      function.parameter_types.push_back(type);
      function.parameter_shapes.push_back(shape);
      signature_parameters.push_back(
          intent == ParameterIntent::none ? type : intern_reference_type(type, intent));
    }
    if (facts != nullptr && facts->has_value_return) {
      if (facts->declared_type == ValueType::tuple && !facts->return_types.empty()) {
        std::vector<TypeId> elements;
        elements.reserve(facts->return_types.size());
        for (std::size_t index = 0; index < facts->return_types.size(); ++index) {
          elements.push_back(intern_type(
              facts->return_types[index],
              index < facts->return_element_types.size() ? facts->return_element_types[index]
                                                         : ValueType::unknown,
              index < facts->return_numeric_types.size() ? facts->return_numeric_types[index]
                                                         : unknown_numeric_type,
              index < facts->return_element_numeric_types.size()
                  ? facts->return_element_numeric_types[index]
                  : unknown_numeric_type,
              index < facts->return_array_storage.size() ? facts->return_array_storage[index]
                                                         : ArrayStorageFormat::none));
        }
        function.result_types.push_back(intern_tuple_type(elements));
        function.result_shapes.push_back(intern_shape({}, false));
      } else {
        function.result_types.push_back(
            intern_type(facts->declared_type, facts->element_type, facts->declared_numeric_type,
                        facts->element_numeric_type, facts->array_storage));
        function.result_shapes.push_back(intern_shape(facts->shape, false));
      }
    } else if (facts != nullptr) {
      for (std::size_t index = 0; index < facts->return_types.size(); ++index) {
        function.result_types.push_back(intern_type(
            facts->return_types[index],
            index < facts->return_element_types.size() ? facts->return_element_types[index]
                                                       : ValueType::unknown,
            index < facts->return_numeric_types.size() ? facts->return_numeric_types[index]
                                                       : unknown_numeric_type,
            index < facts->return_element_numeric_types.size()
                ? facts->return_element_numeric_types[index]
                : unknown_numeric_type,
            index < facts->return_array_storage.size() ? facts->return_array_storage[index]
                                                       : ArrayStorageFormat::none));
        function.result_shapes.push_back(intern_shape(index < facts->return_shapes.size()
                                                          ? facts->return_shapes[index]
                                                          : std::vector<std::size_t>{},
                                                      false));
      }
    }
    function.signature = intern_function_type(signature_parameters, function.result_types);
  }

  StorageRegion full_region(const StorageId storage) const {
    const auto root = storage_root(storage);
    if (!root.valid() || root.value() >= program_.storages.size()) return {};
    const auto shape_id = program_.storages[root.value()].shape;
    const auto* shape_data = mir::shape(program_, shape_id);
    return shape_data == nullptr ? StorageRegion{} : full_storage_region(shape_data->extents);
  }

  MemoryAccess make_memory_access(const StorageId storage, StorageRegion region,
                                  const MemoryAccessMode mode) const {
    return {storage, storage_root(storage), std::move(region), mode};
  }

  void append_instruction(Instruction instruction, std::vector<MemoryAccess> memory_accesses = {}) {
    ensure_open_block();
    current_block().instructions.push_back(instruction.id);
    if (program_.attributes.instructions.size() <= instruction.id.value()) {
      program_.attributes.instructions.resize(static_cast<std::size_t>(instruction.id.value()) +
                                              1U);
    }
    program_.attributes.instructions[instruction.id.value()] = {instruction.id,
                                                                std::move(memory_accesses)};
    program_.instructions.push_back(std::move(instruction));
  }

  BlockId make_block() {
    const auto id = block_ids_.next();
    BasicBlock block;
    block.id = id;
    program_.blocks.push_back(std::move(block));
    return id;
  }

  BlockId make_function_block() {
    const auto id = make_block();
    current_function().blocks.push_back(id);
    return id;
  }

  void ensure_open_block() {
    if (!current_block_.valid()) return;
    if (current_block().terminator.kind == TerminatorKind::none) return;
    current_block_ = make_function_block();
  }

  void set_branch(const BlockId target, const HirNodeId origin) {
    auto& terminator = current_block().terminator;
    terminator.kind = TerminatorKind::branch;
    terminator.origin = origin;
    terminator.successors = {target};
    terminator.successor_arguments = {{}};
  }

  void set_conditional(const ValueId condition, const BlockId true_block, const BlockId false_block,
                       const HirNodeId origin) {
    auto& terminator = current_block().terminator;
    terminator.kind = TerminatorKind::conditional_branch;
    terminator.origin = origin;
    if (condition.valid()) terminator.operands = {condition};
    terminator.successors = {true_block, false_block};
    terminator.successor_arguments = {{}, {}};
  }

  void set_edge_arguments(const BlockId source, const BlockId target,
                          std::vector<ValueId> arguments) {
    auto& terminator = program_.blocks[source.value()].terminator;
    const auto found =
        std::find(terminator.successors.begin(), terminator.successors.end(), target);
    if (found == terminator.successors.end()) return;
    const auto index = static_cast<std::size_t>(found - terminator.successors.begin());
    if (terminator.successor_arguments.size() < terminator.successors.size()) {
      terminator.successor_arguments.resize(terminator.successors.size());
    }
    terminator.successor_arguments[index] = std::move(arguments);
  }

  StorageVersions merge_storage_versions(const BlockId target,
                                         const std::vector<ControlEdge>& incoming,
                                         const StorageVersions& fallback) {
    StorageVersions merged = fallback;
    if (incoming.empty()) return merged;
    std::vector<StorageId> storages;
    for (const auto& edge : incoming) {
      for (const auto& entry : edge.versions) storages.push_back(entry.first);
    }
    for (const auto& entry : fallback) storages.push_back(entry.first);
    std::sort(storages.begin(), storages.end());
    storages.erase(std::unique(storages.begin(), storages.end()), storages.end());

    std::vector<std::vector<ValueId>> edge_arguments(incoming.size());
    for (const auto storage : storages) {
      std::vector<ValueId> values;
      values.reserve(incoming.size());
      bool complete = true;
      for (const auto& edge : incoming) {
        const auto found = edge.versions.find(storage);
        const auto before = fallback.find(storage);
        const auto value = found != edge.versions.end() ? found->second
                           : before != fallback.end()   ? before->second
                                                        : ValueId{};
        complete = complete && value.valid();
        values.push_back(value);
      }
      if (!complete) continue;
      const bool different =
          std::any_of(values.begin() + 1, values.end(),
                      [&](const ValueId value) { return value != values.front(); });
      if (!different && incoming.size() > 1) {
        merged[storage] = values.front();
        continue;
      }
      if (!storage.valid() ||
          static_cast<std::size_t>(storage.value()) >= program_.storages.size()) {
        continue;
      }
      const auto& metadata = program_.storages[storage.value()];
      const auto argument = value_ids_.next();
      program_.blocks[target.value()].arguments.push_back(
          {argument, metadata.type, metadata.shape, storage});
      for (std::size_t edge = 0; edge < incoming.size(); ++edge) {
        edge_arguments[edge].push_back(values[edge]);
      }
      merged[storage] = argument;
    }
    for (std::size_t edge = 0; edge < incoming.size(); ++edge) {
      set_edge_arguments(incoming[edge].block, target, std::move(edge_arguments[edge]));
    }
    return merged;
  }

  BasicBlock& current_block() {
    return program_.blocks[static_cast<std::size_t>(current_block_.value())];
  }

  Function& current_function() {
    return program_.functions[static_cast<std::size_t>(current_function_.value())];
  }

  Program& program_;
  const hir::SemanticTable& semantics_;
  const NameTable& names_;
  IrIdAllocator<MirFunctionId> function_ids_;
  IrIdAllocator<MirExpressionId> expression_ids_;
  IrIdAllocator<MirStatementId> statement_ids_;
  IrIdAllocator<BlockId> block_ids_;
  IrIdAllocator<InstructionId> instruction_ids_;
  IrIdAllocator<ValueId> value_ids_;
  MirFunctionId current_function_{};
  BlockId current_block_{};
  std::unordered_map<std::uint64_t, TypeId> types_;
  std::unordered_map<std::string, TypeId> composite_types_;
  std::unordered_map<std::string, ShapeId> shapes_;
  std::unordered_map<SymbolId, StorageId> storages_;
  std::unordered_map<SymbolId, StorageId> global_storages_;
  StorageVersions storage_values_;
  std::vector<LoopContext> loops_;
  std::vector<UnresolvedCall> unresolved_calls_;
};

}  // namespace

const Expression* expression(const Program& program, const MirExpressionId id) noexcept {
  return id.valid() && static_cast<std::size_t>(id.value()) < program.expressions.size()
             ? &program.expressions[id.value()]
             : nullptr;
}

Expression* expression(Program& program, const MirExpressionId id) noexcept {
  return id.valid() && static_cast<std::size_t>(id.value()) < program.expressions.size()
             ? &program.expressions[id.value()]
             : nullptr;
}

const Statement* statement(const Program& program, const MirStatementId id) noexcept {
  return id.valid() && static_cast<std::size_t>(id.value()) < program.statements.size()
             ? &program.statements[id.value()]
             : nullptr;
}

Statement* statement(Program& program, const MirStatementId id) noexcept {
  return id.valid() && static_cast<std::size_t>(id.value()) < program.statements.size()
             ? &program.statements[id.value()]
             : nullptr;
}

const ExpressionAttributes* attributes(const Program& program, const MirExpressionId id) noexcept {
  return id.valid() && static_cast<std::size_t>(id.value()) < program.attributes.expressions.size()
             ? &program.attributes.expressions[id.value()]
             : nullptr;
}

ExpressionAttributes* attributes(Program& program, const MirExpressionId id) noexcept {
  return id.valid() && static_cast<std::size_t>(id.value()) < program.attributes.expressions.size()
             ? &program.attributes.expressions[id.value()]
             : nullptr;
}

const StatementAttributes* attributes(const Program& program, const MirStatementId id) noexcept {
  return id.valid() && static_cast<std::size_t>(id.value()) < program.attributes.statements.size()
             ? &program.attributes.statements[id.value()]
             : nullptr;
}

StatementAttributes* attributes(Program& program, const MirStatementId id) noexcept {
  return id.valid() && static_cast<std::size_t>(id.value()) < program.attributes.statements.size()
             ? &program.attributes.statements[id.value()]
             : nullptr;
}

const InstructionAttributes* attributes(const Program& program, const InstructionId id) noexcept {
  return id.valid() && static_cast<std::size_t>(id.value()) < program.attributes.instructions.size()
             ? &program.attributes.instructions[id.value()]
             : nullptr;
}

InstructionAttributes* attributes(Program& program, const InstructionId id) noexcept {
  return id.valid() && static_cast<std::size_t>(id.value()) < program.attributes.instructions.size()
             ? &program.attributes.instructions[id.value()]
             : nullptr;
}

const TypeData* type(const Program& program, const TypeId id) noexcept {
  return id.valid() && static_cast<std::size_t>(id.value()) < program.types.size()
             ? &program.types[id.value()]
             : nullptr;
}

const ShapeData* shape(const Program& program, const ShapeId id) noexcept {
  return id.valid() && static_cast<std::size_t>(id.value()) < program.shapes.size()
             ? &program.shapes[id.value()]
             : nullptr;
}

ValueType value_type(const Program& program, const TypeId id) noexcept {
  const auto* data = type(program, id);
  if (data != nullptr && data->kind == TypeKind::reference) data = type(program, data->referent);
  return data == nullptr ? ValueType::unknown : data->value_type;
}

ValueType element_type(const Program& program, const TypeId id) noexcept {
  const auto* data = type(program, id);
  if (data != nullptr && data->kind == TypeKind::reference) data = type(program, data->referent);
  return data == nullptr ? ValueType::unknown : data->element_type;
}

NumericType numeric_type(const Program& program, const TypeId id) noexcept {
  const auto* data = type(program, id);
  if (data != nullptr && data->kind == TypeKind::reference) data = type(program, data->referent);
  return data == nullptr ? unknown_numeric_type : data->numeric_type;
}

NumericType element_numeric_type(const Program& program, const TypeId id) noexcept {
  const auto* data = type(program, id);
  if (data != nullptr && data->kind == TypeKind::reference) data = type(program, data->referent);
  return data == nullptr ? unknown_numeric_type : data->element_numeric_type;
}

ArrayStorageFormat array_storage(const Program& program, const TypeId id) noexcept {
  const auto* data = type(program, id);
  if (data != nullptr && data->kind == TypeKind::reference) data = type(program, data->referent);
  return data == nullptr ? ArrayStorageFormat::none : data->array_storage;
}

bool column_major(const Program& program, const ShapeId id) noexcept {
  const auto* data = shape(program, id);
  return data != nullptr && data->layout == semantic::IndexLayout::column_major;
}

LoweringResult lower_from_hir(hir::Program&& source, hir::SemanticTable&& semantics,
                              const NameTable& names) {
  LoweringResult result;
  result.program.source_language = source.language;
  result.program.semantics = source.semantics;
  result.program.hir_node_count = source.node_count;
  result.program.expressions.push_back({});
  result.program.statements.push_back({});
  result.program.attributes.expressions.push_back({});
  result.program.attributes.statements.push_back({});
  result.program.attributes.instructions.push_back({});
  result.program.types.push_back({});
  result.program.shapes.push_back({});
  result.program.storages.push_back({});
  result.program.instructions.push_back({});
  result.program.blocks.push_back({});
  result.program.functions.push_back({});

  result.diagnostics = hir::verify_semantics(source, semantics, "hir-to-mir");
  auto name_diagnostics = verify_names(source, names, "hir-to-mir");
  result.diagnostics.insert(result.diagnostics.end(),
                            std::make_move_iterator(name_diagnostics.begin()),
                            std::make_move_iterator(name_diagnostics.end()));
  if (!result.diagnostics.empty()) return result;

  Builder builder(result.program, semantics, names);
  builder.begin_function("<module>", {});
  result.program.roots.reserve(source.statements.size());
  for (auto& statement : source.statements) {
    if (statement.kind == StatementKind::function) continue;
    result.program.roots.push_back(builder.lower_statement(std::move(statement)));
  }
  builder.finish_function();

  for (auto& statement : source.statements) {
    if (statement.kind != StatementKind::function) continue;
    const auto function_name = statement.name;
    const auto function_origin = statement.id;
    const auto* function_use = names.use(function_origin, NameRole::declaration);
    const auto* facts = semantics.statement(function_origin);
    builder.begin_function(function_name, function_origin,
                           function_use == nullptr ? SymbolId{} : function_use->symbol,
                           facts != nullptr && facts->exported);
    result.program.roots.push_back(builder.lower_statement(std::move(statement)));
    builder.finish_function();
  }
  builder.link_calls();
  result.program.attributes.mir_revision = result.program.revision;
  result.program.attributes.expression_count = result.program.expressions.size() - 1U;
  result.program.attributes.statement_count = result.program.statements.size() - 1U;
  result.program.attributes.instruction_count = result.program.instructions.size() - 1U;
  result.diagnostics = verify(result.program, "hir-to-mir");
  return result;
}

}  // namespace mpf::detail::mir
