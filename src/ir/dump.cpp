#include "dump.hpp"

#include <iomanip>
#include <sstream>
#include <type_traits>

namespace mpf::detail {
namespace {

template <typename Enum>
constexpr auto enum_value(const Enum value) noexcept {
  return static_cast<std::underlying_type_t<Enum>>(value);
}

void dump_hir_expression(std::ostringstream& output, const hir::Expression& expression,
                         const std::size_t depth) {
  if (!expression.valid()) return;
  output << std::string(depth * 2U, ' ') << "expr %h" << expression.id.value()
         << " kind=" << enum_value(expression.kind)
         << " type=" << enum_value(expression.inferred_type)
         << " intrinsic=" << enum_value(expression.intrinsic)
         << " value=" << std::quoted(expression.value) << " @" << expression.location.line << ':'
         << expression.location.column << '\n';
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
  output << std::string(depth * 2U, ' ') << "expr kind=" << enum_value(expression.kind)
         << " value=" << std::quoted(expression.value) << " operators=[";
  for (std::size_t index = 0; index < expression.operators.size(); ++index) {
    if (index != 0) output << ',';
    output << std::quoted(expression.operators[index]);
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

}  // namespace

std::string dump_hir(const hir::Program& program) {
  std::ostringstream output;
  output << "hir-v1 language=" << enum_value(program.language) << " nodes=" << program.node_count
         << " revision=" << program.revision << '\n';
  output << "semantics truthiness=" << enum_value(program.semantics.truthiness)
         << " logical-result=" << enum_value(program.semantics.logical_result)
         << " equality=" << enum_value(program.semantics.equality)
         << " division=" << enum_value(program.semantics.division)
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
  output << "semantic-v1 hir-nodes=" << table.hir_node_count
         << " hir-revision=" << table.hir_revision << " expressions=" << table.expressions.size()
         << " statements=" << table.statements.size() << '\n';
  for (std::size_t id = 1; id < table.nodes.size(); ++id) {
    const auto slot = table.nodes[id];
    output << "%h" << id << " kind=" << enum_value(slot.kind) << " offset=" << slot.offset;
    if (slot.kind == hir::SemanticNodeKind::expression && slot.offset < table.expressions.size()) {
      const auto& facts = table.expressions[slot.offset];
      output << " type=" << enum_value(facts.inferred_type)
             << " element=" << enum_value(facts.element_type)
             << " binding=" << enum_value(facts.binding)
             << " intrinsic=" << enum_value(facts.intrinsic) << " shape=[";
      for (std::size_t extent = 0; extent < facts.shape.size(); ++extent) {
        if (extent != 0) output << ',';
        output << facts.shape[extent];
      }
      output << "] outputs=" << facts.requested_outputs;
    } else if (slot.kind == hir::SemanticNodeKind::statement &&
               slot.offset < table.statements.size()) {
      const auto& facts = table.statements[slot.offset];
      output << " declared=" << enum_value(facts.declared_type)
             << " element=" << enum_value(facts.element_type) << " shape=[";
      for (std::size_t extent = 0; extent < facts.shape.size(); ++extent) {
        if (extent != 0) output << ',';
        output << facts.shape[extent];
      }
      output << "] parameters=" << facts.parameter_types.size()
             << " returns=" << facts.return_types.size()
             << " targets=" << facts.target_types.size();
    }
    output << '\n';
  }
  return output.str();
}

std::string dump_mir(const mir::Program& program) {
  std::ostringstream output;
  output << "mir-v1 language=" << enum_value(program.source_language)
         << " hir-nodes=" << program.hir_node_count << " revision=" << program.revision << '\n';
  for (std::size_t index = 1; index < program.types.size(); ++index) {
    const auto& type = program.types[index];
    output << "type !t" << index << " kind=" << enum_value(type.kind)
           << " value=" << enum_value(type.value_type)
           << " element=" << enum_value(type.element_type) << " elements=";
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
           << " kind=" << enum_value(storage.kind) << " lifetime=" << enum_value(storage.lifetime)
           << " base=!m" << storage.base.value() << " view=" << enum_value(storage.view)
           << " origin=%h" << storage.origin.value() << '\n';
  }
  for (std::size_t index = 1; index < program.functions.size(); ++index) {
    const auto& function = program.functions[index];
    output << "function @f" << function.id.value() << " name=" << std::quoted(function.name)
           << " signature=!t" << function.signature.value() << " parameters=";
    dump_ids(output, function.parameter_types, "!t");
    output << " results=";
    dump_ids(output, function.result_types, "!t");
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
               << " origin=%h" << instruction.origin.value() << '\n';
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
           << " callee=@f" << call.callee.value() << " arguments=";
    dump_ids(output, call.argument_types, "!t");
    output << " result=!t" << call.result_type.value() << " requested=" << call.requested_results
           << " origin=%h" << call.origin.value() << '\n';
  }
  return output.str();
}

std::string dump_mir(const mir::Program& program, const mir::AliasEffectTable& analysis) {
  std::ostringstream output;
  output << dump_mir(program);
  output << "alias-effect-v1 revision=" << analysis.mir_revision << '\n';
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
  }
  return output.str();
}

}  // namespace mpf::detail
