#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "../compiler/assignment_pattern.hpp"
#include "hir.hpp"
#include "ids.hpp"
#include "semantic_table.hpp"

namespace mpf::detail {
struct NameTable;
}

namespace mpf::detail::mir {

class Effect final {
 public:
  constexpr Effect() noexcept = default;
  explicit constexpr Effect(const std::uint16_t bits) noexcept : bits_(bits) {}

  [[nodiscard]] constexpr std::uint16_t bits() const noexcept { return bits_; }

  static const Effect none;
  static const Effect read;
  static const Effect write;
  static const Effect allocate;
  static const Effect io;
  static const Effect may_fail;
  static const Effect control;
  static const Effect external_unknown;

 private:
  std::uint16_t bits_{0};
};

inline constexpr Effect Effect::none{0};
inline constexpr Effect Effect::read{1U << 0U};
inline constexpr Effect Effect::write{1U << 1U};
inline constexpr Effect Effect::allocate{1U << 2U};
inline constexpr Effect Effect::io{1U << 3U};
inline constexpr Effect Effect::may_fail{1U << 4U};
inline constexpr Effect Effect::control{1U << 5U};
inline constexpr Effect Effect::external_unknown{1U << 6U};

constexpr Effect operator|(const Effect left, const Effect right) noexcept {
  return Effect{static_cast<std::uint16_t>(left.bits() | right.bits())};
}

constexpr Effect& operator|=(Effect& left, const Effect right) noexcept {
  left = left | right;
  return left;
}

[[nodiscard]] constexpr bool has_effect(const Effect set, const Effect effect) noexcept {
  return (set.bits() & effect.bits()) != 0;
}

enum class Opcode {
  invalid,
  literal,
  identifier,
  unary,
  binary,
  comparison_chain,
  conditional,
  call,
  member,
  index,
  slice,
  aggregate,
  assignment,
  indexed_assignment,
  output,
  return_value,
  expression,
  selection,
  loop,
  function,
  control
};

enum class TerminatorKind { none, branch, conditional_branch, return_value, unreachable };
enum class AliasClass { no_alias, may_alias, must_alias };
enum class StorageKind { local, parameter, global, temporary, view };
enum class StorageLifetime { function, module, borrowed, expression };
enum class StorageViewKind { none, element, section };
enum class TypeKind { scalar, sequence, tuple, function, reference };

struct TypeData {
  TypeKind kind{TypeKind::scalar};
  ValueType value_type{ValueType::unknown};
  ValueType element_type{ValueType::unknown};
  std::vector<TypeId> elements;
  std::vector<TypeId> parameters;
  std::vector<TypeId> results;
  TypeId referent{};
  ParameterIntent reference_intent{ParameterIntent::none};
};

struct ShapeData {
  std::vector<std::size_t> extents;
  std::vector<std::size_t> strides;
  semantic::IndexLayout layout{semantic::IndexLayout::row_major};
  bool dynamic_rank{false};
};

struct StorageData {
  std::string name;
  SymbolId symbol{};
  HirNodeId origin{};
  TypeId type{};
  ShapeId shape{};
  bool writable{true};
  StorageKind kind{StorageKind::local};
  StorageLifetime lifetime{StorageLifetime::function};
  StorageId base{};
  StorageViewKind view{StorageViewKind::none};
  ParameterIntent intent{ParameterIntent::none};
};

struct AliasRelation {
  StorageId left{};
  StorageId right{};
  AliasClass relation{AliasClass::no_alias};
  HirNodeId origin{};
};

struct Instruction {
  InstructionId id{};
  Opcode opcode{Opcode::invalid};
  HirNodeId origin{};
  MirFunctionId callee{};
  IntrinsicId intrinsic{IntrinsicId::none};
  SourceLocation location{};
  ValueId result{};
  TypeId type{};
  ShapeId shape{};
  StorageId storage{};
  std::vector<ValueId> operands;
};

struct Terminator {
  TerminatorKind kind{TerminatorKind::none};
  HirNodeId origin{};
  std::vector<ValueId> operands;
  std::vector<BlockId> successors;
  std::vector<std::vector<ValueId>> successor_arguments;
};

struct BlockArgument {
  ValueId value{};
  TypeId type{};
  ShapeId shape{};
  StorageId storage{};
};

struct BasicBlock {
  BlockId id{};
  std::vector<BlockArgument> arguments;
  std::vector<InstructionId> instructions;
  Terminator terminator;
};

struct Function {
  MirFunctionId id{};
  HirNodeId origin{};
  SymbolId symbol{};
  std::string name;
  BlockId entry{};
  std::vector<BlockId> blocks;
  std::vector<TypeId> parameter_types;
  std::vector<bool> parameter_optional;
  std::vector<TypeId> result_types;
  TypeId signature{};
};

struct CallSite {
  InstructionId instruction{};
  HirNodeId origin{};
  MirFunctionId caller{};
  MirFunctionId callee{};
  std::vector<TypeId> argument_types;
  std::vector<StorageId> argument_storages;
  std::vector<bool> argument_omitted;
  TypeId result_type{};
  std::size_t requested_results{1};
};

struct Expression {
  HirNodeId origin{};
  SourceLocation location{};
  ExpressionKind kind{ExpressionKind::invalid};
  std::string value;
  std::vector<std::string> operators;
  std::vector<Expression> children;
  ValueType inferred_type{ValueType::unknown};
  BindingKind binding{BindingKind::unresolved};
  IntrinsicId intrinsic{IntrinsicId::none};
  ValueType element_type{ValueType::unknown};
  std::vector<std::size_t> shape;
  std::vector<ValueType> tuple_types;
  std::vector<ValueType> tuple_element_types;
  std::vector<std::vector<std::size_t>> tuple_shapes;
  bool sequence_is_list{false};
  std::vector<ValueMetadata> sequence_elements;
  std::size_t requested_outputs{1};
  bool multi_output_call{false};
  std::vector<ParameterIntent> argument_intents;
  std::vector<std::string> argument_names;
  std::vector<bool> argument_optional_forward;
  bool procedure_has_result{false};
  std::size_t index_base{0};
  bool allow_negative_index{false};
  bool column_major{false};
  bool slice_stop_inclusive{false};
  ValueId value_id{};
  TypeId type_id{};
  ShapeId shape_id{};
  StorageId storage_id{};
  SymbolId symbol_id{};

  [[nodiscard]] bool valid() const noexcept { return kind != ExpressionKind::invalid; }
};

struct CaseSelector {
  Expression lower;
  bool has_lower{false};
  Expression upper;
  bool has_upper{false};
  bool range{false};
};

struct Statement {
  HirNodeId origin{};
  StatementKind kind{StatementKind::expression};
  std::size_t line{1};
  std::string name;
  SymbolId symbol_id{};
  Expression expression;
  bool has_expression{false};
  bool procedure_call{false};
  Expression secondary_expression;
  bool has_secondary_expression{false};
  Expression tertiary_expression;
  bool has_tertiary_expression{false};
  bool inclusive_stop{false};
  bool retain_last_loop_value{true};
  ValueType declared_type{ValueType::unknown};
  ValueType element_type{ValueType::unknown};
  ValueType previous_type{ValueType::unknown};
  ValueType previous_element_type{ValueType::unknown};
  ParameterIntent parameter_intent{ParameterIntent::none};
  bool optional_parameter{false};
  bool dummy_parameter{false};
  std::vector<std::size_t> shape;
  std::size_t index_base{0};
  bool allow_negative_index{false};
  Expression target_expression;
  bool has_target_expression{false};
  std::vector<std::string> parameters;
  std::vector<SymbolId> parameter_symbols;
  std::vector<ParameterKind> parameter_kinds;
  std::vector<Expression> parameter_defaults;
  std::vector<ParameterIntent> parameter_intents;
  std::vector<bool> parameter_optional;
  std::vector<ValueType> parameter_types;
  std::vector<ValueType> parameter_element_types;
  std::vector<std::vector<std::size_t>> parameter_shapes;
  std::vector<std::string> return_names;
  bool has_value_return{false};
  std::vector<ValueType> return_types;
  std::vector<ValueType> return_element_types;
  std::vector<std::vector<std::size_t>> return_shapes;
  bool return_sequence_is_list{false};
  std::vector<ValueMetadata> return_sequence_elements;
  std::vector<std::string> target_names;
  AssignmentPattern target_pattern;
  bool has_target_pattern{false};
  std::vector<ValueType> target_types;
  std::vector<ValueType> target_element_types;
  std::vector<std::vector<std::size_t>> target_shapes;
  std::vector<ValueType> target_previous_types;
  std::vector<ValueType> target_previous_element_types;
  std::vector<CaseSelector> case_selectors;
  bool default_case{false};
  std::vector<Statement> body;
  std::vector<Statement> alternative;
};

struct Program {
  SourceLanguage source_language{SourceLanguage::automatic};
  semantic::Profile semantics{};
  std::vector<Statement> statements;
  std::vector<TypeData> types;
  std::vector<ShapeData> shapes;
  std::vector<StorageData> storages;
  std::vector<Instruction> instructions;
  std::vector<BasicBlock> blocks;
  std::vector<Function> functions;
  std::vector<CallSite> calls;
  std::size_t hir_node_count{0};
  std::uint64_t revision{0};
};

struct StorageAliasFacts {
  StorageId origin{};
  StorageId root{};
  StorageKind root_kind{StorageKind::temporary};
  bool escapes{false};
};

struct InstructionEffectFacts {
  InstructionId origin{};
  Effect local{Effect::none};
  Effect effects{Effect::none};
  std::vector<StorageId> reads;
  std::vector<StorageId> writes;
  bool reads_unknown{false};
  bool writes_unknown{false};
};

struct FunctionEffectFacts {
  MirFunctionId origin{};
  Effect effects{Effect::none};
  std::vector<bool> parameter_reads;
  std::vector<bool> parameter_writes;
  std::vector<bool> parameter_escapes;
  bool reads_unknown{false};
  bool writes_unknown{false};
};

struct CallEffectFacts {
  InstructionId instruction{};
  MirFunctionId caller{};
  MirFunctionId callee{};
  Effect effects{Effect::none};
  std::vector<StorageId> reads;
  std::vector<StorageId> writes;
  bool reads_unknown{false};
  bool writes_unknown{false};
};

struct AliasEffectTable {
  std::uint64_t mir_revision{0};
  std::size_t storage_count{0};
  std::size_t instruction_count{0};
  std::size_t function_count{0};
  std::size_t call_count{0};
  std::vector<StorageAliasFacts> storages;
  std::vector<AliasRelation> aliases;
  std::vector<InstructionEffectFacts> instructions;
  std::vector<FunctionEffectFacts> functions;
  std::vector<CallEffectFacts> calls;

  [[nodiscard]] const StorageAliasFacts* storage(StorageId id) const noexcept;
  [[nodiscard]] const InstructionEffectFacts* instruction(InstructionId id) const noexcept;
  [[nodiscard]] const FunctionEffectFacts* function(MirFunctionId id) const noexcept;
  [[nodiscard]] const CallEffectFacts* call(InstructionId instruction) const noexcept;
};

struct LoweringResult {
  Program program;
  std::vector<Diagnostic> diagnostics;
};

[[nodiscard]] LoweringResult lower_from_hir(hir::Program&& program, hir::SemanticTable&& semantics,
                                            const NameTable& names);
[[nodiscard]] AliasEffectTable analyze_alias_effects(const Program& program);
[[nodiscard]] bool alias_effects_current(const Program& program,
                                         const AliasEffectTable& analysis) noexcept;
[[nodiscard]] std::vector<Diagnostic> verify_alias_effects(const Program& program,
                                                           const AliasEffectTable& analysis,
                                                           std::string_view stage);
[[nodiscard]] AliasClass alias_between(const AliasEffectTable& analysis, StorageId left,
                                       StorageId right) noexcept;
[[nodiscard]] std::vector<Diagnostic> verify(const Program& program, std::string_view stage);

}  // namespace mpf::detail::mir
