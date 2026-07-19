#include "dump.hpp"

#include <iomanip>
#include <sstream>
#include <type_traits>

namespace mpf::detail {
namespace {

template <typename Enum>
constexpr auto enum_value(const Enum value) noexcept {
  return +static_cast<std::underlying_type_t<Enum>>(value);
}

void dump_numeric_type(std::ostringstream& output, const NumericType type) {
  output << enum_value(type.value_class) << '/' << enum_value(type.complexity);
}

void dump_hir_expression(std::ostringstream& output, const hir::Expression& expression,
                         const std::size_t depth) {
  if (!expression.valid()) return;
  output << std::string(depth * 2U, ' ') << "expr %h" << expression.id.value()
         << " kind=" << enum_value(expression.kind) << " value=" << std::quoted(expression.value)
         << " @" << expression.location.line << ':' << expression.location.column << '\n';
  for (const auto& child : expression.children) {
    dump_hir_expression(output, child, depth + 1U);
  }
}

void dump_hir_statements(std::ostringstream& output, const std::vector<hir::Statement>& statements,
                         const std::size_t depth) {
  for (const auto& statement : statements) {
    output << std::string(depth * 2U, ' ') << "stmt %h" << statement.id.value()
           << " kind=" << enum_value(statement.kind) << " name=" << std::quoted(statement.name)
           << " line=" << statement.line << '\n';
    dump_hir_expression(output, statement.expression, depth + 1U);
    dump_hir_expression(output, statement.secondary_expression, depth + 1U);
    dump_hir_expression(output, statement.tertiary_expression, depth + 1U);
    dump_hir_expression(output, statement.target_expression, depth + 1U);
    for (const auto& expression : statement.parameter_defaults) {
      dump_hir_expression(output, expression, depth + 1U);
    }
    for (const auto& selector : statement.case_selectors) {
      dump_hir_expression(output, selector.lower, depth + 1U);
      dump_hir_expression(output, selector.upper, depth + 1U);
    }
    dump_hir_statements(output, statement.body, depth + 1U);
    dump_hir_statements(output, statement.alternative, depth + 1U);
  }
}

void dump_normalized_hir_expression(std::ostringstream& output, const hir::Expression& expression,
                                    const std::size_t depth) {
  if (!expression.valid()) return;
  const auto value = expression.comparison == ComparisonOperator::none
                         ? std::string_view(expression.value)
                         : comparison_spelling(expression.comparison);
  output << std::string(depth * 2U, ' ') << "expr kind=" << enum_value(expression.kind)
         << " value=" << std::quoted(std::string(value)) << " operators=[";
  for (std::size_t index = 0; index < expression.comparisons.size(); ++index) {
    if (index != 0) output << ',';
    output << std::quoted(std::string(comparison_spelling(expression.comparisons[index])));
  }
  output << "]\n";
  for (const auto& child : expression.children) {
    dump_normalized_hir_expression(output, child, depth + 1U);
  }
}

void dump_normalized_hir_statements(std::ostringstream& output,
                                    const std::vector<hir::Statement>& statements,
                                    const std::size_t depth) {
  for (const auto& statement : statements) {
    output << std::string(depth * 2U, ' ') << "stmt kind=" << enum_value(statement.kind)
           << " name=" << std::quoted(statement.name) << " parameters=[";
    for (std::size_t index = 0; index < statement.parameters.size(); ++index) {
      if (index != 0) output << ',';
      output << std::quoted(statement.parameters[index]);
    }
    output << "] returns=[";
    for (std::size_t index = 0; index < statement.return_names.size(); ++index) {
      if (index != 0) output << ',';
      output << std::quoted(statement.return_names[index]);
    }
    output << "]\n";
    dump_normalized_hir_expression(output, statement.expression, depth + 1U);
    dump_normalized_hir_expression(output, statement.secondary_expression, depth + 1U);
    dump_normalized_hir_expression(output, statement.tertiary_expression, depth + 1U);
    dump_normalized_hir_expression(output, statement.target_expression, depth + 1U);
    for (const auto& expression : statement.parameter_defaults) {
      dump_normalized_hir_expression(output, expression, depth + 1U);
    }
    dump_normalized_hir_statements(output, statement.body, depth + 1U);
    dump_normalized_hir_statements(output, statement.alternative, depth + 1U);
  }
}

template <typename Id>
void dump_ids(std::ostringstream& output, const std::vector<Id>& ids, const char* prefix) {
  output << '[';
  for (std::size_t index = 0; index < ids.size(); ++index) {
    if (index != 0) output << ',';
    output << prefix << ids[index].value();
  }
  output << ']';
}

void dump_flags(std::ostringstream& output, const std::vector<bool>& flags) {
  output << '[';
  for (std::size_t index = 0; index < flags.size(); ++index) {
    if (index != 0) output << ',';
    output << flags[index];
  }
  output << ']';
}

void dump_storage_region(std::ostringstream& output, const StorageRegion& region) {
  output << "{kind=" << enum_value(region.kind) << " shape=[";
  for (std::size_t index = 0; index < region.root_shape.size(); ++index) {
    if (index != 0) output << ',';
    output << region.root_shape[index];
  }
  output << "] dimensions=[";
  for (std::size_t index = 0; index < region.dimensions.size(); ++index) {
    if (index != 0) output << ',';
    const auto& dimension = region.dimensions[index];
    output << dimension.first << ':' << dimension.stride << ':' << dimension.count;
  }
  output << "]}";
}

void dump_memory_accesses(std::ostringstream& output,
                          const std::vector<mir::MemoryAccess>& accesses) {
  output << '[';
  for (std::size_t index = 0; index < accesses.size(); ++index) {
    if (index != 0) output << ',';
    const auto& access = accesses[index];
    output << "{storage=!m" << access.storage.value() << " root=!m" << access.root.value()
           << " mode=" << enum_value(access.mode) << " region=";
    dump_storage_region(output, access.region);
    output << '}';
  }
  output << ']';
}

}  // namespace

std::string dump_hir(const hir::Program& program) {
  std::ostringstream output;
  output << "hir-v2 language=" << enum_value(program.language) << " nodes=" << program.node_count
         << " revision=" << program.revision << '\n';
  output << "semantics truthiness=" << enum_value(program.semantics.truthiness)
         << " logical-result=" << enum_value(program.semantics.logical_result)
         << " equality=" << enum_value(program.semantics.equality)
         << " division=" << enum_value(program.semantics.division)
         << " division-by-zero=" << enum_value(program.semantics.division_by_zero)
         << " layout=" << enum_value(program.semantics.layout)
         << " top-level=" << enum_value(program.semantics.top_level_storage) << '\n';
  dump_hir_statements(output, program.statements, 0);
  return output.str();
}

std::string dump_normalized_hir(const hir::Program& program) {
  std::ostringstream output;
  output << "normalized-hir-v1\n";
  dump_normalized_hir_statements(output, program.statements, 0);
  return output.str();
}

std::string dump_semantics(const hir::SemanticTable& table) {
  std::ostringstream output;
  output << "semantic-v17 hir-nodes=" << table.hir_node_count
         << " hir-revision=" << table.hir_revision << " expressions=" << table.expressions.size()
         << " statements=" << table.statements.size() << '\n';
  for (std::size_t id = 1; id < table.nodes.size(); ++id) {
    const auto slot = table.nodes[id];
    output << "%h" << id << " kind=" << enum_value(slot.kind) << " offset=" << slot.offset;
    if (slot.kind == hir::SemanticNodeKind::expression && slot.offset < table.expressions.size()) {
      const auto& facts = table.expressions[slot.offset];
      output << " type=" << enum_value(facts.inferred_type) << " numeric=";
      dump_numeric_type(output, facts.numeric_type);
      output << " element=" << enum_value(facts.element_type) << " element-numeric=";
      dump_numeric_type(output, facts.element_numeric_type);
      output << " array-storage=" << enum_value(facts.array_storage);
      output << " binding=" << enum_value(facts.binding)
             << " intrinsic=" << enum_value(facts.intrinsic) << " shape=[";
      for (std::size_t extent = 0; extent < facts.shape.size(); ++extent) {
        if (extent != 0) output << ',';
        output << facts.shape[extent];
      }
      output << "] outputs=" << facts.requested_outputs
             << " logical-evaluation=" << enum_value(facts.logical_evaluation);
      if (!facts.index_selectors.empty()) {
        output << " selectors=[";
        for (std::size_t selector = 0; selector < facts.index_selectors.size(); ++selector) {
          if (selector != 0U) output << ',';
          output << enum_value(facts.index_selectors[selector]);
        }
        output << ']';
      }
      if (!facts.index_extents.empty()) {
        output << " extents=[";
        for (std::size_t selector = 0; selector < facts.index_extents.size(); ++selector) {
          if (selector != 0U) output << ',';
          output << enum_value(facts.index_extents[selector]);
        }
        output << ']';
      }
      if (semantic::requires_runtime_extent(facts.index_extent)) {
        output << " index-extent=" << enum_value(facts.index_extent);
      }
      if (facts.broadcast.valid) {
        const auto dump_shape = [&](const std::vector<std::size_t>& shape) {
          output << '[';
          for (std::size_t axis = 0; axis < shape.size(); ++axis) {
            if (axis != 0U) output << ',';
            output << shape[axis];
          }
          output << ']';
        };
        output << " broadcast="
               << (facts.broadcast.shape_source == semantic::BroadcastShapeSource::runtime_operands
                       ? "runtime:"
                       : "static:");
        dump_shape(facts.broadcast.left_shape);
        output << ',';
        dump_shape(facts.broadcast.right_shape);
        output << "->";
        dump_shape(facts.broadcast.result_shape);
      }
      if (facts.matrix_operation.valid()) {
        const auto dump_shape = [&](const std::vector<std::size_t>& shape) {
          output << '[';
          for (std::size_t axis = 0; axis < shape.size(); ++axis) {
            if (axis != 0U) output << ',';
            output << shape[axis];
          }
          output << ']';
        };
        output << " matrix-operation=" << enum_value(facts.matrix_operation.operation)
               << " solve=" << enum_value(facts.matrix_operation.solve)
               << " numeric-domain=" << enum_value(facts.matrix_operation.numeric_domain)
               << " condition-policy=" << enum_value(facts.matrix_operation.condition_policy)
               << " factorization-policy="
               << enum_value(facts.matrix_operation.factorization_policy)
               << " structure-policy=" << enum_value(facts.matrix_operation.structure_policy)
               << " storage-policy=" << enum_value(facts.matrix_operation.storage_policy)
               << " storage=" << enum_value(facts.matrix_operation.left_storage) << ','
               << enum_value(facts.matrix_operation.right_storage) << "->"
               << enum_value(facts.matrix_operation.result_storage) << ' ';
        dump_shape(facts.matrix_operation.left_shape);
        if (!facts.matrix_operation.right_shape.empty()) {
          output << ',';
          dump_shape(facts.matrix_operation.right_shape);
        }
        output << "->";
        dump_shape(facts.matrix_operation.result_shape);
      }
      if (facts.reduction.valid()) {
        const auto dump_shape = [&](const std::vector<std::size_t>& shape) {
          output << '[';
          for (std::size_t axis = 0; axis < shape.size(); ++axis) {
            if (axis != 0U) output << ',';
            output << shape[axis];
          }
          output << ']';
        };
        output << " reduction=" << enum_value(facts.reduction.operation)
               << " axis-policy=" << enum_value(facts.reduction.axis_policy)
               << " shape-source=" << enum_value(facts.reduction.shape_source)
               << " scalar=" << facts.reduction.scalar_result << ' ';
        dump_shape(facts.reduction.input_shape);
        output << " axes=[";
        for (std::size_t axis = 0; axis < facts.reduction.axes.size(); ++axis) {
          if (axis != 0U) output << ',';
          output << facts.reduction.axes[axis];
        }
        output << "]->";
        dump_shape(facts.reduction.result_shape);
        output << " output=";
        dump_shape(facts.reduction.output_shape);
      }
      if (facts.sparse_construction.valid()) {
        output << " sparse-construction=" << enum_value(facts.sparse_construction.kind)
               << " shape=[";
        for (std::size_t axis = 0; axis < facts.sparse_construction.result_shape.size(); ++axis) {
          if (axis != 0U) output << ',';
          output << facts.sparse_construction.result_shape[axis];
        }
        output << "] counts=[";
        for (std::size_t index = 0; index < facts.sparse_construction.triplet_element_counts.size();
             ++index) {
          if (index != 0U) output << ',';
          output << facts.sparse_construction.triplet_element_counts[index];
        }
        output << "] reserve=" << facts.sparse_construction.reserve_hint;
      }
      if (facts.sparse_index.valid()) {
        output << " sparse-index=" << enum_value(facts.sparse_index.kind) << " input=[";
        for (std::size_t axis = 0; axis < facts.sparse_index.input_shape.size(); ++axis) {
          if (axis != 0U) output << ',';
          output << facts.sparse_index.input_shape[axis];
        }
        output << "] result=[";
        for (std::size_t axis = 0; axis < facts.sparse_index.result_shape.size(); ++axis) {
          if (axis != 0U) output << ',';
          output << facts.sparse_index.result_shape[axis];
        }
        output << ']';
      }
      output << " region=";
      dump_storage_region(output, facts.storage_region);
    } else if (slot.kind == hir::SemanticNodeKind::statement &&
               slot.offset < table.statements.size()) {
      const auto& facts = table.statements[slot.offset];
      output << " declared=" << enum_value(facts.declared_type) << " numeric=";
      dump_numeric_type(output, facts.declared_numeric_type);
      output << " element=" << enum_value(facts.element_type) << " element-numeric=";
      dump_numeric_type(output, facts.element_numeric_type);
      output << " array-storage=" << enum_value(facts.array_storage);
      output << " shape=[";
      for (std::size_t extent = 0; extent < facts.shape.size(); ++extent) {
        if (extent != 0) output << ',';
        output << facts.shape[extent];
      }
      output << "] parameters=" << facts.parameter_types.size()
             << " returns=" << facts.return_types.size() << " targets=" << facts.target_types.size()
             << " exported=" << facts.exported;
      if (facts.indexed_mutation.valid()) {
        output << " mutation=" << enum_value(facts.indexed_mutation.kind)
               << " shape-source=" << enum_value(facts.indexed_mutation.shape_source)
               << " linear=" << facts.indexed_mutation.linear
               << " axis=" << facts.indexed_mutation.axis;
      }
      if (facts.sparse_mutation.valid()) {
        output << " sparse-mutation=" << enum_value(facts.sparse_mutation.kind)
               << " replacement=" << enum_value(facts.sparse_mutation.replacement)
               << " duplicate=" << enum_value(facts.sparse_mutation.duplicate_policy)
               << " zero=" << enum_value(facts.sparse_mutation.zero_policy)
               << " storage=" << enum_value(facts.sparse_mutation.source_storage) << '/'
               << enum_value(facts.sparse_mutation.replacement_storage) << '/'
               << enum_value(facts.sparse_mutation.result_storage);
      }
    }
    output << '\n';
  }
  return output.str();
}

std::string dump_mir(const mir::Program& program) {
  std::ostringstream output;
  output << "mir-v23 language=" << enum_value(program.source_language)
         << " hir-nodes=" << program.hir_node_count
         << " expressions=" << (program.expressions.empty() ? 0U : program.expressions.size() - 1U)
         << " operations=" << (program.statements.empty() ? 0U : program.statements.size() - 1U)
         << " revision=" << program.revision << '\n';
  output << "roots=";
  dump_ids(output, program.roots, "%mstmt");
  output << '\n';
  for (std::size_t index = 1; index < program.expressions.size(); ++index) {
    const auto& expression = program.expressions[index];
    const auto* attributes = mir::attributes(program, expression.id);
    output << "expression %mexpr" << expression.id.value() << " instruction=!i"
           << expression.instruction.value() << " kind=" << enum_value(expression.kind)
           << " retired=" << expression.retired << " spelling="
           << std::quoted(attributes == nullptr ? std::string{} : attributes->spelling)
           << " region=";
    dump_storage_region(output,
                        attributes == nullptr ? StorageRegion{} : attributes->storage_region);
    output << " children=";
    dump_ids(output, expression.children, "%mexpr");
    output << " result=%v" << expression.value_id.value() << " type=!t"
           << expression.type_id.value() << " shape=!s" << expression.shape_id.value()
           << " storage=!m" << expression.storage_id.value() << " origin=%h"
           << expression.origin.value();
    if (attributes != nullptr) {
      output << " binding=" << enum_value(attributes->binding)
             << " intrinsic=" << enum_value(attributes->intrinsic)
             << " comparison=" << enum_value(attributes->comparison)
             << " requested=" << attributes->requested_results
             << " lazy-cfg=" << attributes->lazy_cfg
             << " logical-evaluation=" << enum_value(attributes->logical_evaluation)
             << " tuple-shapes=";
      dump_ids(output, attributes->tuple_shapes, "!s");
      if (attributes->unary_operation != UnaryOperator::none) {
        output << " unary=" << enum_value(attributes->unary_operation);
      }
      if (attributes->array_operation == semantic::ArrayOperation::matlab) {
        output << " matlab-array-operation=1";
      }
      if (!attributes->index_selectors.empty()) {
        output << " selectors=[";
        for (std::size_t selector = 0; selector < attributes->index_selectors.size(); ++selector) {
          if (selector != 0U) output << ',';
          output << enum_value(attributes->index_selectors[selector]);
        }
        output << ']';
      }
      if (!attributes->index_extents.empty()) {
        output << " extents=[";
        for (std::size_t selector = 0; selector < attributes->index_extents.size(); ++selector) {
          if (selector != 0U) output << ',';
          output << enum_value(attributes->index_extents[selector]);
        }
        output << ']';
      }
      if (semantic::requires_runtime_extent(attributes->index_extent)) {
        output << " index-extent=" << enum_value(attributes->index_extent);
      }
      if (attributes->broadcast.valid) {
        output << " broadcast="
               << (attributes->broadcast.shape_source ==
                           semantic::BroadcastShapeSource::runtime_operands
                       ? "runtime:"
                       : "static:")
               << "!s" << attributes->broadcast.left_shape.value() << ",!s"
               << attributes->broadcast.right_shape.value() << "->!s"
               << attributes->broadcast.result_shape.value() << " axes=[";
        for (std::size_t axis = 0; axis < attributes->broadcast.axes.size(); ++axis) {
          if (axis != 0U) output << ',';
          output << enum_value(attributes->broadcast.axes[axis]);
        }
        output << ']';
      }
      if (attributes->matrix_operation.valid()) {
        output << " matrix-operation=" << enum_value(attributes->matrix_operation.operation)
               << " solve=" << enum_value(attributes->matrix_operation.solve)
               << " numeric-domain=" << enum_value(attributes->matrix_operation.numeric_domain)
               << " condition-policy=" << enum_value(attributes->matrix_operation.condition_policy)
               << " factorization-policy="
               << enum_value(attributes->matrix_operation.factorization_policy)
               << " structure-policy=" << enum_value(attributes->matrix_operation.structure_policy)
               << " storage-policy=" << enum_value(attributes->matrix_operation.storage_policy)
               << " storage=" << enum_value(attributes->matrix_operation.left_storage) << ','
               << enum_value(attributes->matrix_operation.right_storage) << "->"
               << enum_value(attributes->matrix_operation.result_storage) << " !s"
               << attributes->matrix_operation.left_shape.value();
        if (attributes->matrix_operation.right_shape.valid()) {
          output << ",!s" << attributes->matrix_operation.right_shape.value();
        }
        output << "->!s" << attributes->matrix_operation.result_shape.value();
      }
      if (attributes->reduction.valid()) {
        output << " reduction=" << enum_value(attributes->reduction.operation)
               << " axis-policy=" << enum_value(attributes->reduction.axis_policy)
               << " shape-source=" << enum_value(attributes->reduction.shape_source)
               << " scalar=" << attributes->reduction.scalar_result << " !s"
               << attributes->reduction.input_shape.value() << " axes=[";
        for (std::size_t axis = 0; axis < attributes->reduction.axes.size(); ++axis) {
          if (axis != 0U) output << ',';
          output << attributes->reduction.axes[axis];
        }
        output << "]->!s" << attributes->reduction.result_shape.value() << " output=!s"
               << attributes->reduction.output_shape.value();
      }
      if (attributes->sparse_construction.valid()) {
        output << " sparse-construction=" << enum_value(attributes->sparse_construction.kind)
               << " shape=!s" << attributes->sparse_construction.result_shape.value()
               << " counts=[";
        for (std::size_t count_index = 0;
             count_index < attributes->sparse_construction.triplet_element_counts.size();
             ++count_index) {
          if (count_index != 0U) output << ',';
          output << attributes->sparse_construction.triplet_element_counts[count_index];
        }
        output << "] reserve=" << attributes->sparse_construction.reserve_hint;
      }
      if (attributes->sparse_index.valid()) {
        output << " sparse-index=" << enum_value(attributes->sparse_index.kind) << " input=!s"
               << attributes->sparse_index.input_shape.value() << " result=!s"
               << attributes->sparse_index.result_shape.value();
      }
    }
    output << '\n';
  }
  for (std::size_t index = 1; index < program.statements.size(); ++index) {
    const auto& statement = program.statements[index];
    const auto* attributes = mir::attributes(program, statement.id);
    output << "operation %mstmt" << statement.id.value() << " instruction=!i"
           << statement.instruction.value() << " kind=" << enum_value(statement.kind)
           << " name=" << std::quoted(statement.name) << " expression=%mexpr"
           << statement.expression.value() << " secondary=%mexpr"
           << statement.secondary_expression.value() << " tertiary=%mexpr"
           << statement.tertiary_expression.value() << " target=%mexpr"
           << statement.target_expression.value() << " body=";
    dump_ids(output, statement.body, "%mstmt");
    output << " alternative=";
    dump_ids(output, statement.alternative, "%mstmt");
    output << " origin=%h" << statement.origin.value();
    if (attributes != nullptr) {
      output << " procedure-call=" << attributes->procedure_call
             << " inclusive-stop=" << attributes->inclusive_stop << " previous=!t"
             << attributes->previous_type.value() << " targets=" << attributes->targets.size();
      if (attributes->indexed_mutation.contract.valid()) {
        output << " mutation=" << enum_value(attributes->indexed_mutation.contract.kind)
               << " shape-source=" << enum_value(attributes->indexed_mutation.contract.shape_source)
               << " linear=" << attributes->indexed_mutation.contract.linear
               << " axis=" << attributes->indexed_mutation.contract.axis << " shape=!s"
               << attributes->indexed_mutation.input_shape.value() << "->!s"
               << attributes->indexed_mutation.result_shape.value();
      }
      if (attributes->sparse_mutation.valid()) {
        output << " sparse-mutation=" << enum_value(attributes->sparse_mutation.kind)
               << " replacement=" << enum_value(attributes->sparse_mutation.replacement)
               << " duplicate=" << enum_value(attributes->sparse_mutation.duplicate_policy)
               << " zero=" << enum_value(attributes->sparse_mutation.zero_policy) << " shape=!s"
               << attributes->sparse_mutation.input_shape.value() << "/!s"
               << attributes->sparse_mutation.selection_shape.value() << "/!s"
               << attributes->sparse_mutation.replacement_shape.value() << "->!s"
               << attributes->sparse_mutation.result_shape.value();
      }
    }
    output << '\n';
  }
  output << "attributes revision=" << program.attributes.mir_revision
         << " expressions=" << program.attributes.expression_count
         << " operations=" << program.attributes.statement_count
         << " instructions=" << program.attributes.instruction_count << '\n';
  for (std::size_t index = 1; index < program.types.size(); ++index) {
    const auto& type = program.types[index];
    output << "type !t" << index << " kind=" << enum_value(type.kind)
           << " value=" << enum_value(type.value_type) << " numeric=";
    dump_numeric_type(output, type.numeric_type);
    output << " element=" << enum_value(type.element_type) << " element-numeric=";
    dump_numeric_type(output, type.element_numeric_type);
    output << " array-storage=" << enum_value(type.array_storage);
    output << " elements=";
    dump_ids(output, type.elements, "!t");
    output << " parameters=";
    dump_ids(output, type.parameters, "!t");
    output << " results=";
    dump_ids(output, type.results, "!t");
    output << " referent=!t" << type.referent.value()
           << " intent=" << enum_value(type.reference_intent) << '\n';
  }
  for (std::size_t index = 1; index < program.shapes.size(); ++index) {
    const auto& shape = program.shapes[index];
    output << "shape !s" << index << " layout=" << enum_value(shape.layout)
           << " dynamic-rank=" << shape.dynamic_rank << " extents=[";
    for (std::size_t extent = 0; extent < shape.extents.size(); ++extent) {
      if (extent != 0) output << ',';
      output << shape.extents[extent];
    }
    output << "] strides=[";
    for (std::size_t stride = 0; stride < shape.strides.size(); ++stride) {
      if (stride != 0) output << ',';
      output << shape.strides[stride];
    }
    output << "]\n";
  }
  for (std::size_t index = 1; index < program.storages.size(); ++index) {
    const auto& storage = program.storages[index];
    output << "storage !m" << index << " name=" << std::quoted(storage.name) << " symbol=$s"
           << storage.symbol.value() << " type=!t" << storage.type.value() << " shape=!s"
           << storage.shape.value() << " writable=" << storage.writable
           << " optional=" << storage.optional << " kind=" << enum_value(storage.kind)
           << " lifetime=" << enum_value(storage.lifetime) << " base=!m" << storage.base.value()
           << " view=" << enum_value(storage.view) << " origin=%h" << storage.origin.value()
           << '\n';
  }
  for (std::size_t index = 1; index < program.functions.size(); ++index) {
    const auto& function = program.functions[index];
    output << "function @f" << function.id.value() << " name=" << std::quoted(function.name)
           << " exported=" << function.exported << " signature=!t" << function.signature.value()
           << " parameters=";
    dump_ids(output, function.parameter_types, "!t");
    output << " parameter-shapes=";
    dump_ids(output, function.parameter_shapes, "!s");
    output << " results=";
    dump_ids(output, function.result_types, "!t");
    output << " result-shapes=";
    dump_ids(output, function.result_shapes, "!s");
    output << " entry=^b" << function.entry.value() << " blocks=";
    dump_ids(output, function.blocks, "^b");
    output << '\n';
    for (const auto block_id : function.blocks) {
      if (!block_id.valid() || block_id.value() >= program.blocks.size()) continue;
      const auto& block = program.blocks[block_id.value()];
      output << "  block ^b" << block.id.value() << " args=[";
      for (std::size_t argument = 0; argument < block.arguments.size(); ++argument) {
        if (argument != 0) output << ',';
        const auto& value = block.arguments[argument];
        output << "%v" << value.value.value() << ":!t" << value.type.value() << ":!s"
               << value.shape.value() << ":!m" << value.storage.value();
      }
      output << "]\n";
      for (const auto instruction_id : block.instructions) {
        if (!instruction_id.valid() || instruction_id.value() >= program.instructions.size()) {
          output << "    invalid-instruction !i" << instruction_id.value() << '\n';
          continue;
        }
        const auto& instruction = program.instructions[instruction_id.value()];
        output << "    !i" << instruction.id.value();
        if (instruction.result.valid()) output << " %v" << instruction.result.value() << " =";
        output << " op" << enum_value(instruction.opcode) << " operands=";
        dump_ids(output, instruction.operands, "%v");
        output << " type=!t" << instruction.type.value() << " shape=!s" << instruction.shape.value()
               << " storage=!m" << instruction.storage.value() << " callee=@f"
               << instruction.callee.value() << " intrinsic=" << enum_value(instruction.intrinsic)
               << " transfer=" << enum_value(instruction.transfer)
               << " comparison=" << enum_value(instruction.comparison)
               << " truthiness=" << enum_value(instruction.truthiness) << " result-index=";
        if (instruction.result_index == dynamic_extent)
          output << '-';
        else
          output << instruction.result_index;
        const auto* instruction_attributes = mir::attributes(program, instruction.id);
        output << " origin=%h" << instruction.origin.value() << " memory-accesses=";
        dump_memory_accesses(output, instruction_attributes == nullptr
                                         ? std::vector<mir::MemoryAccess>{}
                                         : instruction_attributes->memory_accesses);
        output << '\n';
      }
      output << "    terminator op" << enum_value(block.terminator.kind) << " operands=";
      dump_ids(output, block.terminator.operands, "%v");
      output << " successors=";
      dump_ids(output, block.terminator.successors, "^b");
      output << " edge-args=[";
      for (std::size_t successor = 0; successor < block.terminator.successor_arguments.size();
           ++successor) {
        if (successor != 0) output << ',';
        dump_ids(output, block.terminator.successor_arguments[successor], "%v");
      }
      output << ']';
      output << '\n';
    }
  }
  for (const auto& call : program.calls) {
    output << "call !i" << call.instruction.value() << " caller=@f" << call.caller.value()
           << " callee=@f" << call.callee.value() << " arguments=[";
    for (std::size_t index = 0; index < call.arguments.size(); ++index) {
      if (index != 0) output << ',';
      const auto& argument = call.arguments[index];
      output << "{type=!t" << argument.type.value() << " storage=!m" << argument.storage.value()
             << " root=!m" << argument.root.value() << " intent=" << enum_value(argument.intent)
             << " transfer=" << enum_value(argument.transfer)
             << " view=" << enum_value(argument.view)
             << " lifetime=" << enum_value(argument.lifetime) << " writable=" << argument.writable
             << " region=";
      dump_storage_region(output, argument.region);
      output << '}';
    }
    output << "] result=!t" << call.result_type.value() << " requested=" << call.requested_results
           << " origin=%h" << call.origin.value() << '\n';
  }
  return output.str();
}

std::string dump_mir(const mir::Program& program, const mir::AliasEffectTable& analysis) {
  std::ostringstream output;
  output << dump_mir(program);
  output << "alias-effect-v3 revision=" << analysis.mir_revision << '\n';
  for (std::size_t index = 1; index < analysis.storages.size(); ++index) {
    const auto& facts = analysis.storages[index];
    output << "storage-alias !m" << facts.origin.value() << " root=!m" << facts.root.value()
           << " root-kind=" << enum_value(facts.root_kind) << " escapes=" << facts.escapes << '\n';
  }
  for (const auto& relation : analysis.aliases) {
    output << "alias !m" << relation.left.value() << " !m" << relation.right.value()
           << " relation=" << enum_value(relation.relation) << " origin=%h"
           << relation.origin.value() << '\n';
  }
  for (std::size_t index = 1; index < analysis.instructions.size(); ++index) {
    const auto& facts = analysis.instructions[index];
    output << "effect !i" << facts.origin.value() << " local=" << facts.local.bits()
           << " transitive=" << facts.effects.bits() << " reads=";
    dump_ids(output, facts.reads, "!m");
    output << " writes=";
    dump_ids(output, facts.writes, "!m");
    output << " memory-accesses=";
    dump_memory_accesses(output, facts.memory_accesses);
    output << " unknown-read=" << facts.reads_unknown << " unknown-write=" << facts.writes_unknown
           << '\n';
  }
  for (std::size_t index = 1; index < analysis.functions.size(); ++index) {
    const auto& facts = analysis.functions[index];
    output << "function-effect @f" << facts.origin.value() << " effects=" << facts.effects.bits()
           << " parameter-reads=";
    dump_flags(output, facts.parameter_reads);
    output << " parameter-writes=";
    dump_flags(output, facts.parameter_writes);
    output << " parameter-escapes=";
    dump_flags(output, facts.parameter_escapes);
    output << " unknown-read=" << facts.reads_unknown << " unknown-write=" << facts.writes_unknown
           << '\n';
  }
  for (const auto& facts : analysis.calls) {
    output << "call-effect !i" << facts.instruction.value() << " caller=@f" << facts.caller.value()
           << " callee=@f" << facts.callee.value() << " effects=" << facts.effects.bits()
           << " reads=";
    dump_ids(output, facts.reads, "!m");
    output << " writes=";
    dump_ids(output, facts.writes, "!m");
    output << " unknown-read=" << facts.reads_unknown << " unknown-write=" << facts.writes_unknown
           << '\n';
    for (const auto& argument : facts.arguments) {
      output << "  argument " << argument.ordinal << " storage=!m" << argument.storage.value()
             << " root=!m" << argument.root.value() << " transfer=" << enum_value(argument.transfer)
             << " region=";
      dump_storage_region(output, argument.region);
      output << " reads=" << argument.reads << " writes=" << argument.writes
             << " escapes=" << argument.escapes << '\n';
    }
    for (const auto& overlap : facts.overlaps) {
      output << "  overlap " << overlap.left << ',' << overlap.right
             << " relation=" << enum_value(overlap.relation)
             << " writable-conflict=" << overlap.writable_conflict << '\n';
    }
  }
  return output.str();
}

std::string dump_mir(const mir::Program& program, const mir::AliasEffectTable& alias_effects,
                     const mir::MemoryDependenceTable& memory_dependences) {
  std::ostringstream output;
  output << dump_mir(program, alias_effects);
  output << "memory-dependence-v1 revision=" << memory_dependences.mir_revision
         << " complete=" << memory_dependences.complete << '\n';
  for (std::size_t index = 1; index < memory_dependences.instructions.size(); ++index) {
    const auto& facts = memory_dependences.instructions[index];
    output << "memory-adjacency !i" << facts.origin.value() << " incoming=";
    dump_ids(output, facts.incoming, "!d");
    output << " outgoing=";
    dump_ids(output, facts.outgoing, "!d");
    output << '\n';
  }
  for (std::size_t index = 1; index < memory_dependences.dependences.size(); ++index) {
    const auto& dependence = memory_dependences.dependences[index];
    output << "memory-dependence !d" << dependence.id.value() << " source=!i"
           << dependence.source.instruction.value() << ':' << dependence.source.ordinal
           << (dependence.source.unknown ? ":unknown" : ":known") << " target=!i"
           << dependence.target.instruction.value() << ':' << dependence.target.ordinal
           << (dependence.target.unknown ? ":unknown" : ":known")
           << " kind=" << enum_value(dependence.kind)
           << " relation=" << enum_value(dependence.relation) << " barrier=" << dependence.barrier
           << " loop-carried=" << dependence.loop_carried << '\n';
  }
  return output.str();
}

}  // namespace mpf::detail
