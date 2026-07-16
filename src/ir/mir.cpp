#include "mir.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include <unordered_map>
#include <utility>

namespace mpf::detail::mir {
namespace {

std::uint32_t type_key(const ValueType type, const ValueType element_type) noexcept {
  return (static_cast<std::uint32_t>(type) << 16U) | static_cast<std::uint32_t>(element_type);
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

Opcode expression_opcode(const ExpressionKind kind) noexcept {
  switch (kind) {
    case ExpressionKind::number_literal:
    case ExpressionKind::string_literal:
    case ExpressionKind::boolean_literal:
    case ExpressionKind::null_literal:
    case ExpressionKind::omitted_argument: return Opcode::literal;
    case ExpressionKind::identifier: return Opcode::identifier;
    case ExpressionKind::unary: return Opcode::unary;
    case ExpressionKind::binary: return Opcode::binary;
    case ExpressionKind::comparison_chain: return Opcode::comparison_chain;
    case ExpressionKind::conditional: return Opcode::conditional;
    case ExpressionKind::call: return Opcode::call;
    case ExpressionKind::member: return Opcode::member;
    case ExpressionKind::index: return Opcode::index;
    case ExpressionKind::slice: return Opcode::slice;
    case ExpressionKind::list:
    case ExpressionKind::tuple: return Opcode::aggregate;
    case ExpressionKind::invalid: return Opcode::invalid;
  }
  return Opcode::invalid;
}

Opcode statement_opcode(const StatementKind kind) noexcept {
  switch (kind) {
    case StatementKind::declaration:
    case StatementKind::assignment:
    case StatementKind::multi_assignment: return Opcode::assignment;
    case StatementKind::indexed_assignment: return Opcode::indexed_assignment;
    case StatementKind::print: return Opcode::output;
    case StatementKind::return_statement: return Opcode::return_value;
    case StatementKind::expression: return Opcode::expression;
    case StatementKind::if_statement:
    case StatementKind::select_case:
    case StatementKind::case_clause: return Opcode::selection;
    case StatementKind::while_loop:
    case StatementKind::range_loop: return Opcode::loop;
    case StatementKind::function: return Opcode::function;
    case StatementKind::break_statement:
    case StatementKind::continue_statement: return Opcode::control;
  }
  return Opcode::invalid;
}

Effect intrinsic_effects(const IntrinsicId intrinsic) noexcept {
  switch (intrinsic) {
    case IntrinsicId::none: return Effect::external_unknown | Effect::may_fail;
    case IntrinsicId::absolute:
    case IntrinsicId::arc_cosine:
    case IntrinsicId::arc_sine:
    case IntrinsicId::arc_tangent:
    case IntrinsicId::square_root:
    case IntrinsicId::sine:
    case IntrinsicId::cosine:
    case IntrinsicId::tangent:
    case IntrinsicId::exponential:
    case IntrinsicId::logarithm:
    case IntrinsicId::maximum:
    case IntrinsicId::minimum:
    case IntrinsicId::round:
    case IntrinsicId::floor:
    case IntrinsicId::ceiling:
    case IntrinsicId::not_a_number:
    case IntrinsicId::infinity: return Effect::none;
    case IntrinsicId::python_float:
    case IntrinsicId::python_length:
    case IntrinsicId::matlab_length:
    case IntrinsicId::element_count:
    case IntrinsicId::sum:
    case IntrinsicId::reshape:
    case IntrinsicId::present: return Effect::may_fail;
    case IntrinsicId::count: break;
  }
  return Effect::external_unknown | Effect::may_fail;
}

class Builder final {
 public:
  Builder(Program& program, const hir::SemanticTable& semantics)
      : program_(program), semantics_(semantics) {}

  void begin_function(std::string name, const HirNodeId origin) {
    storages_.clear();
    storage_values_.clear();
    Function function;
    function.id = function_ids_.next();
    function.origin = origin;
    function.name = std::move(name);
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
    std::unordered_map<std::string, MirFunctionId> functions;
    for (std::size_t index = 1; index < program_.functions.size(); ++index) {
      functions.emplace(program_.functions[index].name, program_.functions[index].id);
    }
    program_.calls.reserve(unresolved_calls_.size());
    for (auto& unresolved : unresolved_calls_) {
      CallSite call;
      call.instruction = unresolved.instruction;
      call.origin = unresolved.origin;
      call.caller = unresolved.caller;
      const auto found = functions.find(unresolved.callee_name);
      if (found != functions.end()) {
        call.callee = found->second;
        program_.instructions[call.instruction.value()].callee = call.callee;
      }
      call.argument_types = std::move(unresolved.argument_types);
      call.argument_storages = std::move(unresolved.argument_storages);
      call.argument_omitted = std::move(unresolved.argument_omitted);
      call.result_type = unresolved.result_type;
      call.requested_results = unresolved.requested_results;
      program_.calls.push_back(std::move(call));
    }
  }

  Statement lower_statement(hir::Statement&& source) {
    ensure_open_block();
    Statement result;
    const auto* semantic_facts = semantics_.statement(source.id);
    result.origin = source.id;
    result.kind = source.kind;
    result.line = source.line;
    result.name = std::move(source.name);

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
    result.procedure_call = source.procedure_call;
    result.secondary_expression = lower_expression(std::move(source.secondary_expression));
    result.has_secondary_expression = source.has_secondary_expression;
    result.tertiary_expression = lower_expression(std::move(source.tertiary_expression));
    result.has_tertiary_expression = source.has_tertiary_expression;
    result.inclusive_stop = source.inclusive_stop;
    result.retain_last_loop_value = source.retain_last_loop_value;
    if (semantic_facts != nullptr) {
      result.declared_type = semantic_facts->declared_type;
      result.element_type = semantic_facts->element_type;
      result.previous_type = semantic_facts->previous_type;
      result.previous_element_type = semantic_facts->previous_element_type;
      result.parameter_intent = semantic_facts->parameter_intent;
      result.optional_parameter = semantic_facts->optional_parameter;
      result.dummy_parameter = semantic_facts->dummy_parameter;
      result.shape = semantic_facts->shape;
      result.index_base = semantic_facts->index_base;
      result.allow_negative_index = semantic_facts->allow_negative_index;
    }
    result.target_expression = lower_expression(std::move(source.target_expression));
    result.has_target_expression = source.has_target_expression;
    result.parameters = std::move(source.parameters);
    result.parameter_kinds = std::move(source.parameter_kinds);
    result.parameter_defaults.reserve(source.parameter_defaults.size());
    for (auto& expression : source.parameter_defaults) {
      result.parameter_defaults.push_back(lower_expression(std::move(expression)));
    }
    if (semantic_facts != nullptr) {
      result.parameter_intents = semantic_facts->parameter_intents;
      result.parameter_optional = semantic_facts->parameter_optional;
      result.parameter_types = semantic_facts->parameter_types;
      result.parameter_element_types = semantic_facts->parameter_element_types;
      result.parameter_shapes = semantic_facts->parameter_shapes;
    }
    result.return_names = std::move(source.return_names);
    if (semantic_facts != nullptr) {
      result.has_value_return = semantic_facts->has_value_return;
      result.return_types = semantic_facts->return_types;
      result.return_element_types = semantic_facts->return_element_types;
      result.return_shapes = semantic_facts->return_shapes;
      result.return_sequence_is_list = semantic_facts->return_sequence_is_list;
      result.return_sequence_elements = semantic_facts->return_sequence_elements;
    }
    result.target_names = std::move(source.target_names);
    if (semantic_facts != nullptr) result.target_pattern = semantic_facts->target_pattern;
    result.has_target_pattern = source.has_target_pattern;
    if (semantic_facts != nullptr) {
      result.target_types = semantic_facts->target_types;
      result.target_element_types = semantic_facts->target_element_types;
      result.target_shapes = semantic_facts->target_shapes;
      result.target_previous_types = semantic_facts->target_previous_types;
      result.target_previous_element_types = semantic_facts->target_previous_element_types;
    }
    result.case_selectors.reserve(source.case_selectors.size());
    for (auto& selector : source.case_selectors) {
      result.case_selectors.push_back(lower_selector(std::move(selector)));
    }
    result.default_case = source.default_case;

    if (result.kind == StatementKind::function) initialize_function_signature(result);

    auto effects = statement_effects(result.kind);
    effects |= result.expression.effects;
    effects |= result.secondary_expression.effects;
    effects |= result.tertiary_expression.effects;
    effects |= result.target_expression.effects;
    result.effects = effects;
    emit_statement_instruction(result);

    if (source.kind == StatementKind::if_statement) {
      const auto entry_versions = storage_values_;
      const auto then_block = make_function_block();
      const auto else_block = make_function_block();
      const auto merge_block = make_function_block();
      set_conditional(result.expression.value_id, then_block, else_block, source.id);
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
          const auto predicate = emit_case_predicate(clause, result.expression.value_id);
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
      set_conditional(result.expression.value_id, loop_body, loop_else, source.id);
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
                    loop_condition) != current_block().terminator.successors.end()) {
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
      if (result.expression.value_id.valid()) {
        terminator.operands.push_back(result.expression.value_id);
      }
    }
    if (result.kind == StatementKind::break_statement && !loops_.empty()) {
      loops_.back().break_edges.push_back({current_block_, storage_values_});
      set_branch(loops_.back().break_target, result.origin);
    } else if (result.kind == StatementKind::continue_statement && !loops_.empty()) {
      loops_.back().continue_edges.push_back({current_block_, storage_values_});
      set_branch(loops_.back().continue_target, result.origin);
    }
    return result;
  }

  [[nodiscard]] TypeId intern_type(const ValueType type, const ValueType element_type) {
    const auto key = type_key(type, element_type);
    const auto found = types_.find(key);
    if (found != types_.end()) return found->second;
    const auto id = TypeId{static_cast<TypeId::value_type>(program_.types.size())};
    const auto kind = type == ValueType::list       ? TypeKind::sequence
                      : type == ValueType::tuple    ? TypeKind::tuple
                      : type == ValueType::function ? TypeKind::function
                                                    : TypeKind::scalar;
    program_.types.push_back({kind, type, element_type, {}, {}, {}, {}, ParameterIntent::none});
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
                              ParameterIntent::none});
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
                              intent});
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
                              ParameterIntent::none});
    composite_types_.emplace(std::move(key), id);
    return id;
  }

  [[nodiscard]] TypeId intern_expression_type(const Expression& expression) {
    if (expression.inferred_type != ValueType::tuple && expression.tuple_types.empty()) {
      return intern_type(expression.inferred_type, expression.element_type);
    }
    std::vector<TypeId> elements;
    if (!expression.tuple_types.empty()) {
      elements.reserve(expression.tuple_types.size());
      for (std::size_t index = 0; index < expression.tuple_types.size(); ++index) {
        elements.push_back(
            intern_type(expression.tuple_types[index], index < expression.tuple_element_types.size()
                                                           ? expression.tuple_element_types[index]
                                                           : ValueType::unknown));
      }
    } else {
      elements.reserve(expression.children.size());
      for (const auto& child : expression.children) elements.push_back(child.type_id);
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

 private:
  using StorageVersions = std::unordered_map<StorageId, ValueId>;
  struct UnresolvedCall {
    InstructionId instruction{};
    HirNodeId origin{};
    MirFunctionId caller{};
    std::string callee_name;
    std::vector<TypeId> argument_types;
    std::vector<StorageId> argument_storages;
    std::vector<bool> argument_omitted;
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

  Expression lower_expression(hir::Expression&& source) {
    Expression result;
    const auto* semantic_facts = semantics_.expression(source.id);
    result.origin = source.id;
    result.location = source.location;
    result.kind = source.kind;
    result.value = std::move(source.value);
    result.operators = std::move(source.operators);
    result.children.reserve(source.children.size());
    std::vector<ValueId> operands;
    operands.reserve(source.children.size());
    Effect child_effects = Effect::none;
    for (auto& child : source.children) {
      auto lowered = lower_expression(std::move(child));
      if (lowered.value_id.valid()) operands.push_back(lowered.value_id);
      child_effects |= lowered.effects;
      result.children.push_back(std::move(lowered));
    }
    if (semantic_facts != nullptr) {
      result.inferred_type = semantic_facts->inferred_type;
      result.binding = semantic_facts->binding;
      result.intrinsic = semantic_facts->intrinsic;
      result.element_type = semantic_facts->element_type;
      result.shape = semantic_facts->shape;
      result.tuple_types = semantic_facts->tuple_types;
      result.tuple_element_types = semantic_facts->tuple_element_types;
      result.tuple_shapes = semantic_facts->tuple_shapes;
      result.sequence_is_list = semantic_facts->sequence_is_list;
      result.sequence_elements = semantic_facts->sequence_elements;
      result.requested_outputs = semantic_facts->requested_outputs;
      result.multi_output_call = semantic_facts->multi_output_call;
      result.argument_intents = semantic_facts->argument_intents;
      result.argument_names = semantic_facts->argument_names;
      result.argument_optional_forward = semantic_facts->argument_optional_forward;
      result.procedure_has_result = semantic_facts->procedure_has_result;
      result.index_base = semantic_facts->index_base;
      result.allow_negative_index = semantic_facts->allow_negative_index;
      result.column_major = semantic_facts->column_major;
      result.slice_stop_inclusive = semantic_facts->slice_stop_inclusive;
    }
    if (!result.valid()) return result;

    result.type_id = intern_expression_type(result);
    result.shape_id = intern_shape(result.shape, result.column_major);
    result.effects = expression_effects(result);
    result.effects |= child_effects;
    if (result.kind == ExpressionKind::identifier && result.binding == BindingKind::variable) {
      result.storage_id = storage_for(result.value, result.type_id, result.shape_id);
    } else if ((result.kind == ExpressionKind::index || result.kind == ExpressionKind::slice) &&
               !result.children.empty() && result.children.front().storage_id.valid()) {
      result.storage_id =
          make_view_storage(result.children.front().storage_id, result.type_id, result.shape_id,
                            result.origin, result.kind == ExpressionKind::slice);
    }
    result.value_id = value_ids_.next();
    Instruction instruction;
    instruction.id = instruction_ids_.next();
    instruction.opcode = expression_opcode(result.kind);
    instruction.origin = result.origin;
    instruction.location = result.location;
    instruction.result = result.value_id;
    instruction.type = result.type_id;
    instruction.shape = result.shape_id;
    instruction.storage = result.storage_id;
    instruction.effects = result.effects;
    instruction.operands = std::move(operands);
    if (result.kind == ExpressionKind::call && !result.children.empty() &&
        result.children.front().binding == BindingKind::function) {
      UnresolvedCall call;
      call.instruction = instruction.id;
      call.origin = result.origin;
      call.caller = current_function_;
      call.callee_name = result.children.front().value;
      call.result_type = result.type_id;
      call.requested_results =
          program_.source_language == SourceLanguage::matlab ? result.requested_outputs : 1U;
      call.argument_types.reserve(result.children.size() - 1U);
      call.argument_storages.reserve(result.children.size() - 1U);
      call.argument_omitted.reserve(result.children.size() - 1U);
      for (std::size_t index = 1; index < result.children.size(); ++index) {
        call.argument_types.push_back(result.children[index].type_id);
        call.argument_storages.push_back(result.children[index].storage_id);
        call.argument_omitted.push_back(result.children[index].kind ==
                                        ExpressionKind::omitted_argument);
      }
      unresolved_calls_.push_back(std::move(call));
    }
    append_instruction(std::move(instruction));
    return result;
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
    instruction.type = intern_type(ValueType::boolean, ValueType::unknown);
    instruction.shape = intern_shape({}, false);
    instruction.effects = Effect::control;
    if (selector.valid()) instruction.operands.push_back(selector);
    for (const auto& source_selector : clause.case_selectors) {
      if (source_selector.has_lower) {
        auto copy = source_selector.lower;
        auto lowered = lower_expression(std::move(copy));
        if (lowered.value_id.valid()) instruction.operands.push_back(lowered.value_id);
      }
      if (source_selector.has_upper) {
        auto copy = source_selector.upper;
        auto lowered = lower_expression(std::move(copy));
        if (lowered.value_id.valid()) instruction.operands.push_back(lowered.value_id);
      }
    }
    instruction.id = instruction_ids_.next();
    instruction.result = value_ids_.next();
    const auto result = instruction.result;
    append_instruction(std::move(instruction));
    return result;
  }

  void lower_statement_list(std::vector<hir::Statement>&& source, std::vector<Statement>& result) {
    result.reserve(source.size());
    for (auto& statement : source) {
      result.push_back(lower_statement(std::move(statement)));
    }
  }

  Effect expression_effects(const Expression& expression) const noexcept {
    if (expression.kind == ExpressionKind::identifier &&
        expression.binding == BindingKind::variable) {
      return Effect::read;
    }
    if (expression.kind == ExpressionKind::call) {
      if (!expression.children.empty() &&
          expression.children.front().binding == BindingKind::builtin) {
        return intrinsic_effects(expression.children.front().intrinsic);
      }
      return Effect::external_unknown | Effect::may_fail;
    }
    if (expression.kind == ExpressionKind::index || expression.kind == ExpressionKind::slice) {
      return Effect::read | Effect::may_fail;
    }
    if (expression.kind == ExpressionKind::list || expression.kind == ExpressionKind::tuple) {
      return Effect::allocate;
    }
    if (expression.kind == ExpressionKind::conditional ||
        expression.kind == ExpressionKind::comparison_chain) {
      return Effect::control;
    }
    return Effect::none;
  }

  static Effect statement_effects(const StatementKind kind) noexcept {
    switch (kind) {
      case StatementKind::declaration:
      case StatementKind::assignment:
      case StatementKind::multi_assignment:
      case StatementKind::indexed_assignment: return Effect::write;
      case StatementKind::print: return Effect::io;
      case StatementKind::return_statement:
      case StatementKind::break_statement:
      case StatementKind::continue_statement:
      case StatementKind::if_statement:
      case StatementKind::select_case:
      case StatementKind::case_clause:
      case StatementKind::while_loop:
      case StatementKind::range_loop: return Effect::control;
      case StatementKind::expression:
      case StatementKind::function: return Effect::none;
    }
    return Effect::none;
  }

  void emit_statement_instruction(const Statement& statement) {
    Instruction instruction;
    instruction.id = instruction_ids_.next();
    instruction.opcode = statement_opcode(statement.kind);
    instruction.origin = statement.origin;
    instruction.location = {statement.line, 1};
    instruction.effects = statement.effects;
    if (statement.expression.value_id.valid()) {
      instruction.operands.push_back(statement.expression.value_id);
    }
    if (statement.secondary_expression.value_id.valid()) {
      instruction.operands.push_back(statement.secondary_expression.value_id);
    }
    if (statement.tertiary_expression.value_id.valid()) {
      instruction.operands.push_back(statement.tertiary_expression.value_id);
    }
    if (statement.target_expression.value_id.valid()) {
      instruction.operands.push_back(statement.target_expression.value_id);
    }
    if ((statement.kind == StatementKind::declaration ||
         statement.kind == StatementKind::assignment ||
         statement.kind == StatementKind::range_loop) &&
        !statement.name.empty()) {
      instruction.storage =
          storage_for(statement.name, intern_type(statement.declared_type, statement.element_type),
                      intern_shape(statement.shape, false));
    } else if (statement.kind == StatementKind::indexed_assignment &&
               statement.target_expression.storage_id.valid()) {
      instruction.storage = statement.target_expression.storage_id;
    }
    if ((statement.kind == StatementKind::declaration ||
         statement.kind == StatementKind::assignment ||
         statement.kind == StatementKind::indexed_assignment) &&
        instruction.storage.valid() && statement.expression.value_id.valid()) {
      instruction.result = value_ids_.next();
      instruction.type = statement.expression.type_id;
      instruction.shape = statement.expression.shape_id;
      storage_values_[instruction.storage] = instruction.result;
    }
    append_instruction(std::move(instruction));
  }

  StorageId storage_for(const std::string& name, const TypeId type, const ShapeId shape,
                        const StorageKind kind = StorageKind::local,
                        const ParameterIntent intent = ParameterIntent::none) {
    const auto found = storages_.find(name);
    if (found != storages_.end()) return found->second;
    const auto id = StorageId{static_cast<StorageId::value_type>(program_.storages.size())};
    const auto parameter = kind == StorageKind::parameter;
    program_.storages.push_back({name,
                                 type,
                                 shape,
                                 parameter ? AliasClass::may_alias : AliasClass::no_alias,
                                 intent != ParameterIntent::in,
                                 kind,
                                 parameter ? StorageLifetime::borrowed : StorageLifetime::function,
                                 {},
                                 intent});
    storages_.emplace(name, id);
    return id;
  }

  StorageId make_view_storage(const StorageId base, const TypeId type, const ShapeId shape,
                              const HirNodeId origin, const bool section) {
    const auto id = StorageId{static_cast<StorageId::value_type>(program_.storages.size())};
    program_.storages.push_back({"$view" + std::to_string(id.value()), type, shape,
                                 section ? AliasClass::may_alias : AliasClass::must_alias,
                                 program_.storages[base.value()].writable, StorageKind::view,
                                 StorageLifetime::expression, base, ParameterIntent::none});
    program_.aliases.push_back(
        {base, id, section ? AliasClass::may_alias : AliasClass::must_alias, origin});
    return id;
  }

  void initialize_function_signature(const Statement& statement) {
    auto& function = current_function();
    function.parameter_types.reserve(statement.parameters.size());
    function.parameter_optional = statement.parameter_optional;
    std::vector<TypeId> signature_parameters;
    signature_parameters.reserve(statement.parameters.size());
    for (std::size_t index = 0; index < statement.parameters.size(); ++index) {
      const auto type =
          intern_type(index < statement.parameter_types.size() ? statement.parameter_types[index]
                                                               : ValueType::unknown,
                      index < statement.parameter_element_types.size()
                          ? statement.parameter_element_types[index]
                          : ValueType::unknown);
      const auto shape =
          intern_shape(index < statement.parameter_shapes.size() ? statement.parameter_shapes[index]
                                                                 : std::vector<std::size_t>{},
                       false);
      const auto intent = index < statement.parameter_intents.size()
                              ? statement.parameter_intents[index]
                              : ParameterIntent::none;
      const auto storage =
          storage_for(statement.parameters[index], type, shape, StorageKind::parameter, intent);
      const auto value = value_ids_.next();
      current_block().arguments.push_back({value, type, shape, storage});
      storage_values_[storage] = value;
      function.parameter_types.push_back(type);
      signature_parameters.push_back(
          intent == ParameterIntent::none ? type : intern_reference_type(type, intent));
    }
    if (program_.source_language == SourceLanguage::python && statement.has_value_return) {
      if (statement.declared_type == ValueType::tuple && !statement.return_types.empty()) {
        std::vector<TypeId> elements;
        elements.reserve(statement.return_types.size());
        for (std::size_t index = 0; index < statement.return_types.size(); ++index) {
          elements.push_back(intern_type(statement.return_types[index],
                                         index < statement.return_element_types.size()
                                             ? statement.return_element_types[index]
                                             : ValueType::unknown));
        }
        function.result_types.push_back(intern_tuple_type(elements));
      } else {
        function.result_types.push_back(
            intern_type(statement.declared_type, statement.element_type));
      }
    } else {
      for (std::size_t index = 0; index < statement.return_types.size(); ++index) {
        function.result_types.push_back(
            intern_type(statement.return_types[index], index < statement.return_element_types.size()
                                                           ? statement.return_element_types[index]
                                                           : ValueType::unknown));
      }
    }
    function.signature = intern_function_type(signature_parameters, function.result_types);
  }

  void append_instruction(Instruction instruction) {
    ensure_open_block();
    current_block().instructions.push_back(instruction.id);
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
  IrIdAllocator<MirFunctionId> function_ids_;
  IrIdAllocator<BlockId> block_ids_;
  IrIdAllocator<InstructionId> instruction_ids_;
  IrIdAllocator<ValueId> value_ids_;
  MirFunctionId current_function_{};
  BlockId current_block_{};
  std::unordered_map<std::uint32_t, TypeId> types_;
  std::unordered_map<std::string, TypeId> composite_types_;
  std::unordered_map<std::string, ShapeId> shapes_;
  std::unordered_map<std::string, StorageId> storages_;
  StorageVersions storage_values_;
  std::vector<LoopContext> loops_;
  std::vector<UnresolvedCall> unresolved_calls_;
};

void add_error(std::vector<Diagnostic>& diagnostics, const SourceLocation location,
               const std::string_view stage, std::string message) {
  diagnostics.push_back({DiagnosticSeverity::error, "MPF0006",
                         "invalid MIR at '" + std::string(stage) + "': " + std::move(message),
                         location});
}

template <typename Id, typename Item>
bool valid_index(const Id id, const std::vector<Item>& items) noexcept {
  return id.valid() && static_cast<std::size_t>(id.value()) < items.size();
}

const TypeData* type_data(const Program& program, const TypeId id) noexcept {
  return valid_index(id, program.types) ? &program.types[id.value()] : nullptr;
}

TypeId logical_type(const Program& program, const TypeId id) noexcept {
  const auto* type = type_data(program, id);
  return type != nullptr && type->kind == TypeKind::reference ? type->referent : id;
}

bool compatible_type(const Program& program, const TypeId actual, const TypeId expected) noexcept {
  const auto actual_logical = logical_type(program, actual);
  const auto expected_logical = logical_type(program, expected);
  if (actual_logical == expected_logical) return true;
  const auto* actual_data = type_data(program, actual_logical);
  const auto* expected_data = type_data(program, expected_logical);
  if (actual_data == nullptr || expected_data == nullptr) return false;
  if (actual_data->value_type == ValueType::unknown ||
      expected_data->value_type == ValueType::unknown) {
    return true;
  }
  if (actual_data->kind != expected_data->kind ||
      actual_data->value_type != expected_data->value_type) {
    return false;
  }
  if (actual_data->kind == TypeKind::sequence) {
    return actual_data->element_type == expected_data->element_type ||
           actual_data->element_type == ValueType::unknown ||
           expected_data->element_type == ValueType::unknown;
  }
  if (actual_data->kind == TypeKind::tuple) {
    if (actual_data->elements.size() != expected_data->elements.size()) return false;
    for (std::size_t index = 0; index < actual_data->elements.size(); ++index) {
      if (!compatible_type(program, actual_data->elements[index], expected_data->elements[index])) {
        return false;
      }
    }
  }
  return true;
}

void verify_expression(const Expression& expression, const Program& program,
                       std::vector<Diagnostic>& diagnostics, const std::string_view stage) {
  if (!expression.valid()) return;
  if (!expression.origin.valid() || !expression.value_id.valid() ||
      !valid_index(expression.type_id, program.types) ||
      !valid_index(expression.shape_id, program.shapes)) {
    add_error(diagnostics, expression.location, stage,
              "typed expression has an invalid origin, value, type, or shape ID");
  }
  if (expression.storage_id.valid() && !valid_index(expression.storage_id, program.storages)) {
    add_error(diagnostics, expression.location, stage,
              "expression references an invalid storage ID");
  }
  for (const auto& child : expression.children) {
    verify_expression(child, program, diagnostics, stage);
  }
}

void verify_statements(const std::vector<Statement>& statements, const Program& program,
                       std::vector<Diagnostic>& diagnostics, const std::string_view stage) {
  for (const auto& statement : statements) {
    if (!statement.origin.valid()) {
      add_error(diagnostics, {statement.line, 1}, stage, "statement has no HIR origin");
    }
    verify_expression(statement.expression, program, diagnostics, stage);
    verify_expression(statement.secondary_expression, program, diagnostics, stage);
    verify_expression(statement.tertiary_expression, program, diagnostics, stage);
    verify_expression(statement.target_expression, program, diagnostics, stage);
    for (const auto& expression : statement.parameter_defaults) {
      verify_expression(expression, program, diagnostics, stage);
    }
    for (const auto& selector : statement.case_selectors) {
      verify_expression(selector.lower, program, diagnostics, stage);
      verify_expression(selector.upper, program, diagnostics, stage);
    }
    verify_statements(statement.body, program, diagnostics, stage);
    verify_statements(statement.alternative, program, diagnostics, stage);
  }
}

struct ValueDefinition {
  BlockId block{};
  std::size_t order{0};
  TypeId type{};
  ShapeId shape{};
  StorageId storage{};
};

void verify_terminator_shape(const BasicBlock& block, std::vector<Diagnostic>& diagnostics,
                             const std::string_view stage) {
  const auto& terminator = block.terminator;
  bool valid = false;
  switch (terminator.kind) {
    case TerminatorKind::branch:
      valid = terminator.operands.empty() && terminator.successors.size() == 1 &&
              terminator.successor_arguments.size() == 1;
      break;
    case TerminatorKind::conditional_branch:
      valid = terminator.operands.size() == 1 && terminator.successors.size() == 2 &&
              terminator.successor_arguments.size() == 2;
      break;
    case TerminatorKind::return_value:
      valid = terminator.operands.size() <= 1 && terminator.successors.empty() &&
              terminator.successor_arguments.empty();
      break;
    case TerminatorKind::unreachable:
      valid = terminator.operands.empty() && terminator.successors.empty() &&
              terminator.successor_arguments.empty();
      break;
    case TerminatorKind::none: break;
  }
  if (!valid) {
    add_error(diagnostics, {1, 1}, stage,
              "terminator operand/successor arity does not match its opcode");
  }
}

void verify_cfg(const Program& program, std::vector<Diagnostic>& diagnostics,
                const std::string_view stage) {
  std::vector<MirFunctionId> block_owners(program.blocks.size());
  std::vector<BlockId> instruction_owners(program.instructions.size());

  for (std::size_t index = 1; index < program.functions.size(); ++index) {
    const auto& function = program.functions[index];
    if (function.id.value() != index || !valid_index(function.entry, program.blocks) ||
        function.blocks.empty()) {
      add_error(diagnostics, {1, 1}, stage,
                "function table is not dense or function has no valid entry block");
      continue;
    }
    if (std::find(function.blocks.begin(), function.blocks.end(), function.entry) ==
        function.blocks.end()) {
      add_error(diagnostics, {1, 1}, stage, "function entry block is not owned by the function");
    }
    for (const auto block_id : function.blocks) {
      if (!valid_index(block_id, program.blocks)) {
        add_error(diagnostics, {1, 1}, stage, "function references an invalid block");
        continue;
      }
      auto& owner = block_owners[block_id.value()];
      if (owner.valid()) {
        add_error(diagnostics, {1, 1}, stage,
                  "basic block is owned more than once by the function table");
      } else {
        owner = function.id;
      }
    }
  }

  for (std::size_t index = 1; index < program.blocks.size(); ++index) {
    const auto& block = program.blocks[index];
    if (block.id.value() != index) {
      add_error(diagnostics, {1, 1}, stage, "basic block table is not dense");
    }
    if (!block_owners[index].valid()) {
      add_error(diagnostics, {1, 1}, stage, "basic block has no owning function");
    }
    verify_terminator_shape(block, diagnostics, stage);
    for (const auto instruction_id : block.instructions) {
      if (!valid_index(instruction_id, program.instructions)) {
        add_error(diagnostics, {1, 1}, stage, "block references an invalid instruction");
        continue;
      }
      auto& owner = instruction_owners[instruction_id.value()];
      if (owner.valid()) {
        add_error(diagnostics, program.instructions[instruction_id.value()].location, stage,
                  "instruction is owned by more than one basic block");
      } else {
        owner = block.id;
      }
    }
    for (const auto successor : block.terminator.successors) {
      if (!valid_index(successor, program.blocks)) {
        add_error(diagnostics, {1, 1}, stage, "terminator references an invalid successor");
      } else if (block_owners[index].valid() && block_owners[successor.value()].valid() &&
                 block_owners[index] != block_owners[successor.value()]) {
        add_error(diagnostics, {1, 1}, stage, "control-flow edge crosses a function boundary");
      }
    }
    if (block.terminator.successor_arguments.size() == block.terminator.successors.size()) {
      for (std::size_t edge = 0; edge < block.terminator.successors.size(); ++edge) {
        const auto successor = block.terminator.successors[edge];
        if (!valid_index(successor, program.blocks)) continue;
        if (block.terminator.successor_arguments[edge].size() !=
            program.blocks[successor.value()].arguments.size()) {
          add_error(diagnostics, {1, 1}, stage,
                    "control-flow edge argument arity does not match successor block arguments");
        }
      }
    }
  }

  std::unordered_map<ValueId, ValueDefinition> definitions;
  for (std::size_t block_index = 1; block_index < program.blocks.size(); ++block_index) {
    const auto& block = program.blocks[block_index];
    for (const auto& argument : block.arguments) {
      if (!argument.value.valid() || !valid_index(argument.type, program.types) ||
          !valid_index(argument.shape, program.shapes) ||
          (argument.storage.valid() && !valid_index(argument.storage, program.storages)) ||
          !definitions
               .emplace(argument.value, ValueDefinition{block.id, 0, argument.type, argument.shape,
                                                        argument.storage})
               .second) {
        add_error(diagnostics, {1, 1}, stage,
                  "block argument has invalid metadata or a duplicate value identity");
      }
    }
    for (std::size_t order = 0; order < block.instructions.size(); ++order) {
      const auto instruction_id = block.instructions[order];
      if (!valid_index(instruction_id, program.instructions)) continue;
      const auto& instruction = program.instructions[instruction_id.value()];
      if (instruction.result.valid() &&
          !definitions
               .emplace(instruction.result, ValueDefinition{block.id, order + 1U, instruction.type,
                                                            instruction.shape, instruction.storage})
               .second) {
        add_error(diagnostics, instruction.location, stage,
                  "value is defined by more than one instruction or block argument");
      }
    }
  }
  for (std::size_t index = 1; index < program.instructions.size(); ++index) {
    const auto& instruction = program.instructions[index];
    if (!instruction_owners[index].valid()) {
      add_error(diagnostics, instruction.location, stage, "instruction has no owning basic block");
    }
  }

  for (std::size_t block_index = 1; block_index < program.blocks.size(); ++block_index) {
    const auto& block = program.blocks[block_index];
    if (block.terminator.successor_arguments.size() != block.terminator.successors.size()) continue;
    for (std::size_t edge = 0; edge < block.terminator.successors.size(); ++edge) {
      const auto successor = block.terminator.successors[edge];
      if (!valid_index(successor, program.blocks)) continue;
      const auto& expected = program.blocks[successor.value()].arguments;
      const auto& actual = block.terminator.successor_arguments[edge];
      if (actual.size() != expected.size()) continue;
      for (std::size_t argument = 0; argument < actual.size(); ++argument) {
        const auto found = definitions.find(actual[argument]);
        if (found == definitions.end()) continue;
        if (found->second.type != expected[argument].type ||
            found->second.shape != expected[argument].shape ||
            (found->second.storage.valid() && expected[argument].storage.valid() &&
             found->second.storage != expected[argument].storage)) {
          add_error(diagnostics, {1, 1}, stage,
                    "control-flow edge argument type, shape, or storage is incompatible");
        }
      }
    }
  }

  for (std::size_t function_index = 1; function_index < program.functions.size();
       ++function_index) {
    const auto& function = program.functions[function_index];
    if (function.blocks.empty() || !valid_index(function.entry, program.blocks)) continue;
    const auto& entry_block = program.blocks[function.entry.value()];
    if (entry_block.arguments.size() != function.parameter_types.size()) {
      add_error(diagnostics, {1, 1}, stage,
                "function entry block arguments do not match its parameter signature");
    } else {
      for (std::size_t parameter = 0; parameter < function.parameter_types.size(); ++parameter) {
        if (!valid_index(function.parameter_types[parameter], program.types) ||
            entry_block.arguments[parameter].type != function.parameter_types[parameter]) {
          add_error(diagnostics, {1, 1}, stage,
                    "function parameter type is invalid or differs from its entry argument");
        }
      }
    }
    for (const auto result : function.result_types) {
      if (!valid_index(result, program.types)) {
        add_error(diagnostics, {1, 1}, stage, "function result signature has an invalid type");
      }
    }
    std::unordered_map<BlockId, std::size_t> positions;
    for (std::size_t index = 0; index < function.blocks.size(); ++index) {
      positions.emplace(function.blocks[index], index);
    }
    const auto entry = positions.find(function.entry);
    if (entry == positions.end()) continue;

    std::vector<std::vector<std::size_t>> predecessors(function.blocks.size());
    for (std::size_t index = 0; index < function.blocks.size(); ++index) {
      const auto block_id = function.blocks[index];
      if (!valid_index(block_id, program.blocks)) continue;
      for (const auto successor : program.blocks[block_id.value()].terminator.successors) {
        const auto found = positions.find(successor);
        if (found != positions.end()) predecessors[found->second].push_back(index);
      }
    }

    std::vector<std::vector<bool>> dominators(function.blocks.size(),
                                              std::vector<bool>(function.blocks.size(), true));
    std::fill(dominators[entry->second].begin(), dominators[entry->second].end(), false);
    dominators[entry->second][entry->second] = true;
    bool changed = true;
    while (changed) {
      changed = false;
      for (std::size_t block = 0; block < function.blocks.size(); ++block) {
        if (block == entry->second) continue;
        std::vector<bool> next(function.blocks.size(), false);
        if (!predecessors[block].empty()) {
          std::fill(next.begin(), next.end(), true);
          for (const auto predecessor : predecessors[block]) {
            for (std::size_t candidate = 0; candidate < next.size(); ++candidate) {
              next[candidate] = next[candidate] && dominators[predecessor][candidate];
            }
          }
        }
        next[block] = true;
        if (next != dominators[block]) {
          dominators[block] = std::move(next);
          changed = true;
        }
      }
    }

    const auto verify_use = [&](const ValueId operand, const BlockId use_block,
                                const std::size_t use_order, const SourceLocation location) {
      const auto definition = definitions.find(operand);
      if (definition == definitions.end()) {
        add_error(diagnostics, location, stage, "value operand has no definition");
        return;
      }
      if (definition->second.block == use_block) {
        if (definition->second.order >= use_order) {
          add_error(diagnostics, location, stage,
                    "value definition does not precede its use in the basic block");
        }
        return;
      }
      const auto definition_position = positions.find(definition->second.block);
      const auto use_position = positions.find(use_block);
      if (definition_position == positions.end() || use_position == positions.end() ||
          !dominators[use_position->second][definition_position->second]) {
        add_error(diagnostics, location, stage,
                  "value %v" + std::to_string(operand.value()) + " defined in block ^b" +
                      std::to_string(definition->second.block.value()) +
                      " does not dominate its use in block ^b" + std::to_string(use_block.value()));
      }
    };

    for (const auto block_id : function.blocks) {
      if (!valid_index(block_id, program.blocks)) continue;
      const auto& block = program.blocks[block_id.value()];
      for (std::size_t order = 0; order < block.instructions.size(); ++order) {
        const auto instruction_id = block.instructions[order];
        if (!valid_index(instruction_id, program.instructions)) continue;
        const auto& instruction = program.instructions[instruction_id.value()];
        for (const auto operand : instruction.operands) {
          verify_use(operand, block.id, order + 1U, instruction.location);
        }
      }
      for (const auto operand : block.terminator.operands) {
        verify_use(operand, block.id, block.instructions.size() + 1U, {1, 1});
      }
      for (const auto& edge_arguments : block.terminator.successor_arguments) {
        for (const auto operand : edge_arguments) {
          verify_use(operand, block.id, block.instructions.size() + 1U, {1, 1});
        }
      }
    }
  }
}

void verify_function_types_and_calls(const Program& program, std::vector<Diagnostic>& diagnostics,
                                     const std::string_view stage) {
  std::vector<MirFunctionId> instruction_callers(program.instructions.size());
  std::unordered_map<ValueId, TypeId> value_types;
  std::unordered_map<std::string, MirFunctionId> function_names;

  for (std::size_t function_index = 1; function_index < program.functions.size();
       ++function_index) {
    const auto& function = program.functions[function_index];
    if (!function_names.emplace(function.name, function.id).second) {
      add_error(diagnostics, {1, 1}, stage, "function name is not unique in MIR");
    }
    const auto* signature = type_data(program, function.signature);
    if (signature == nullptr || signature->kind != TypeKind::function ||
        signature->parameters.size() != function.parameter_types.size() ||
        signature->results != function.result_types ||
        (!function.parameter_optional.empty() &&
         function.parameter_optional.size() != function.parameter_types.size())) {
      add_error(diagnostics, {1, 1}, stage,
                "function signature is missing or disagrees with its logical types");
    } else {
      for (std::size_t parameter = 0; parameter < function.parameter_types.size(); ++parameter) {
        if (!compatible_type(program, function.parameter_types[parameter],
                             signature->parameters[parameter])) {
          add_error(diagnostics, {1, 1}, stage,
                    "function signature parameter disagrees with its logical type");
        }
      }
    }
    for (const auto block_id : function.blocks) {
      if (!valid_index(block_id, program.blocks)) continue;
      const auto& block = program.blocks[block_id.value()];
      for (const auto& argument : block.arguments) {
        if (argument.value.valid() && valid_index(argument.type, program.types)) {
          value_types.emplace(argument.value, argument.type);
        }
      }
      for (const auto instruction_id : block.instructions) {
        if (!valid_index(instruction_id, program.instructions)) continue;
        instruction_callers[instruction_id.value()] = function.id;
        const auto& instruction = program.instructions[instruction_id.value()];
        if (instruction.result.valid() && valid_index(instruction.type, program.types)) {
          value_types.emplace(instruction.result, instruction.type);
        }
      }
    }
  }

  for (std::size_t function_index = 1; function_index < program.functions.size();
       ++function_index) {
    const auto& function = program.functions[function_index];
    for (const auto block_id : function.blocks) {
      if (!valid_index(block_id, program.blocks)) continue;
      const auto& terminator = program.blocks[block_id.value()].terminator;
      if (terminator.kind != TerminatorKind::return_value || terminator.operands.empty()) continue;
      if (function.result_types.empty()) {
        add_error(diagnostics, {1, 1}, stage,
                  "value-returning terminator belongs to a function with no result type");
        continue;
      }
      const auto actual = value_types.find(terminator.operands.front());
      if (actual == value_types.end()) continue;
      if (function.result_types.size() == 1U) {
        if (!compatible_type(program, actual->second, function.result_types.front())) {
          add_error(diagnostics, {1, 1}, stage,
                    "return value type disagrees with the function result signature");
        }
        continue;
      }
      const auto* tuple = type_data(program, actual->second);
      if (tuple == nullptr || tuple->kind != TypeKind::tuple ||
          tuple->elements.size() != function.result_types.size()) {
        add_error(diagnostics, {1, 1}, stage,
                  "multiple function results are not returned as a matching tuple type");
        continue;
      }
      for (std::size_t result = 0; result < function.result_types.size(); ++result) {
        if (!compatible_type(program, tuple->elements[result], function.result_types[result])) {
          add_error(diagnostics, {1, 1}, stage,
                    "tuple return element disagrees with the function result signature");
        }
      }
    }
  }

  std::vector<bool> seen_call_instruction(program.instructions.size(), false);
  for (const auto& call : program.calls) {
    if (!valid_index(call.instruction, program.instructions) ||
        !valid_index(call.caller, program.functions) ||
        !valid_index(call.callee, program.functions) || !call.origin.valid() ||
        call.requested_results == 0 ||
        call.argument_types.size() != call.argument_storages.size() ||
        call.argument_types.size() != call.argument_omitted.size()) {
      add_error(diagnostics, {1, 1}, stage, "call-site table contains invalid identity or arity");
      continue;
    }
    const auto& instruction = program.instructions[call.instruction.value()];
    if (seen_call_instruction[call.instruction.value()] || instruction.opcode != Opcode::call ||
        instruction.origin != call.origin || instruction.callee != call.callee ||
        instruction_callers[call.instruction.value()] != call.caller) {
      add_error(diagnostics, instruction.location, stage,
                "call site does not match its call instruction or owning function");
      continue;
    }
    seen_call_instruction[call.instruction.value()] = true;
    const auto& callee = program.functions[call.callee.value()];
    const auto* signature = type_data(program, callee.signature);
    if (signature == nullptr || signature->kind != TypeKind::function) {
      add_error(diagnostics, instruction.location, stage,
                "call site references a callee without a function type");
      continue;
    }
    if (call.argument_types.size() > signature->parameters.size()) {
      add_error(diagnostics, instruction.location, stage,
                "call has more arguments than the callee signature");
      continue;
    }
    for (std::size_t parameter = call.argument_types.size();
         parameter < signature->parameters.size(); ++parameter) {
      if (parameter >= callee.parameter_optional.size() || !callee.parameter_optional[parameter]) {
        add_error(diagnostics, instruction.location, stage,
                  "call omits a required function parameter");
      }
    }
    for (std::size_t argument = 0; argument < call.argument_types.size(); ++argument) {
      const bool optional =
          argument < callee.parameter_optional.size() && callee.parameter_optional[argument];
      if (call.argument_omitted[argument]) {
        if (!optional) {
          add_error(diagnostics, instruction.location, stage,
                    "call uses an omitted value for a required function parameter");
        }
        continue;
      }
      if (!valid_index(call.argument_types[argument], program.types) ||
          !compatible_type(program, call.argument_types[argument],
                           signature->parameters[argument])) {
        add_error(diagnostics, instruction.location, stage,
                  "call argument type disagrees with the callee signature");
        continue;
      }
      const auto* formal = type_data(program, signature->parameters[argument]);
      if (formal != nullptr && formal->kind == TypeKind::reference &&
          (formal->reference_intent == ParameterIntent::out ||
           formal->reference_intent == ParameterIntent::inout) &&
          (!valid_index(call.argument_storages[argument], program.storages) ||
           !program.storages[call.argument_storages[argument].value()].writable)) {
        add_error(diagnostics, instruction.location, stage,
                  "OUT/INOUT call argument is not backed by writable storage");
      }
    }
    if (signature->results.empty()) continue;
    if (!valid_index(call.result_type, program.types) ||
        call.requested_results > signature->results.size()) {
      add_error(diagnostics, instruction.location, stage,
                "call result arity or type identity disagrees with the callee signature");
      continue;
    }
    if (call.requested_results == 1U) {
      if (!compatible_type(program, call.result_type, signature->results.front())) {
        add_error(diagnostics, instruction.location, stage,
                  "call result type disagrees with the callee result signature");
      }
      continue;
    }
    const auto* tuple = type_data(program, call.result_type);
    if (tuple == nullptr || tuple->kind != TypeKind::tuple ||
        tuple->elements.size() != call.requested_results) {
      add_error(diagnostics, instruction.location, stage,
                "multi-result call does not produce a matching tuple type");
      continue;
    }
    for (std::size_t result = 0; result < call.requested_results; ++result) {
      if (!compatible_type(program, tuple->elements[result], signature->results[result])) {
        add_error(diagnostics, instruction.location, stage,
                  "multi-result call tuple element disagrees with the callee signature");
      }
    }
  }
  for (std::size_t index = 1; index < program.instructions.size(); ++index) {
    const auto& instruction = program.instructions[index];
    if (instruction.callee.valid() &&
        (instruction.opcode != Opcode::call ||
         !valid_index(instruction.callee, program.functions) || !seen_call_instruction[index])) {
      add_error(diagnostics, instruction.location, stage,
                "linked user-function call instruction has no matching call-site entry");
    }
  }
}

}  // namespace

LoweringResult lower_from_hir(hir::Program&& source, hir::SemanticTable&& semantics) {
  LoweringResult result;
  result.program.source_language = source.language;
  result.program.semantics = source.semantics;
  result.program.hir_node_count = source.node_count;
  result.program.types.push_back({});
  result.program.shapes.push_back({});
  result.program.storages.push_back({});
  result.program.instructions.push_back({});
  result.program.blocks.push_back({});
  result.program.functions.push_back({});

  result.diagnostics = hir::verify_semantics(source, semantics, "hir-to-mir");
  if (!result.diagnostics.empty()) return result;

  Builder builder(result.program, semantics);
  builder.begin_function("<module>", {});
  result.program.statements.reserve(source.statements.size());
  for (auto& statement : source.statements) {
    if (statement.kind == StatementKind::function) continue;
    result.program.statements.push_back(builder.lower_statement(std::move(statement)));
  }
  builder.finish_function();

  for (auto& statement : source.statements) {
    if (statement.kind != StatementKind::function) continue;
    const auto function_name = statement.name;
    const auto function_origin = statement.id;
    builder.begin_function(function_name, function_origin);
    result.program.statements.push_back(builder.lower_statement(std::move(statement)));
    builder.finish_function();
  }
  builder.link_calls();
  result.diagnostics = verify(result.program, "hir-to-mir");
  return result;
}

AliasClass alias_between(const Program& program, const StorageId left,
                         const StorageId right) noexcept {
  if (!valid_index(left, program.storages) || !valid_index(right, program.storages)) {
    return AliasClass::may_alias;
  }
  if (left == right) return AliasClass::must_alias;
  for (const auto& relation : program.aliases) {
    if ((relation.left == left && relation.right == right) ||
        (relation.left == right && relation.right == left)) {
      return relation.relation;
    }
  }

  const auto root = [&](StorageId storage) {
    for (std::size_t depth = 0; depth < program.storages.size(); ++depth) {
      const auto& metadata = program.storages[storage.value()];
      if (metadata.kind != StorageKind::view) return storage;
      if (!valid_index(metadata.base, program.storages)) return StorageId{};
      storage = metadata.base;
    }
    return StorageId{};
  };
  const auto left_root = root(left);
  const auto right_root = root(right);
  if (!left_root.valid() || !right_root.valid()) return AliasClass::may_alias;
  if (left_root == right_root) return AliasClass::may_alias;

  const auto left_kind = program.storages[left_root.value()].kind;
  const auto right_kind = program.storages[right_root.value()].kind;
  if (left_kind == StorageKind::global || right_kind == StorageKind::global ||
      (left_kind == StorageKind::parameter && right_kind == StorageKind::parameter)) {
    return AliasClass::may_alias;
  }
  return AliasClass::no_alias;
}

std::vector<Diagnostic> validate_effects(Program& program) {
  std::vector<Diagnostic> diagnostics;
  for (std::size_t index = 1; index < program.instructions.size(); ++index) {
    const auto& instruction = program.instructions[index];
    const bool invalid_external = has_effect(instruction.effects, Effect::external_unknown) &&
                                  !has_effect(instruction.effects, Effect::may_fail);
    const bool invalid_output =
        instruction.opcode == Opcode::output && !has_effect(instruction.effects, Effect::io);
    const bool invalid_store = (instruction.opcode == Opcode::assignment ||
                                instruction.opcode == Opcode::indexed_assignment) &&
                               !has_effect(instruction.effects, Effect::write);
    if (invalid_external || invalid_output || invalid_store) {
      add_error(diagnostics, instruction.location, "effect-validation",
                "instruction effect set is weaker than its operation requires");
    }
  }
  return diagnostics;
}

std::vector<Diagnostic> verify(const Program& program, const std::string_view stage) {
  std::vector<Diagnostic> diagnostics;
  if (program.source_language == SourceLanguage::automatic) {
    add_error(diagnostics, {1, 1}, stage, "source language is unresolved");
  }
  if (program.types.size() <= 1 || program.shapes.size() <= 1 || program.blocks.size() <= 1 ||
      program.functions.size() <= 1) {
    add_error(diagnostics, {1, 1}, stage, "required dense tables are empty");
    return diagnostics;
  }
  for (std::size_t index = 1; index < program.types.size(); ++index) {
    const auto& type = program.types[index];
    for (const auto element : type.elements) {
      if (!valid_index(element, program.types)) {
        add_error(diagnostics, {1, 1}, stage, "composite type references an invalid element type");
      }
    }
    for (const auto parameter : type.parameters) {
      if (!valid_index(parameter, program.types)) {
        add_error(diagnostics, {1, 1}, stage, "function type references an invalid parameter type");
      }
    }
    for (const auto result : type.results) {
      if (!valid_index(result, program.types)) {
        add_error(diagnostics, {1, 1}, stage, "function type references an invalid result type");
      }
    }
    const bool has_function_payload = !type.parameters.empty() || !type.results.empty();
    switch (type.kind) {
      case TypeKind::scalar:
        if (type.value_type == ValueType::list || type.value_type == ValueType::tuple ||
            type.value_type == ValueType::function || !type.elements.empty() ||
            has_function_payload || type.referent.valid()) {
          add_error(diagnostics, {1, 1}, stage, "scalar type has composite payload");
        }
        break;
      case TypeKind::sequence:
        if (type.value_type != ValueType::list || !type.elements.empty() || has_function_payload ||
            type.referent.valid()) {
          add_error(diagnostics, {1, 1}, stage, "sequence type has an invalid payload");
        }
        break;
      case TypeKind::tuple:
        if (type.value_type != ValueType::tuple || has_function_payload || type.referent.valid()) {
          add_error(diagnostics, {1, 1}, stage, "tuple type has an invalid payload");
        }
        break;
      case TypeKind::function:
        if (type.value_type != ValueType::function || !type.elements.empty() ||
            type.referent.valid()) {
          add_error(diagnostics, {1, 1}, stage, "function type has an invalid payload");
        }
        break;
      case TypeKind::reference:
        if (!valid_index(type.referent, program.types) || type.referent.value() == index ||
            type.reference_intent == ParameterIntent::none || !type.elements.empty() ||
            has_function_payload) {
          add_error(diagnostics, {1, 1}, stage, "reference type has an invalid referent or mode");
        }
        break;
    }
  }
  for (std::size_t index = 1; index < program.shapes.size(); ++index) {
    const auto& shape = program.shapes[index];
    if (!shape.dynamic_rank && shape.extents.size() != shape.strides.size()) {
      add_error(diagnostics, {1, 1}, stage,
                "shape extent and canonical stride ranks are inconsistent");
    }
  }
  for (std::size_t index = 1; index < program.storages.size(); ++index) {
    const auto& storage = program.storages[index];
    if (storage.name.empty() || !valid_index(storage.type, program.types) ||
        !valid_index(storage.shape, program.shapes)) {
      add_error(diagnostics, {1, 1}, stage, "storage has invalid name, type, or shape metadata");
    }
    if (storage.kind == StorageKind::view) {
      if (!valid_index(storage.base, program.storages) || storage.base.value() == index) {
        add_error(diagnostics, {1, 1}, stage, "view storage has an invalid base storage");
      }
    } else if (storage.base.valid()) {
      add_error(diagnostics, {1, 1}, stage, "non-view storage unexpectedly has a base storage");
    }
    if (storage.intent == ParameterIntent::in && storage.writable) {
      add_error(diagnostics, {1, 1}, stage, "intent-in storage must not be writable");
    }
  }
  std::unordered_map<std::uint64_t, AliasClass> alias_pairs;
  for (const auto& relation : program.aliases) {
    if (!valid_index(relation.left, program.storages) ||
        !valid_index(relation.right, program.storages) || relation.left == relation.right ||
        relation.relation == AliasClass::no_alias) {
      add_error(diagnostics, {1, 1}, stage, "alias relation is invalid or redundant");
      continue;
    }
    const auto low = std::min(relation.left.value(), relation.right.value());
    const auto high = std::max(relation.left.value(), relation.right.value());
    const auto key = (static_cast<std::uint64_t>(low) << 32U) | high;
    if (!alias_pairs.emplace(key, relation.relation).second) {
      add_error(diagnostics, {1, 1}, stage, "alias relation pair is duplicated");
    }
  }
  for (std::size_t index = 1; index < program.instructions.size(); ++index) {
    const auto& instruction = program.instructions[index];
    if (instruction.id.value() != index || instruction.opcode == Opcode::invalid ||
        !instruction.origin.valid()) {
      add_error(diagnostics, instruction.location, stage,
                "instruction table is not dense or has an invalid opcode/origin");
    }
    if (instruction.result.valid() && !valid_index(instruction.type, program.types)) {
      add_error(diagnostics, instruction.location, stage,
                "value-producing instruction has an invalid type");
    }
    if (instruction.result.valid() && !valid_index(instruction.shape, program.shapes)) {
      add_error(diagnostics, instruction.location, stage,
                "value-producing instruction has an invalid shape");
    }
    if (instruction.opcode >= Opcode::literal && instruction.opcode <= Opcode::aggregate &&
        !instruction.result.valid()) {
      add_error(diagnostics, instruction.location, stage,
                "expression instruction does not define a value");
    }
    for (const auto operand : instruction.operands) {
      if (!operand.valid()) {
        add_error(diagnostics, instruction.location, stage,
                  "instruction has an invalid value operand");
      }
    }
  }
  verify_cfg(program, diagnostics, stage);
  verify_function_types_and_calls(program, diagnostics, stage);
  verify_statements(program.statements, program, diagnostics, stage);
  return diagnostics;
}

}  // namespace mpf::detail::mir
