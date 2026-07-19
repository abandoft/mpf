#include <cstdint>
#include <string>
#include <vector>

#include "backends/cpp/lir.hpp"
#include "backends/cpp/lir_representation.hpp"
#include "backends/javascript/lir.hpp"
#include "backends/javascript/lir_representation.hpp"
#include "test_framework.hpp"

namespace {

using Storage = mpf::detail::ArrayStorageFormat;
using LogicalOperation = mpf::detail::semantic::SparseLogicalOperation;
using StoragePolicy = mpf::detail::semantic::SparseLogicalStoragePolicy;
using BroadcastAxis = mpf::detail::semantic::BroadcastAxis;

constexpr std::size_t kRows = 2U;
constexpr std::size_t kColumns = 3U;

template <typename Expression>
void configure_operand(Expression& expression, const Storage storage, const char* name) {
  expression.kind = mpf::detail::ExpressionKind::identifier;
  expression.value = name;
  expression.inferred_type = mpf::detail::ValueType::list;
  expression.element_type = mpf::detail::ValueType::boolean;
  expression.element_numeric_type = mpf::detail::logical_numeric_type;
  expression.array_storage = storage;
  expression.shape = {kRows, kColumns};
}

template <typename Statement>
void configure_not(Statement& statement) {
  auto& expression = statement.expression;
  expression.kind = mpf::detail::ExpressionKind::unary;
  expression.value = "~";
  expression.unary_operation = mpf::detail::UnaryOperator::logical_not;
  expression.inferred_type = mpf::detail::ValueType::list;
  expression.element_type = mpf::detail::ValueType::boolean;
  expression.element_numeric_type = mpf::detail::logical_numeric_type;
  expression.array_storage = Storage::sparse_csc;
  expression.shape = {kRows, kColumns};
  expression.logical_evaluation = mpf::detail::semantic::LogicalEvaluation::eager_elementwise;
  expression.sparse_logical = {LogicalOperation::logical_not,
                               StoragePolicy::preserve_sparse,
                               mpf::detail::semantic::BroadcastShapeSource::static_extents,
                               Storage::sparse_csc,
                               Storage::none,
                               Storage::sparse_csc,
                               {kRows, kColumns},
                               {},
                               {kRows, kColumns},
                               {}};
  expression.children.resize(1U);
  configure_operand(expression.children[0], Storage::sparse_csc, "sparse_value");
}

template <typename Statement>
void configure_binary(Statement& statement, const LogicalOperation operation,
                      const Storage right_storage, const Storage result_storage,
                      const StoragePolicy storage_policy) {
  auto& expression = statement.expression;
  expression.kind = mpf::detail::ExpressionKind::binary;
  expression.value = operation == LogicalOperation::logical_and ? "&" : "|";
  expression.operation = operation == LogicalOperation::logical_and
                             ? mpf::detail::BinaryOperator::elementwise_logical_and
                             : mpf::detail::BinaryOperator::elementwise_logical_or;
  expression.inferred_type = mpf::detail::ValueType::list;
  expression.element_type = mpf::detail::ValueType::boolean;
  expression.element_numeric_type = mpf::detail::logical_numeric_type;
  expression.array_storage = result_storage;
  expression.shape = {kRows, kColumns};
  expression.array_operation = mpf::detail::semantic::ArrayOperation::matlab;
  expression.logical_evaluation = mpf::detail::semantic::LogicalEvaluation::eager_elementwise;
  expression.sparse_logical = {operation,
                               storage_policy,
                               mpf::detail::semantic::BroadcastShapeSource::static_extents,
                               Storage::sparse_csc,
                               right_storage,
                               result_storage,
                               {kRows, kColumns},
                               {kRows, kColumns},
                               {kRows, kColumns},
                               {BroadcastAxis::match, BroadcastAxis::match}};
  expression.children.resize(2U);
  configure_operand(expression.children[0], Storage::sparse_csc, "sparse_left");
  configure_operand(expression.children[1], right_storage, "right");
}

template <typename Program>
void configure_program(Program& program) {
  program.source_language = mpf::SourceLanguage::matlab;
  program.statements.resize(3U);
  configure_not(program.statements[0]);
  configure_binary(program.statements[1], LogicalOperation::logical_and, Storage::dense,
                   Storage::sparse_csc, StoragePolicy::preserve_sparse);
  configure_binary(program.statements[2], LogicalOperation::logical_or, Storage::dense,
                   Storage::dense, StoragePolicy::materialize_dense);
}

template <typename Program, typename Planner, typename Verifier>
void verify_sparse_logical_plans(Program& program, Planner planner, Verifier verifier,
                                 const std::string& helper_prefix) {
  planner(program);
  std::vector<mpf::Diagnostic> diagnostics;
  verifier(program, diagnostics);
  REQUIRE(diagnostics.empty());

  const std::vector<std::vector<std::size_t>> unary_shapes{{kRows, kColumns}, {kRows, kColumns}};
  const std::vector<std::vector<std::size_t>> binary_shapes{
      {kRows, kColumns}, {kRows, kColumns}, {kRows, kColumns}};
  REQUIRE(program.statements[0].expression.plan.token == helper_prefix + "not");
  REQUIRE(program.statements[1].expression.plan.token == helper_prefix + "and");
  REQUIRE(program.statements[2].expression.plan.token == helper_prefix + "or");
  REQUIRE(program.statements[0].expression.plan.runtime_shape_arguments == unary_shapes);
  REQUIRE(program.statements[1].expression.plan.runtime_shape_arguments == binary_shapes);
  REQUIRE(program.statements[2].expression.plan.runtime_shape_arguments == binary_shapes);

  const std::vector<std::int64_t> and_storage_abi{
      static_cast<std::int64_t>(LogicalOperation::logical_and),
      static_cast<std::int64_t>(StoragePolicy::preserve_sparse),
      static_cast<std::int64_t>(Storage::sparse_csc), static_cast<std::int64_t>(Storage::dense),
      static_cast<std::int64_t>(Storage::sparse_csc)};
  const std::vector<std::int64_t> or_storage_abi{
      static_cast<std::int64_t>(LogicalOperation::logical_or),
      static_cast<std::int64_t>(StoragePolicy::materialize_dense),
      static_cast<std::int64_t>(Storage::sparse_csc), static_cast<std::int64_t>(Storage::dense),
      static_cast<std::int64_t>(Storage::dense)};
  REQUIRE(program.statements[1].expression.plan.runtime_integer_arguments == and_storage_abi);
  REQUIRE(program.statements[2].expression.plan.runtime_integer_arguments == or_storage_abi);

  program.statements[1].expression.plan.token = helper_prefix + "or";
  verifier(program, diagnostics);
  REQUIRE(!diagnostics.empty());

  diagnostics.clear();
  planner(program);
  program.statements[2].expression.plan.runtime_integer_arguments.back() =
      static_cast<std::int64_t>(Storage::sparse_csc);
  verifier(program, diagnostics);
  REQUIRE(!diagnostics.empty());

  diagnostics.clear();
  planner(program);
  program.statements[0].expression.plan.runtime_shape_arguments.back() = {1U, 6U};
  verifier(program, diagnostics);
  REQUIRE(!diagnostics.empty());
}

}  // namespace

TEST_CASE("JavaScript LIR independently owns sparse logical target plans") {
  mpf::detail::javascript::lir::SemanticProgram program;
  configure_program(program);
  verify_sparse_logical_plans(program, mpf::detail::javascript::plan_lir_representation,
                              mpf::detail::javascript::verify_lir_representation,
                              "__mpf_sparse_logical_");
}

TEST_CASE("C++ LIR independently owns sparse logical target plans") {
  mpf::detail::cpp::lir::SemanticProgram program;
  configure_program(program);
  verify_sparse_logical_plans(program, mpf::detail::cpp::plan_lir_representation,
                              mpf::detail::cpp::verify_lir_representation,
                              "mpf_runtime::sparse_logical_");
}
