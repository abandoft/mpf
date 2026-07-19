#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "compiler/assignment_pattern.hpp"
#include "compiler/call_contract.hpp"
#include "hir.hpp"
#include "ids.hpp"
#include "semantic_table.hpp"
#include "storage_region.hpp"

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
  load,
  unary,
  binary,
  compare,
  comparison_chain,
  conditional,
  truthiness,
  call,
  member,
  index,
  slice,
  aggregate,
  allocate,
  store,
  store_indexed,
  copy,
  writeback,
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
  NumericType numeric_type{unknown_numeric_type};
  NumericType element_numeric_type{unknown_numeric_type};
  ArrayStorageFormat array_storage{ArrayStorageFormat::none};
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
  bool optional{false};
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
  ArgumentTransfer transfer{ArgumentTransfer::value};
  ComparisonOperator comparison{ComparisonOperator::none};
  semantic::Truthiness truthiness{semantic::Truthiness::native};
  SourceLocation location{};
  ValueId result{};
  TypeId type{};
  ShapeId shape{};
  StorageId storage{};
  std::size_t result_index{dynamic_extent};
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
  bool exported{false};
  BlockId entry{};
  std::vector<BlockId> blocks;
  std::vector<TypeId> parameter_types;
  std::vector<ShapeId> parameter_shapes;
  std::vector<bool> parameter_optional;
  std::vector<TypeId> result_types;
  std::vector<ShapeId> result_shapes;
  TypeId signature{};
};

struct CallSite {
  InstructionId instruction{};
  HirNodeId origin{};
  MirFunctionId caller{};
  MirFunctionId callee{};
  struct Argument {
    TypeId type{};
    StorageId storage{};
    StorageId root{};
    ParameterIntent intent{ParameterIntent::none};
    ArgumentTransfer transfer{ArgumentTransfer::value};
    StorageViewKind view{StorageViewKind::none};
    StorageLifetime lifetime{StorageLifetime::expression};
    StorageRegion region;
    bool writable{false};
  };

  std::vector<Argument> arguments;
  TypeId result_type{};
  std::size_t requested_results{1};
};

struct Expression {
  MirExpressionId id{};
  InstructionId instruction{};
  HirNodeId origin{};
  SourceLocation location{};
  ExpressionKind kind{ExpressionKind::invalid};
  std::vector<MirExpressionId> children;
  ValueId value_id{};
  TypeId type_id{};
  ShapeId shape_id{};
  StorageId storage_id{};
  SymbolId symbol_id{};
  bool retired{false};

  [[nodiscard]] bool valid() const noexcept { return kind != ExpressionKind::invalid; }
};

struct CaseSelector {
  MirExpressionId lower{};
  bool has_lower{false};
  MirExpressionId upper{};
  bool has_upper{false};
  bool range{false};
};

struct Statement {
  MirStatementId id{};
  InstructionId instruction{};
  HirNodeId origin{};
  StatementKind kind{StatementKind::expression};
  std::size_t line{1};
  std::string name;
  SymbolId symbol_id{};
  MirExpressionId expression{};
  bool has_expression{false};
  MirExpressionId secondary_expression{};
  bool has_secondary_expression{false};
  MirExpressionId tertiary_expression{};
  bool has_tertiary_expression{false};
  MirExpressionId target_expression{};
  bool has_target_expression{false};
  std::vector<std::string> parameters;
  std::vector<SymbolId> parameter_symbols;
  std::vector<ParameterKind> parameter_kinds;
  std::vector<MirExpressionId> parameter_defaults;
  std::vector<std::string> return_names;
  std::vector<SymbolId> return_symbols;
  std::vector<std::string> target_names;
  std::vector<SymbolId> target_symbols;
  bool has_target_pattern{false};
  std::vector<CaseSelector> case_selectors;
  bool default_case{false};
  std::vector<MirStatementId> body;
  std::vector<MirStatementId> alternative;
};

struct ValueMetadata {
  TypeId type{};
  ShapeId shape{};
  bool sequence{false};
  bool list_sequence{false};
  std::vector<ValueMetadata> elements;
};

struct AssignmentPattern {
  AssignmentPatternKind kind{AssignmentPatternKind::invalid};
  SourceLocation location{};
  std::string name;
  std::vector<AssignmentPattern> children;
  TypeId type{};
  ShapeId shape{};
  TypeId previous_type{};
  std::vector<AssignmentAccess> access_path;
  std::vector<std::vector<AssignmentAccess>> captured_paths;

  [[nodiscard]] bool valid() const noexcept { return kind != AssignmentPatternKind::invalid; }
};

struct BroadcastPlan {
  bool valid{false};
  semantic::BroadcastShapeSource shape_source{semantic::BroadcastShapeSource::static_extents};
  ShapeId left_shape{};
  ShapeId right_shape{};
  ShapeId result_shape{};
  std::vector<semantic::BroadcastAxis> axes;
};

struct SparseElementwisePlan {
  semantic::SparseElementwiseOperation operation{semantic::SparseElementwiseOperation::none};
  semantic::SparseElementwiseStoragePolicy storage_policy{
      semantic::SparseElementwiseStoragePolicy::none};
  semantic::BroadcastShapeSource shape_source{semantic::BroadcastShapeSource::static_extents};
  ArrayStorageFormat left_storage{ArrayStorageFormat::none};
  ArrayStorageFormat right_storage{ArrayStorageFormat::none};
  ArrayStorageFormat result_storage{ArrayStorageFormat::none};
  ShapeId left_shape{};
  ShapeId right_shape{};
  ShapeId result_shape{};
  std::vector<semantic::BroadcastAxis> axes;

  [[nodiscard]] bool valid() const noexcept {
    return operation != semantic::SparseElementwiseOperation::none;
  }
};

struct MatrixOperationPlan {
  semantic::MatrixOperation operation{semantic::MatrixOperation::none};
  semantic::MatrixSolveKind solve{semantic::MatrixSolveKind::none};
  semantic::MatrixNumericDomain numeric_domain{semantic::MatrixNumericDomain::none};
  semantic::MatrixConditionPolicy condition_policy{semantic::MatrixConditionPolicy::none};
  semantic::MatrixFactorizationPolicy factorization_policy{
      semantic::MatrixFactorizationPolicy::none};
  semantic::MatrixStructurePolicy structure_policy{semantic::MatrixStructurePolicy::none};
  semantic::MatrixStoragePolicy storage_policy{semantic::MatrixStoragePolicy::none};
  ArrayStorageFormat left_storage{ArrayStorageFormat::none};
  ArrayStorageFormat right_storage{ArrayStorageFormat::none};
  ArrayStorageFormat result_storage{ArrayStorageFormat::none};
  ShapeId left_shape{};
  ShapeId right_shape{};
  ShapeId result_shape{};

  [[nodiscard]] bool valid() const noexcept { return operation != semantic::MatrixOperation::none; }
};

struct ReductionPlan {
  semantic::ReductionOperation operation{semantic::ReductionOperation::none};
  semantic::ReductionAxisPolicy axis_policy{semantic::ReductionAxisPolicy::none};
  semantic::ReductionShapeSource shape_source{semantic::ReductionShapeSource::static_extents};
  ShapeId input_shape{};
  ShapeId result_shape{};
  ShapeId output_shape{};
  std::vector<std::size_t> axes;
  bool scalar_result{false};

  [[nodiscard]] bool valid() const noexcept {
    return operation != semantic::ReductionOperation::none;
  }
};

struct SparseConstructionPlan {
  semantic::SparseConstructionKind kind{semantic::SparseConstructionKind::none};
  ShapeId result_shape{};
  std::vector<std::size_t> triplet_element_counts;
  std::size_t reserve_hint{0U};

  [[nodiscard]] bool valid() const noexcept {
    return kind != semantic::SparseConstructionKind::none;
  }
};

struct SparseIndexPlan {
  semantic::SparseIndexKind kind{semantic::SparseIndexKind::none};
  ArrayStorageFormat source_storage{ArrayStorageFormat::none};
  ArrayStorageFormat result_storage{ArrayStorageFormat::none};
  ShapeId input_shape{};
  ShapeId result_shape{};

  [[nodiscard]] bool valid() const noexcept { return kind != semantic::SparseIndexKind::none; }
};

struct SparseReshapePlan {
  semantic::SparseReshapeKind kind{semantic::SparseReshapeKind::none};
  semantic::SparseReshapeDimensionForm dimension_form{semantic::SparseReshapeDimensionForm::none};
  semantic::SparseReshapeInference inference{semantic::SparseReshapeInference::none};
  std::size_t inferred_axis{0U};
  ArrayStorageFormat source_storage{ArrayStorageFormat::none};
  ArrayStorageFormat result_storage{ArrayStorageFormat::none};
  ShapeId input_shape{};
  ShapeId requested_shape{};
  ShapeId result_shape{};

  [[nodiscard]] bool valid() const noexcept { return kind != semantic::SparseReshapeKind::none; }
};

struct ExpressionAttributes {
  MirExpressionId origin{};
  std::string spelling;
  UnaryOperator unary_operation{UnaryOperator::none};
  BinaryOperator operation{BinaryOperator::none};
  ComparisonOperator comparison{ComparisonOperator::none};
  std::vector<ComparisonOperator> comparisons;
  semantic::LogicalEvaluation logical_evaluation{semantic::LogicalEvaluation::none};
  semantic::ArrayOperation array_operation{semantic::ArrayOperation::native};
  BroadcastPlan broadcast;
  SparseElementwisePlan sparse_elementwise;
  MatrixOperationPlan matrix_operation;
  ReductionPlan reduction;
  SparseConstructionPlan sparse_construction;
  SparseIndexPlan sparse_index;
  SparseReshapePlan sparse_reshape;
  BindingKind binding{BindingKind::unresolved};
  IntrinsicId intrinsic{IntrinsicId::none};
  std::vector<ShapeId> tuple_shapes;
  std::vector<ValueMetadata> sequence_elements;
  std::size_t requested_results{1};
  bool multi_result_call{false};
  bool procedure_has_result{false};
  std::size_t index_base{0};
  bool allow_negative_index{false};
  bool slice_stop_inclusive{false};
  semantic::IndexExtentSource index_extent{semantic::IndexExtentSource::none};
  std::vector<semantic::IndexSelectorKind> index_selectors;
  std::vector<semantic::IndexExtentSource> index_extents;
  bool lazy_cfg{false};
  StorageRegion storage_region;
};

struct TargetAttributes {
  TypeId type{};
  ShapeId shape{};
  TypeId previous_type{};
  StorageId storage{};
};

struct IndexedMutationPlan {
  semantic::IndexedMutationContract contract;
  ShapeId input_shape{};
  ShapeId result_shape{};
};

struct SparseMutationPlan {
  semantic::SparseMutationKind kind{semantic::SparseMutationKind::none};
  semantic::SparseReplacementKind replacement{semantic::SparseReplacementKind::none};
  semantic::SparseDuplicateWritePolicy duplicate_policy{semantic::SparseDuplicateWritePolicy::none};
  semantic::SparseZeroWritePolicy zero_policy{semantic::SparseZeroWritePolicy::none};
  ArrayStorageFormat source_storage{ArrayStorageFormat::none};
  ArrayStorageFormat replacement_storage{ArrayStorageFormat::none};
  ArrayStorageFormat result_storage{ArrayStorageFormat::none};
  ShapeId input_shape{};
  ShapeId selection_shape{};
  ShapeId replacement_shape{};
  ShapeId result_shape{};

  [[nodiscard]] bool valid() const noexcept { return kind != semantic::SparseMutationKind::none; }
};

struct StatementAttributes {
  MirStatementId origin{};
  bool procedure_call{false};
  bool inclusive_stop{false};
  bool retain_last_loop_value{true};
  TypeId previous_type{};
  AssignmentPattern target_pattern;
  std::vector<TargetAttributes> targets;
  IndexedMutationPlan indexed_mutation;
  SparseMutationPlan sparse_mutation;
};

enum class MemoryAccessMode : std::uint8_t { none, read, write, read_write };

[[nodiscard]] constexpr bool memory_access_reads(const MemoryAccessMode mode) noexcept {
  return mode == MemoryAccessMode::read || mode == MemoryAccessMode::read_write;
}

[[nodiscard]] constexpr bool memory_access_writes(const MemoryAccessMode mode) noexcept {
  return mode == MemoryAccessMode::write || mode == MemoryAccessMode::read_write;
}

struct MemoryAccess {
  StorageId storage{};
  StorageId root{};
  StorageRegion region;
  MemoryAccessMode mode{MemoryAccessMode::none};
};

[[nodiscard]] inline bool operator==(const MemoryAccess& left, const MemoryAccess& right) noexcept {
  return left.storage == right.storage && left.root == right.root && left.region == right.region &&
         left.mode == right.mode;
}

[[nodiscard]] inline bool operator!=(const MemoryAccess& left, const MemoryAccess& right) noexcept {
  return !(left == right);
}

struct InstructionAttributes {
  InstructionId origin{};
  std::vector<MemoryAccess> memory_accesses;
};

struct OperationAttributeTable {
  std::uint64_t mir_revision{0};
  std::size_t expression_count{0};
  std::size_t statement_count{0};
  std::size_t instruction_count{0};
  std::vector<ExpressionAttributes> expressions;
  std::vector<StatementAttributes> statements;
  std::vector<InstructionAttributes> instructions;
};

struct Program {
  SourceLanguage source_language{SourceLanguage::automatic};
  semantic::Profile semantics{};
  std::vector<Expression> expressions;
  std::vector<Statement> statements;
  std::vector<MirStatementId> roots;
  OperationAttributeTable attributes;
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

[[nodiscard]] const Expression* expression(const Program& program, MirExpressionId id) noexcept;
[[nodiscard]] Expression* expression(Program& program, MirExpressionId id) noexcept;
[[nodiscard]] const Statement* statement(const Program& program, MirStatementId id) noexcept;
[[nodiscard]] Statement* statement(Program& program, MirStatementId id) noexcept;
[[nodiscard]] const ExpressionAttributes* attributes(const Program& program,
                                                     MirExpressionId id) noexcept;
[[nodiscard]] ExpressionAttributes* attributes(Program& program, MirExpressionId id) noexcept;
[[nodiscard]] const StatementAttributes* attributes(const Program& program,
                                                    MirStatementId id) noexcept;
[[nodiscard]] StatementAttributes* attributes(Program& program, MirStatementId id) noexcept;
[[nodiscard]] const InstructionAttributes* attributes(const Program& program,
                                                      InstructionId id) noexcept;
[[nodiscard]] InstructionAttributes* attributes(Program& program, InstructionId id) noexcept;
[[nodiscard]] const TypeData* type(const Program& program, TypeId id) noexcept;
[[nodiscard]] const ShapeData* shape(const Program& program, ShapeId id) noexcept;
[[nodiscard]] ValueType value_type(const Program& program, TypeId id) noexcept;
[[nodiscard]] ValueType element_type(const Program& program, TypeId id) noexcept;
[[nodiscard]] NumericType numeric_type(const Program& program, TypeId id) noexcept;
[[nodiscard]] NumericType element_numeric_type(const Program& program, TypeId id) noexcept;
[[nodiscard]] ArrayStorageFormat array_storage(const Program& program, TypeId id) noexcept;
[[nodiscard]] bool column_major(const Program& program, ShapeId id) noexcept;

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
  std::vector<MemoryAccess> memory_accesses;
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

struct CallArgumentEffectFacts {
  std::uint32_t ordinal{0};
  StorageId storage{};
  StorageId root{};
  ArgumentTransfer transfer{ArgumentTransfer::value};
  StorageRegion region;
  bool reads{false};
  bool writes{false};
  bool escapes{false};
};

struct CallOverlapFacts {
  std::uint32_t left{0};
  std::uint32_t right{0};
  AliasClass relation{AliasClass::no_alias};
  bool writable_conflict{false};
};

struct CallEffectFacts {
  InstructionId instruction{};
  MirFunctionId caller{};
  MirFunctionId callee{};
  Effect effects{Effect::none};
  std::vector<StorageId> reads;
  std::vector<StorageId> writes;
  std::vector<CallArgumentEffectFacts> arguments;
  std::vector<CallOverlapFacts> overlaps;
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
[[nodiscard]] AliasClass alias_between(const AliasEffectTable& analysis, const MemoryAccess& left,
                                       const MemoryAccess& right) noexcept;
[[nodiscard]] bool memory_accesses_conflict(const AliasEffectTable& analysis,
                                            const MemoryAccess& left,
                                            const MemoryAccess& right) noexcept;
[[nodiscard]] std::vector<Diagnostic> verify(const Program& program, std::string_view stage);

}  // namespace mpf::detail::mir
