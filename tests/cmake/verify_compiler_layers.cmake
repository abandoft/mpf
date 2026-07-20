cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "compiler layer verification requires SOURCE_DIR")
endif()

function(mpf_assert_file_excludes file pattern message_text)
  file(READ "${SOURCE_DIR}/${file}" contents)
  if(contents MATCHES "${pattern}")
    message(FATAL_ERROR "${message_text}: ${file}")
  endif()
endfunction()

file(GLOB script_mode_contracts
  "${SOURCE_DIR}/cmake/*.cmake"
  "${SOURCE_DIR}/tests/cmake/*.cmake")
foreach(script_mode_contract IN LISTS script_mode_contracts)
  file(STRINGS "${script_mode_contract}" first_line LIMIT_COUNT 1)
  if(NOT first_line STREQUAL "cmake_minimum_required(VERSION 3.20)")
    message(FATAL_ERROR
      "script-mode CMake entry point does not establish the project policy baseline: "
      "${script_mode_contract}")
  endif()
endforeach()

file(READ "${SOURCE_DIR}/CMakeLists.txt" root_build_contract)
if(NOT root_build_contract MATCHES "COMPATIBILITY ExactVersion" OR
   root_build_contract MATCHES "COMPATIBILITY (SameMajorVersion|AnyNewerVersion)")
  message(FATAL_ERROR
    "unfinished MPF package must require its exact version without compatibility ranges")
endif()
foreach(static_target IN ITEMS
    mpf_core mpf_backend_common mpf_backend_javascript mpf_backend_cpp mpf)
  if(NOT root_build_contract MATCHES
     "add_library\\(${static_target}[ \t\r\n]+STATIC")
    message(FATAL_ERROR
      "0.x C++ target must not become an accidental shared ABI: ${static_target}")
  endif()
endforeach()
foreach(source_language IN ITEMS python matlab)
  foreach(logical_source_component IN ITEMS logical_source.cpp logical_source.hpp)
    if(NOT EXISTS
       "${SOURCE_DIR}/src/frontends/${source_language}/${logical_source_component}")
      message(FATAL_ERROR
        "logical-source frontend component is missing: "
        "${source_language}/${logical_source_component}")
    endif()
  endforeach()
endforeach()
mpf_assert_file_excludes("cmake/mpf-config.cmake.in"
  "MPF_(JAVASCRIPT|CPP)_BACKEND_AVAILABLE"
  "obsolete uppercase package availability variables were restored")
mpf_assert_file_excludes("include/mpf/transpiler.hpp"
  "language_from_name|source_language_name_known|target_from_name|target_language_name_known|fortran_source_form_from_name"
  "ambiguous legacy name parsing API was restored")
foreach(descriptor IN ITEMS
    src/frontends/common/descriptor.hpp
    src/backends/common/descriptor.hpp)
  mpf_assert_file_excludes("${descriptor}" "StringViewList"
    "descriptor compatibility string-list type was restored")
endforeach()
foreach(registry IN ITEMS
    src/frontends/common/registry.cpp
    src/backends/common/registry.cpp)
  mpf_assert_file_excludes("${registry}" "\\.aliases|_aliases"
    "registry compatibility alias routing was restored")
endforeach()
foreach(example IN ITEMS
    examples/embedding/CMakeLists.txt
    examples/installed/frontend/CMakeLists.txt
    examples/installed/backend/CMakeLists.txt)
  file(READ "${SOURCE_DIR}/${example}" example_contract)
  string(FIND "${example_contract}"
    [=[find_package(mpf ${MPF_REQUIRED_VERSION} EXACT]=]
    exact_version_contract)
  if(exact_version_contract EQUAL -1)
    message(FATAL_ERROR "installed example does not require the exact MPF version: ${example}")
  endif()
endforeach()
mpf_assert_file_excludes("src/core/diagnostic.cpp" "effective_end"
  "diagnostic renderer restored missing-range compatibility synthesis")

foreach(emitter IN ITEMS
    src/backends/javascript/emitter.cpp
    src/backends/cpp/emitter.cpp)
  mpf_assert_file_excludes("${emitter}" "SourceLanguage::"
    "target printer still branches on a source language identity")
  mpf_assert_file_excludes("${emitter}" "(javascript|cpp)_code_binding\\("
    "target printer still resolves intrinsic bindings")
  mpf_assert_file_excludes("${emitter}" "[./]ir/(hir|mir)\\.hpp"
    "target printer includes HIR or MIR")
  mpf_assert_file_excludes("${emitter}" "semantic::|program\\.semantics|semantics_"
    "target printer still interprets a source semantic profile")
  mpf_assert_file_excludes("${emitter}"
    "StatementKind|ExpressionKind|ValueType|IdentifierMangler|switch[ \\t]*\\("
    "pure emitter still performs target representation lowering")
  file(READ "${SOURCE_DIR}/${emitter}" emitter_contents)
  if(NOT emitter_contents MATCHES "serialize_chunks")
    message(FATAL_ERROR "pure emitter does not serialize target chunks: ${emitter}")
  endif()
endforeach()

foreach(frontend IN ITEMS
    src/frontends/python/frontend.cpp
    src/frontends/matlab/frontend.cpp
    src/frontends/fortran/frontend.cpp
    src/frontends/typescript/frontend.cpp)
  mpf_assert_file_excludes("${frontend}" "lower_from_syntax|\\.syntax|inventory_ast"
    "production frontend still wraps or lowers the shared parser syntax tree")
endforeach()
mpf_assert_file_excludes("src/frontends/common/ast.hpp" "Program syntax|AstInventory"
  "language AST artifact still embeds the transitional shared syntax program")

foreach(legacy_scratch IN ITEMS
    src/compiler/ir.hpp
    src/compiler/frontend.hpp
    src/compiler/frontend.cpp
    src/compiler/function_graph.cpp)
  if(EXISTS "${SOURCE_DIR}/${legacy_scratch}")
    message(FATAL_ERROR
      "shared recursive statement-parser scratch or compatibility facade was restored: ${legacy_scratch}")
  endif()
endforeach()
if(NOT EXISTS "${SOURCE_DIR}/src/frontends/common/ast_builder.hpp")
  message(FATAL_ERROR "direct language AST builder is missing")
endif()
file(READ "${SOURCE_DIR}/src/frontends/common/ast_builder.hpp" direct_ast_builder)
if(NOT direct_ast_builder MATCHES "class FrontendAstBuilder" OR
   NOT direct_ast_builder MATCHES "add_expression" OR
   NOT direct_ast_builder MATCHES "add_statement" OR
   NOT direct_ast_builder MATCHES "void reserve" OR
   NOT direct_ast_builder MATCHES "set_roots")
  message(FATAL_ERROR "direct language AST builder does not own the parser-to-arena lifecycle")
endif()
foreach(statement_parser IN ITEMS
    src/frontends/python/statement_parser.cpp
    src/frontends/matlab/statement_parser.cpp
    src/frontends/fortran/statement_parser.cpp
    src/frontends/typescript/statement_parser.cpp)
  file(READ "${SOURCE_DIR}/${statement_parser}" statement_parser_contract)
  if(NOT statement_parser_contract MATCHES "FrontendAstBuilder<" OR
     NOT statement_parser_contract MATCHES "std::vector<AstNodeId> parse_block" OR
     statement_parser_contract MATCHES "std::vector<Statement>" OR
     statement_parser_contract MATCHES "compiler/ir\\.hpp|make_(python|matlab|fortran)_ast")
    message(FATAL_ERROR
      "statement parser does not build its language arena directly: ${statement_parser}")
  endif()
endforeach()

file(READ "${SOURCE_DIR}/src/ir/hir.hpp" hir_contract)
foreach(forbidden IN ITEMS
    "inferred_type"
    "declared_type"
    "element_type"
    "previous_type"
    "tuple_types"
    "sequence_elements"
    "argument_intents"
    "parameter_intents"
    "return_types"
    "target_types"
    "requested_outputs"
    "multi_output_call"
    "procedure_has_result"
    "column_major"
    "slice_stop_inclusive"
    "storage_region")
  if(hir_contract MATCHES "${forbidden}")
    message(FATAL_ERROR "HIR contract regained semantic payload: ${forbidden}")
  endif()
endforeach()
if(hir_contract MATCHES "AssignmentPattern[ \t]+target_pattern")
  message(FATAL_ERROR "HIR contract regained the semantic assignment pattern payload")
endif()
file(READ "${SOURCE_DIR}/src/ir/hir_lowering.hpp" hir_lowering_contract)
if(NOT hir_lowering_contract MATCHES "struct LoweringResult" OR
   NOT hir_lowering_contract MATCHES "SemanticTable semantics")
  message(FATAL_ERROR "AST-to-HIR result does not carry its dense semantic seed")
endif()
mpf_assert_file_excludes("src/ir/hir.cpp" "lower_from_syntax|lower_expression|lower_statement"
  "shared syntax-to-HIR compatibility lowering was restored")

if(NOT EXISTS "${SOURCE_DIR}/src/ir/storage_region.hpp" OR
   NOT EXISTS "${SOURCE_DIR}/src/ir/storage_region.cpp")
  message(FATAL_ERROR "shared N-dimensional storage-region contract is missing")
endif()
file(READ "${SOURCE_DIR}/src/ir/storage_region.hpp" storage_region_contract)
foreach(required_region IN ITEMS
    "StorageRegionKind"
    "StorageRegionDimension"
    "StorageRegionRelation"
    "full_storage_region"
    "valid_storage_region"
    "storage_region_relation")
  if(NOT storage_region_contract MATCHES "${required_region}")
    message(FATAL_ERROR "storage-region contract is incomplete: ${required_region}")
  endif()
endforeach()
file(READ "${SOURCE_DIR}/src/ir/semantic_facts.hpp" semantic_facts_contract)
if(NOT semantic_facts_contract MATCHES "StorageRegion storage_region")
  message(FATAL_ERROR "HIR semantic side table does not own normalized storage regions")
endif()

foreach(lowering IN ITEMS
    src/backends/javascript/lowering.cpp
    src/backends/cpp/lowering.cpp)
  mpf_assert_file_excludes("${lowering}" "expression\\.argument_(intents|optional_forward)"
    "target lowering reinterprets source call intent instead of MIR transfer facts")
  file(READ "${SOURCE_DIR}/${lowering}" lowering_contents)
  if(NOT lowering_contents MATCHES "materialize_chunks\\(render_" OR
     NOT lowering_contents MATCHES "SemanticProgram" OR
     NOT lowering_contents MATCHES "plan_lir_resources" OR
     NOT lowering_contents MATCHES "plan_lir_representation")
    message(FATAL_ERROR "target lowering does not materialize an independent serialized LIR: ${lowering}")
  endif()
  string(FIND "${lowering_contents}" "plan_lir_resources(*lowered" resource_plan_index)
  string(FIND "${lowering_contents}" "plan_lir_representation(*lowered" representation_plan_index)
  if(resource_plan_index LESS 0 OR representation_plan_index LESS 0 OR
     representation_plan_index LESS resource_plan_index)
    message(FATAL_ERROR "target representation must run after ABI/resource planning: ${lowering}")
  endif()
endforeach()

foreach(representation IN ITEMS
    src/backends/javascript/lir_representation.cpp
    src/backends/cpp/lir_representation.cpp)
  if(NOT EXISTS "${SOURCE_DIR}/${representation}")
    message(FATAL_ERROR "target LIR representation layer is missing: ${representation}")
  endif()
  file(READ "${SOURCE_DIR}/${representation}" representation_contents)
  if(NOT representation_contents MATCHES "plan_lir_representation" OR
     NOT representation_contents MATCHES "verify_lir_representation" OR
     NOT representation_contents MATCHES "expected_expression_plan" OR
     NOT representation_contents MATCHES "expected_statement_plan" OR
     NOT representation_contents MATCHES "CallArgumentForm" OR
     NOT representation_contents MATCHES "EvaluationForm" OR
     NOT representation_contents MATCHES "WritebackForm" OR
     NOT representation_contents MATCHES "build_source_segment_plan" OR
     NOT representation_contents MATCHES "AssignmentLeafPlan" OR
     NOT representation_contents MATCHES "function_context")
    message(FATAL_ERROR "target LIR representation layer does not own expression/statement plans: ${representation}")
  endif()
endforeach()

foreach(planning IN ITEMS
    src/backends/javascript/lir_planning.cpp
    src/backends/cpp/lir_planning.cpp)
  if(NOT EXISTS "${SOURCE_DIR}/${planning}")
    message(FATAL_ERROR "target LIR planning layer is missing: ${planning}")
  endif()
  file(READ "${SOURCE_DIR}/${planning}" planning_contents)
  if(NOT planning_contents MATCHES "plan_lir_resources" OR
     NOT planning_contents MATCHES "verify_lir_resources" OR
     NOT planning_contents MATCHES "program_scope" OR
     NOT planning_contents MATCHES "function_scope" OR
     NOT planning_contents MATCHES "statement_scope" OR
     NOT planning_contents MATCHES "body_scope" OR
     NOT planning_contents MATCHES "RuntimeFragment")
    message(FATAL_ERROR "target LIR planning layer does not own scope and layout resources: ${planning}")
  endif()
endforeach()

foreach(renderer IN ITEMS
    src/backends/javascript/renderer.cpp
    src/backends/cpp/renderer.cpp)
  mpf_assert_file_excludes("${renderer}"
    "mangler_->name\\((statement\\.name|expression\\.plan\\.token|statement\\.parameters\\[|statement\\.plan\\.targets\\[)"
    "target renderer bypasses SymbolId-aware identifier lookup")
endforeach()

file(READ "${SOURCE_DIR}/src/backends/common/identifier_mangler.hpp" identifier_contract)
if(NOT identifier_contract MATCHES "struct IdentifierInventory" OR
   NOT identifier_contract MATCHES "std::map<SymbolId, std::string> symbols" OR
   NOT identifier_contract MATCHES "name\\(SymbolId symbol")
  message(FATAL_ERROR "target identifier allocation is not keyed by semantic SymbolId")
endif()

file(READ "${SOURCE_DIR}/src/ir/mir.hpp" mir_contract)
file(READ "${SOURCE_DIR}/src/ir/pass_manager.hpp" pass_manager_contract)
if(NOT pass_manager_contract MATCHES "std::deque<Entry> entries_")
  message(FATAL_ERROR
    "AnalysisManager cache storage does not preserve returned references across inserted analyses")
endif()
if(NOT EXISTS "${SOURCE_DIR}/src/ir/mir_verifier.cpp" OR
   NOT EXISTS "${SOURCE_DIR}/src/ir/mir_opcode.hpp" OR
   NOT EXISTS "${SOURCE_DIR}/src/ir/mir_optimization.cpp" OR
   NOT EXISTS "${SOURCE_DIR}/src/ir/memory_dependence.cpp")
  message(FATAL_ERROR
    "MIR verifier/opcode/optimization/memory-dependence contracts are not split into dedicated components")
endif()
file(READ "${SOURCE_DIR}/src/ir/mir_optimization.cpp" mir_optimization_contract)
foreach(required_pass IN ITEMS
    "mir-shape-canonicalization"
    "mir-copy-propagation"
    "mir-constant-folding-dce"
    "mir-cfg-cleanup")
  if(NOT mir_optimization_contract MATCHES "${required_pass}")
    message(FATAL_ERROR "shared MIR default optimization is missing: ${required_pass}")
  endif()
endforeach()
foreach(required IN ITEMS
    "MirExpressionId"
    "MirStatementId"
    "std::vector<MirExpressionId> children"
    "std::vector<MirStatementId> body"
    "std::vector<MirStatementId> roots"
    "successor_arguments"
    "BlockArgument"
    "AliasEffectTable"
    "ArgumentTransfer"
    "StorageLifetime"
    "StorageViewKind"
    "StorageRegion storage_region"
    "StorageRegion region"
    "OperationAttributeTable"
    "ExpressionAttributes"
    "StatementAttributes"
    "InstructionAttributes"
    "MemoryAccessMode"
    "MemoryAccess"
    "std::vector<MemoryAccess> memory_accesses"
    "std::vector<InstructionAttributes> instructions"
    "std::vector<ShapeId> parameter_shapes"
    "std::vector<ShapeId> result_shapes"
    "bool lazy_cfg"
    "load"
    "store_indexed"
    "copy"
    "writeback"
    "truthiness"
    "ComparisonOperator comparison")
  if(NOT mir_contract MATCHES "${required}")
    message(FATAL_ERROR "MIR contract is missing commercial CFG/alias field: ${required}")
  endif()
endforeach()
if(mir_contract MATCHES "std::vector<Expression>[ \t]+children" OR
   mir_contract MATCHES "std::vector<Statement>[ \t]+(body|alternative)")
  message(FATAL_ERROR "MIR restored a recursive HIR-compatible expression/statement projection")
endif()
if(mir_contract MATCHES "Opcode::assignment|Opcode::indexed_assignment")
  message(FATAL_ERROR "MIR restored ambiguous assignment opcodes instead of load/store contracts")
endif()
if(mir_contract MATCHES
     "struct Expression[ \t\r\n]*\\{[^}]*((inferred|element)_type|tuple_types|sequence_elements|argument_intents|requested_outputs|column_major)" OR
   mir_contract MATCHES
     "struct Statement[ \t\r\n]*\\{[^}]*(declared_type|previous_type|parameter_intents|return_types|target_types|AssignmentPattern[ \t]+target_pattern)")
  message(FATAL_ERROR "flat MIR nodes regained duplicated HIR semantic payload")
endif()
file(READ "${SOURCE_DIR}/src/backends/common/lir_builder.hpp" target_lir_builder_contract)
if(NOT target_lir_builder_contract MATCHES "mir::expression\\(program" OR
   NOT target_lir_builder_contract MATCHES "mir::statement\\(program" OR
   NOT target_lir_builder_contract MATCHES "mir::attributes\\(program" OR
   NOT target_lir_builder_contract MATCHES "mir::value_type\\(program" OR
   NOT target_lir_builder_contract MATCHES "mir::shape\\(program" OR
   target_lir_builder_contract MATCHES "source\\.statements")
  message(FATAL_ERROR "target LIR builder does not consume the flat MIR value/operation arena")
endif()
if(mir_contract MATCHES "struct LoweringResult[^{]*\\{[^}]*AliasEffectTable" OR
   mir_contract MATCHES "struct Instruction[^{]*\\{[^}]*Effect[ \t]+effects")
  message(FATAL_ERROR "alias/effect analysis is coupled back into MIR lowering or instructions")
endif()
if(mir_contract MATCHES "argument_(types|storages|omitted)")
  message(FATAL_ERROR "MIR call sites regressed to parallel argument arrays")
endif()
file(READ "${SOURCE_DIR}/src/ir/alias_effect_analysis.cpp" alias_effect_contract)
if(NOT alias_effect_contract MATCHES "storage_region_relation" OR
   NOT alias_effect_contract MATCHES "memory_accesses_conflict" OR
   NOT alias_effect_contract MATCHES "writable_conflict")
  message(FATAL_ERROR "MIR alias/effect analysis does not refine calls with storage regions")
endif()
file(READ "${SOURCE_DIR}/src/ir/memory_dependence.hpp" memory_dependence_contract)
file(READ "${SOURCE_DIR}/src/ir/memory_dependence.cpp" memory_dependence_implementation)
foreach(required IN ITEMS
    "MemoryDependenceId"
    "MemoryAccessSite"
    "MemoryDependenceKind"
    "MemoryDependenceTable"
    "InstructionMemoryDependenceFacts"
    "analyze_memory_dependences"
    "verify_memory_dependences")
  if(NOT memory_dependence_contract MATCHES "${required}")
    message(FATAL_ERROR "memory-dependence contract is missing: ${required}")
  endif()
endforeach()
foreach(required IN ITEMS
    "MemoryDependenceKind::flow"
    "MemoryDependenceKind::anti"
    "MemoryDependenceKind::output"
    "loop_carried"
    "AliasClass::must_alias"
    "graph.loop_edges")
  if(NOT memory_dependence_implementation MATCHES "${required}")
    message(FATAL_ERROR "CFG memory-dependence implementation is missing: ${required}")
  endif()
endforeach()
foreach(target_region_consumer IN ITEMS
    src/backends/javascript/lowering.cpp
    src/backends/cpp/lowering.cpp
    src/backends/javascript/lir_representation.cpp
    src/backends/cpp/lir_representation.cpp
    src/backends/javascript/renderer.cpp
    src/backends/cpp/renderer.cpp)
  mpf_assert_file_excludes("${target_region_consumer}" "StorageRegion|storage_region_relation"
    "target backend recomputes shared storage-region semantics")
endforeach()

foreach(target_lir IN ITEMS src/backends/javascript/lir.hpp src/backends/cpp/lir.hpp)
  file(READ "${SOURCE_DIR}/${target_lir}" target_lir_contract)
  if(NOT target_lir_contract MATCHES "argument_transfers" OR
     NOT target_lir_contract MATCHES "FunctionAbi" OR
     NOT target_lir_contract MATCHES "TemporaryPlan" OR
     NOT target_lir_contract MATCHES "ScopePlan" OR
     NOT target_lir_contract MATCHES "program_scope" OR
     NOT target_lir_contract MATCHES "function_scope" OR
     NOT target_lir_contract MATCHES "RuntimeFragment" OR
     NOT target_lir_contract MATCHES "ExpressionPlan" OR
     NOT target_lir_contract MATCHES "ExpressionForm" OR
     NOT target_lir_contract MATCHES "StatementPlan" OR
     NOT target_lir_contract MATCHES "StatementForm" OR
     NOT target_lir_contract MATCHES "ConditionForm" OR
     NOT target_lir_contract MATCHES "VariableAccess" OR
     NOT target_lir_contract MATCHES "EvaluationForm" OR
     NOT target_lir_contract MATCHES "CallValueForm" OR
     NOT target_lir_contract MATCHES "CallArgumentPlan" OR
     NOT target_lir_contract MATCHES "WritebackForm" OR
     NOT target_lir_contract MATCHES "source_segments" OR
     NOT target_lir_contract MATCHES "AssignmentLeafPlan" OR
     NOT target_lir_contract MATCHES "CallForm" OR
     NOT target_lir_contract MATCHES "CallArgumentForm" OR
     NOT target_lir_contract MATCHES "IndexForm" OR
     NOT target_lir_contract MATCHES "index_selectors" OR
     NOT target_lir_contract MATCHES "IndexExtentSource" OR
     NOT target_lir_contract MATCHES "index_extents" OR
     NOT target_lir_contract MATCHES "offsets" OR
     target_lir_contract MATCHES "argument_intents")
    message(FATAL_ERROR "target LIR does not own a lowered argument transfer plan: ${target_lir}")
  endif()
endforeach()

file(READ "${SOURCE_DIR}/src/ir/semantics.hpp" division_semantic_contract)
if(NOT division_semantic_contract MATCHES "DivisionByZero" OR
   NOT division_semantic_contract MATCHES "target_native" OR
   NOT division_semantic_contract MATCHES "ieee754" OR
   NOT division_semantic_contract MATCHES "exception" OR
   NOT division_semantic_contract MATCHES "valid_division_contract" OR
   NOT division_semantic_contract MATCHES "source_division_contract_matches")
  message(FATAL_ERROR "source division behavior is not a typed semantic contract")
endif()
foreach(shared_verifier IN ITEMS src/ir/hir.cpp src/ir/mir_verifier.cpp)
  file(READ "${SOURCE_DIR}/${shared_verifier}" division_shared_verifier)
  if(NOT division_shared_verifier MATCHES "source_division_contract_matches")
    message(FATAL_ERROR
      "shared IR verifier does not validate source division policy: ${shared_verifier}")
  endif()
endforeach()
foreach(target_lir IN ITEMS src/backends/javascript/lir.hpp src/backends/cpp/lir.hpp)
  file(READ "${SOURCE_DIR}/${target_lir}" division_target_contract)
  if(NOT division_target_contract MATCHES "Division division" OR
     NOT division_target_contract MATCHES "DivisionByZero division_by_zero" OR
     NOT division_target_contract MATCHES "binary_runtime_call" OR
     NOT division_target_contract MATCHES "scalar_division")
    message(FATAL_ERROR "target LIR does not own scalar-division policy: ${target_lir}")
  endif()
endforeach()
mpf_assert_file_excludes("src/backends/cpp/lir.hpp"
  "binary_(floor|real|reverse)_divide"
  "cpp LIR restored direct scalar-division representation forms")
mpf_assert_file_excludes("src/backends/javascript/lir.hpp" "binary_floor_divide"
  "JavaScript LIR restored unchecked Python floor division")
foreach(representation IN ITEMS
    src/backends/javascript/lir_representation.cpp
    src/backends/cpp/lir_representation.cpp)
  file(READ "${SOURCE_DIR}/${representation}" division_representation)
  if(NOT division_representation MATCHES "python_true_divide" OR
     NOT division_representation MATCHES "python_floor_divide" OR
     NOT division_representation MATCHES "DivisionByZero")
    message(FATAL_ERROR
      "target representation does not select scalar-division runtime calls: ${representation}")
  endif()
endforeach()
foreach(runtime IN ITEMS src/backends/javascript/runtime.cpp src/backends/cpp/runtime.cpp)
  file(READ "${SOURCE_DIR}/${runtime}" division_runtime)
  if(NOT division_runtime MATCHES "python_true_divide" OR
     NOT division_runtime MATCHES "python_floor_divide" OR
     NOT division_runtime MATCHES "division by zero")
    message(FATAL_ERROR "target runtime does not own checked Python division: ${runtime}")
  endif()
endforeach()
file(READ "${SOURCE_DIR}/src/backends/cpp/runtime.cpp" cpp_division_runtime)
if(NOT cpp_division_runtime MATCHES "right != 0.0" OR
   NOT cpp_division_runtime MATCHES "quiet_NaN" OR
   NOT cpp_division_runtime MATCHES "std::signbit")
  message(FATAL_ERROR "cpp IEEE division does not handle signed zero without a zero-divide")
endif()
if(cpp_division_runtime MATCHES "warning\\(disable|diagnostic ignored")
  message(FATAL_ERROR "cpp IEEE division suppresses compiler diagnostics")
endif()
foreach(renderer IN ITEMS src/backends/javascript/renderer.cpp src/backends/cpp/renderer.cpp)
  mpf_assert_file_excludes("${renderer}"
    "DivisionByZero|ieee_divide|python_true_divide|python_floor_divide"
    "target renderer recovered scalar-division semantics from source policy or helper names")
endforeach()

file(READ "${SOURCE_DIR}/src/ir/semantics.hpp" index_extent_contract)
file(READ "${SOURCE_DIR}/src/ir/semantic_facts.hpp" hir_extent_contract)
file(READ "${SOURCE_DIR}/src/ir/mir.hpp" mir_extent_contract)
file(READ "${SOURCE_DIR}/src/compiler/array_storage.hpp" array_storage_contract)
if(NOT index_extent_contract MATCHES "IndexExtentSource" OR
   NOT index_extent_contract MATCHES "runtime_axis" OR
   NOT index_extent_contract MATCHES "runtime_linear" OR
   NOT hir_extent_contract MATCHES "index_extents" OR
   NOT mir_extent_contract MATCHES "index_extents")
  message(FATAL_ERROR "dynamic index extent is not a typed HIR/MIR contract")
endif()

if(NOT index_extent_contract MATCHES "MatrixConditionPolicy" OR
   NOT index_extent_contract MATCHES "matrix_condition_policy" OR
   NOT index_extent_contract MATCHES "square_continue_with_warning" OR
   NOT index_extent_contract MATCHES "MatrixFactorizationPolicy" OR
   NOT index_extent_contract MATCHES "matrix_factorization_policy" OR
   NOT index_extent_contract MATCHES "rank_revealing_column_pivoted_qr" OR
   NOT index_extent_contract MATCHES "MatrixStructurePolicy" OR
   NOT index_extent_contract MATCHES "matrix_structure_policy" OR
   NOT index_extent_contract MATCHES "classify_real_square" OR
   NOT index_extent_contract MATCHES "MatrixStoragePolicy" OR
   NOT index_extent_contract MATCHES "matrix_storage_policy" OR
   NOT index_extent_contract MATCHES "sparse_csc_coefficient" OR
   NOT index_extent_contract MATCHES "sparse_csc_multiply" OR
   NOT index_extent_contract MATCHES "valid_matrix_multiply_storage_contract" OR
   NOT index_extent_contract MATCHES "SparseElementwiseOperation" OR
   NOT index_extent_contract MATCHES "SparseElementwiseStoragePolicy" OR
   NOT index_extent_contract MATCHES "valid_sparse_elementwise_contract" OR
   NOT index_extent_contract MATCHES "SparseLogicalOperation" OR
   NOT index_extent_contract MATCHES "SparseLogicalStoragePolicy" OR
   NOT index_extent_contract MATCHES "valid_sparse_logical_contract" OR
   NOT index_extent_contract MATCHES "ReductionStoragePolicy" OR
   NOT index_extent_contract MATCHES "valid_logical_reduction_storage_contract" OR
   NOT index_extent_contract MATCHES "SparseConstructionKind" OR
   NOT index_extent_contract MATCHES "triplets_reserved" OR
   NOT index_extent_contract MATCHES "SparseValueDomain" OR
   NOT index_extent_contract MATCHES "SparseDuplicatePolicy" OR
   NOT index_extent_contract MATCHES "valid_sparse_stored_value_type" OR
   NOT index_extent_contract MATCHES "SparseIndexKind" OR
   NOT index_extent_contract MATCHES "valid_sparse_index_contract" OR
   NOT index_extent_contract MATCHES "SparseMutationKind" OR
   NOT index_extent_contract MATCHES "valid_sparse_mutation_contract" OR
   NOT array_storage_contract MATCHES "ArrayStorageFormat" OR
   NOT array_storage_contract MATCHES "sparse_csc" OR
   NOT array_storage_contract MATCHES "join_array_storage_formats" OR
   NOT hir_extent_contract MATCHES "MatrixConditionPolicy condition_policy" OR
   NOT hir_extent_contract MATCHES "MatrixFactorizationPolicy factorization_policy" OR
   NOT hir_extent_contract MATCHES "MatrixStructurePolicy structure_policy" OR
   NOT hir_extent_contract MATCHES "MatrixStoragePolicy storage_policy" OR
   NOT hir_extent_contract MATCHES "SparseElementwisePlan" OR
   NOT hir_extent_contract MATCHES "sparse_elementwise" OR
   NOT hir_extent_contract MATCHES "SparseLogicalPlan" OR
   NOT hir_extent_contract MATCHES "sparse_logical" OR
   NOT hir_extent_contract MATCHES "ReductionPlan" OR
   NOT hir_extent_contract MATCHES "ReductionStoragePolicy storage_policy" OR
   NOT hir_extent_contract MATCHES "SparseConstructionPlan" OR
   NOT hir_extent_contract MATCHES "sparse_construction" OR
   NOT hir_extent_contract MATCHES "SparseValueDomain value_domain" OR
   NOT hir_extent_contract MATCHES "SparseDuplicatePolicy duplicate_policy" OR
   NOT hir_extent_contract MATCHES "SparseIndexPlan" OR
   NOT hir_extent_contract MATCHES "sparse_index" OR
   NOT hir_extent_contract MATCHES "SparseMutationPlan" OR
   NOT hir_extent_contract MATCHES "sparse_mutation" OR
   NOT hir_extent_contract MATCHES "ArrayStorageFormat array_storage" OR
   NOT mir_extent_contract MATCHES "MatrixConditionPolicy condition_policy" OR
   NOT mir_extent_contract MATCHES "MatrixFactorizationPolicy factorization_policy" OR
   NOT mir_extent_contract MATCHES "MatrixStructurePolicy structure_policy" OR
   NOT mir_extent_contract MATCHES "MatrixStoragePolicy storage_policy" OR
   NOT mir_extent_contract MATCHES "SparseElementwisePlan" OR
   NOT mir_extent_contract MATCHES "sparse_elementwise" OR
   NOT mir_extent_contract MATCHES "SparseLogicalPlan" OR
   NOT mir_extent_contract MATCHES "sparse_logical" OR
   NOT mir_extent_contract MATCHES "ReductionPlan" OR
   NOT mir_extent_contract MATCHES "ReductionStoragePolicy storage_policy" OR
   NOT mir_extent_contract MATCHES "SparseConstructionPlan" OR
   NOT mir_extent_contract MATCHES "sparse_construction" OR
   NOT mir_extent_contract MATCHES "SparseValueDomain value_domain" OR
   NOT mir_extent_contract MATCHES "SparseDuplicatePolicy duplicate_policy" OR
   NOT mir_extent_contract MATCHES "SparseIndexPlan" OR
   NOT mir_extent_contract MATCHES "sparse_index" OR
   NOT mir_extent_contract MATCHES "SparseMutationPlan" OR
   NOT mir_extent_contract MATCHES "sparse_mutation" OR
   NOT mir_extent_contract MATCHES "ArrayStorageFormat array_storage")
  message(FATAL_ERROR
    "matrix and sparse operation policy is not a typed Semantic/HIR/MIR contract")
endif()
mpf_assert_file_excludes("src/ir/semantics.hpp" "detect_diagonal_triangular"
  "matrix structure policy restored the pre-advanced real classifier")
file(READ "${SOURCE_DIR}/src/backends/common/lir_builder.hpp" condition_lir_builder_contract)
if(NOT condition_lir_builder_contract MATCHES
     "matrix_operation\.condition_policy = attributes\.matrix_operation\.condition_policy" OR
   NOT condition_lir_builder_contract MATCHES
     "matrix_operation\.factorization_policy =" OR
   NOT condition_lir_builder_contract MATCHES
     "matrix_operation\.structure_policy = attributes\.matrix_operation\.structure_policy" OR
   NOT condition_lir_builder_contract MATCHES
     "matrix_operation\.storage_policy = attributes\.matrix_operation\.storage_policy" OR
   NOT condition_lir_builder_contract MATCHES
     "matrix_operation\.left_storage = attributes\.matrix_operation\.left_storage" OR
   NOT condition_lir_builder_contract MATCHES
     "sparse_elementwise\.operation = attributes\.sparse_elementwise\.operation" OR
   NOT condition_lir_builder_contract MATCHES
     "sparse_elementwise\.storage_policy = attributes\.sparse_elementwise\.storage_policy" OR
   NOT condition_lir_builder_contract MATCHES
     "sparse_logical\.operation = attributes\.sparse_logical\.operation" OR
   NOT condition_lir_builder_contract MATCHES
     "sparse_logical\.storage_policy = attributes\.sparse_logical\.storage_policy" OR
   NOT condition_lir_builder_contract MATCHES
     "reduction\.storage_policy = attributes\.reduction\.storage_policy" OR
   NOT condition_lir_builder_contract MATCHES
     "reduction\.input_storage = attributes\.reduction\.input_storage" OR
   NOT condition_lir_builder_contract MATCHES
     "reduction\.result_storage = attributes\.reduction\.result_storage" OR
   NOT condition_lir_builder_contract MATCHES
     "sparse_construction\.kind = attributes\.sparse_construction\.kind" OR
   NOT condition_lir_builder_contract MATCHES
     "sparse_construction\.triplet_element_counts" OR
   NOT condition_lir_builder_contract MATCHES
     "sparse_construction\.value_domain = attributes\.sparse_construction\.value_domain" OR
   NOT condition_lir_builder_contract MATCHES
     "sparse_construction\.duplicate_policy = attributes\.sparse_construction\.duplicate_policy" OR
   NOT condition_lir_builder_contract MATCHES
     "sparse_index\.kind = attributes\.sparse_index\.kind" OR
   NOT condition_lir_builder_contract MATCHES
     "sparse_index\.result_shape" OR
   NOT condition_lir_builder_contract MATCHES
     "sparse_mutation\.kind = sparse\.kind" OR
   NOT condition_lir_builder_contract MATCHES
     "sparse_mutation\.result_shape" OR
   NOT condition_lir_builder_contract MATCHES
     "result\.array_storage = mir::array_storage")
  message(FATAL_ERROR
    "target LIR builder does not propagate analyzed matrix and sparse operation policies")
endif()
foreach(target_lir IN ITEMS src/backends/javascript/lir.hpp src/backends/cpp/lir.hpp)
  file(READ "${SOURCE_DIR}/${target_lir}" condition_target_lir_contract)
  if(NOT condition_target_lir_contract MATCHES "MatrixConditionPolicy condition_policy" OR
     NOT condition_target_lir_contract MATCHES
       "MatrixFactorizationPolicy factorization_policy" OR
     NOT condition_target_lir_contract MATCHES "MatrixStructurePolicy structure_policy" OR
     NOT condition_target_lir_contract MATCHES "MatrixStoragePolicy storage_policy" OR
     NOT condition_target_lir_contract MATCHES "SparseElementwisePlan" OR
     NOT condition_target_lir_contract MATCHES
       "SparseElementwiseStoragePolicy storage_policy" OR
     NOT condition_target_lir_contract MATCHES "SparseLogicalPlan" OR
     NOT condition_target_lir_contract MATCHES
       "SparseLogicalStoragePolicy storage_policy" OR
     NOT condition_target_lir_contract MATCHES "ReductionPlan" OR
     NOT condition_target_lir_contract MATCHES "ReductionStoragePolicy storage_policy" OR
     NOT condition_target_lir_contract MATCHES "SparseConstructionPlan" OR
     NOT condition_target_lir_contract MATCHES "SparseConstructionKind kind" OR
     NOT condition_target_lir_contract MATCHES "SparseValueDomain value_domain" OR
     NOT condition_target_lir_contract MATCHES "SparseDuplicatePolicy duplicate_policy" OR
     NOT condition_target_lir_contract MATCHES "SparseIndexPlan" OR
     NOT condition_target_lir_contract MATCHES "SparseIndexKind kind" OR
     NOT condition_target_lir_contract MATCHES "SparseMutationPlan" OR
     NOT condition_target_lir_contract MATCHES "SparseMutationKind kind" OR
     NOT condition_target_lir_contract MATCHES "ArrayStorageFormat left_storage" OR
     NOT condition_target_lir_contract MATCHES "ArrayStorageFormat array_storage")
    message(FATAL_ERROR "target LIR does not own matrix policies: ${target_lir}")
  endif()
endforeach()
file(READ "${SOURCE_DIR}/src/semantic/expression_analyzer.cpp" reduction_analyzer_contract)
file(READ "${SOURCE_DIR}/src/ir/mir.cpp" reduction_mir_lowering_contract)
file(READ "${SOURCE_DIR}/src/ir/mir_verifier.cpp" reduction_mir_verifier_contract)
if(NOT reduction_analyzer_contract MATCHES "facts\.reduction\.storage_policy" OR
   NOT reduction_analyzer_contract MATCHES "facts\.reduction\.input_storage" OR
   NOT reduction_analyzer_contract MATCHES "facts\.reduction\.result_storage" OR
   NOT reduction_mir_lowering_contract MATCHES "reduction\.storage_policy" OR
   NOT reduction_mir_lowering_contract MATCHES "reduction\.result_storage" OR
   NOT reduction_mir_verifier_contract MATCHES
     "valid_logical_reduction_storage_contract" OR
   NOT reduction_mir_verifier_contract MATCHES
     "reduction\.result_storage != array_storage")
  message(FATAL_ERROR
    "Analyzer and MIR do not independently own logical reduction storage plans")
endif()
file(READ "${SOURCE_DIR}/src/semantic/expression_analyzer.cpp" sparse_logical_analyzer_contract)
file(READ "${SOURCE_DIR}/src/ir/mir.cpp" sparse_logical_mir_lowering_contract)
file(READ "${SOURCE_DIR}/src/ir/mir_verifier.cpp" sparse_logical_mir_verifier_contract)
if(NOT sparse_logical_analyzer_contract MATCHES "facts\.sparse_logical =" OR
   NOT sparse_logical_analyzer_contract MATCHES "sparse_logical_result_storage" OR
   NOT sparse_logical_analyzer_contract MATCHES "valid_sparse_logical_contract" OR
   NOT sparse_logical_mir_lowering_contract MATCHES "sparse_logical\.operation" OR
   NOT sparse_logical_mir_lowering_contract MATCHES "sparse_logical\.result_shape" OR
   NOT sparse_logical_mir_verifier_contract MATCHES "valid_sparse_logical_shapes" OR
   NOT sparse_logical_mir_verifier_contract MATCHES "valid_sparse_logical_contract")
  message(FATAL_ERROR
    "Analyzer and MIR do not independently own sparse logical storage and shape plans")
endif()
file(READ "${SOURCE_DIR}/src/ir/semantics.hpp" sparse_arithmetic_semantic_contract)
foreach(required_sparse_arithmetic_semantic IN ITEMS
    "SparseArithmeticOperation"
    "SparseArithmeticStoragePolicy"
    "SparseValueDomain"
    "sparse_arithmetic_result_storage"
    "sparse_arithmetic_storage_policy"
    "valid_sparse_arithmetic_contract")
  if(NOT sparse_arithmetic_semantic_contract MATCHES
     "${required_sparse_arithmetic_semantic}")
    message(FATAL_ERROR
      "sparse arithmetic semantic contract is missing: "
      "${required_sparse_arithmetic_semantic}")
  endif()
endforeach()
foreach(sparse_arithmetic_ir IN ITEMS
    src/ir/semantic_facts.hpp
    src/ir/mir.hpp
    src/backends/javascript/lir.hpp
    src/backends/cpp/lir.hpp)
  file(READ "${SOURCE_DIR}/${sparse_arithmetic_ir}" sparse_arithmetic_ir_contract)
  if(NOT sparse_arithmetic_ir_contract MATCHES "SparseArithmeticPlan" OR
     NOT sparse_arithmetic_ir_contract MATCHES "sparse_arithmetic" OR
     NOT sparse_arithmetic_ir_contract MATCHES "SparseArithmeticStoragePolicy" OR
     NOT sparse_arithmetic_ir_contract MATCHES "SparseValueDomain value_domain")
    message(FATAL_ERROR
      "IR layer does not own a typed sparse arithmetic plan: ${sparse_arithmetic_ir}")
  endif()
endforeach()
file(READ "${SOURCE_DIR}/src/backends/common/lir_builder.hpp"
     sparse_arithmetic_lir_builder_contract)
if(NOT sparse_arithmetic_lir_builder_contract MATCHES
     "sparse_arithmetic\.operation = attributes\.sparse_arithmetic\.operation" OR
   NOT sparse_arithmetic_lir_builder_contract MATCHES
     "sparse_arithmetic\.storage_policy = attributes\.sparse_arithmetic\.storage_policy" OR
   NOT sparse_arithmetic_lir_builder_contract MATCHES
     "sparse_arithmetic\.value_domain = attributes\.sparse_arithmetic\.value_domain" OR
   NOT sparse_arithmetic_lir_builder_contract MATCHES
     "sparse_arithmetic\.result_storage = attributes\.sparse_arithmetic\.result_storage" OR
   NOT sparse_arithmetic_lir_builder_contract MATCHES
     "sparse_arithmetic\.result_shape")
  message(FATAL_ERROR
    "target LIR builder does not propagate the sparse arithmetic contract")
endif()
file(READ "${SOURCE_DIR}/src/semantic/expression_analyzer.cpp"
     sparse_arithmetic_analyzer_contract)
file(READ "${SOURCE_DIR}/src/ir/mir.cpp" sparse_arithmetic_mir_lowering_contract)
file(READ "${SOURCE_DIR}/src/ir/mir_verifier.cpp" sparse_arithmetic_mir_verifier_contract)
if(NOT sparse_arithmetic_analyzer_contract MATCHES "facts\.sparse_arithmetic =" OR
   NOT sparse_arithmetic_analyzer_contract MATCHES "sparse_arithmetic_result_storage" OR
   NOT sparse_arithmetic_analyzer_contract MATCHES "SparseValueDomain::finite_complex" OR
   NOT sparse_arithmetic_analyzer_contract MATCHES "valid_sparse_arithmetic_contract" OR
   NOT sparse_arithmetic_mir_lowering_contract MATCHES "sparse_arithmetic\.operation" OR
   NOT sparse_arithmetic_mir_lowering_contract MATCHES "sparse_arithmetic\.value_domain" OR
   NOT sparse_arithmetic_mir_lowering_contract MATCHES "sparse_arithmetic\.result_shape" OR
   NOT sparse_arithmetic_mir_verifier_contract MATCHES "valid_sparse_arithmetic_shapes" OR
   NOT sparse_arithmetic_mir_verifier_contract MATCHES
     "sparse_arithmetic\.value_domain" OR
   NOT sparse_arithmetic_mir_verifier_contract MATCHES "valid_sparse_arithmetic_contract")
  message(FATAL_ERROR
    "Analyzer and MIR do not independently own sparse arithmetic storage and shape plans")
endif()
foreach(sparse_arithmetic_representation IN ITEMS
    src/backends/javascript/lir_representation.cpp
    src/backends/cpp/lir_representation.cpp)
  file(READ "${SOURCE_DIR}/${sparse_arithmetic_representation}"
       sparse_arithmetic_representation_contract)
  if(NOT sparse_arithmetic_representation_contract MATCHES
       "valid_sparse_arithmetic_plan" OR
     NOT sparse_arithmetic_representation_contract MATCHES "sparse_add" OR
     NOT sparse_arithmetic_representation_contract MATCHES "sparse_subtract" OR
     NOT sparse_arithmetic_representation_contract MATCHES "runtime_shape_arguments" OR
     NOT sparse_arithmetic_representation_contract MATCHES "runtime_integer_arguments" OR
     NOT sparse_arithmetic_representation_contract MATCHES
       "sparse_arithmetic\.value_domain")
    message(FATAL_ERROR
      "target representation does not own sparse arithmetic helper and ABI selection: "
      "${sparse_arithmetic_representation}")
  endif()
endforeach()
foreach(sparse_arithmetic_planning IN ITEMS
    src/backends/javascript/lir_planning.cpp
    src/backends/cpp/lir_planning.cpp)
  file(READ "${SOURCE_DIR}/${sparse_arithmetic_planning}"
       sparse_arithmetic_planning_contract)
  if(NOT sparse_arithmetic_planning_contract MATCHES
       "RuntimeFeature::sparse_arithmetic" OR
     NOT sparse_arithmetic_planning_contract MATCHES
       "RuntimeFragment::sparse_arithmetic" OR
     NOT sparse_arithmetic_planning_contract MATCHES
       "sparse-arithmetic runtime requires sparse-matrix support")
    message(FATAL_ERROR
      "target resource planner does not own the sparse arithmetic fragment dependency: "
      "${sparse_arithmetic_planning}")
  endif()
  string(FIND "${sparse_arithmetic_planning_contract}"
    "RuntimeFragment::sparse_matrices" sparse_matrix_fragment_index)
  string(FIND "${sparse_arithmetic_planning_contract}"
    "RuntimeFragment::sparse_arithmetic" sparse_arithmetic_fragment_index)
  if(sparse_matrix_fragment_index EQUAL -1 OR sparse_arithmetic_fragment_index EQUAL -1 OR
     sparse_arithmetic_fragment_index LESS sparse_matrix_fragment_index)
    message(FATAL_ERROR
      "sparse arithmetic fragment is planned before its sparse-matrix dependency: "
      "${sparse_arithmetic_planning}")
  endif()
endforeach()
foreach(renderer IN ITEMS src/backends/javascript/renderer.cpp src/backends/cpp/renderer.cpp)
  mpf_assert_file_excludes("${renderer}"
    "SparseArithmetic(Operation|StoragePolicy)|valid_sparse_arithmetic_contract|sparse_arithmetic"
    "target renderer recovered sparse arithmetic semantics from source plans")
endforeach()
file(READ "${SOURCE_DIR}/src/ir/semantics.hpp" sparse_power_semantic_contract)
foreach(required_sparse_power_semantic IN ITEMS
    "MatrixExponentPolicy"
    "sparse_csc_power"
    "matrix_exponent_policy"
    "valid_matrix_power_storage_contract")
  if(NOT sparse_power_semantic_contract MATCHES "${required_sparse_power_semantic}")
    message(FATAL_ERROR
      "sparse matrix power semantic contract is missing: ${required_sparse_power_semantic}")
  endif()
endforeach()
foreach(sparse_power_ir IN ITEMS
    src/ir/semantic_facts.hpp
    src/ir/mir.hpp
    src/backends/javascript/lir.hpp
    src/backends/cpp/lir.hpp)
  file(READ "${SOURCE_DIR}/${sparse_power_ir}" sparse_power_ir_contract)
  if(NOT sparse_power_ir_contract MATCHES "MatrixOperationPlan" OR
     NOT sparse_power_ir_contract MATCHES "MatrixExponentPolicy" OR
     NOT sparse_power_ir_contract MATCHES "exponent_policy")
    message(FATAL_ERROR
      "IR layer does not own the typed matrix exponent policy: ${sparse_power_ir}")
  endif()
endforeach()
file(READ "${SOURCE_DIR}/src/backends/common/lir_builder.hpp"
     sparse_power_lir_builder_contract)
if(NOT sparse_power_lir_builder_contract MATCHES
     "matrix_operation\.exponent_policy = attributes\.matrix_operation\.exponent_policy")
  message(FATAL_ERROR "target LIR builder does not propagate the matrix exponent policy")
endif()
file(READ "${SOURCE_DIR}/src/semantic/expression_analyzer.cpp" sparse_power_analyzer_contract)
file(READ "${SOURCE_DIR}/src/ir/mir.cpp" sparse_power_mir_lowering_contract)
file(READ "${SOURCE_DIR}/src/ir/mir_verifier.cpp" sparse_power_mir_verifier_contract)
if(NOT sparse_power_analyzer_contract MATCHES "sparse matrix power requires" OR
   NOT sparse_power_analyzer_contract MATCHES "valid_matrix_power_storage_contract" OR
   NOT sparse_power_analyzer_contract MATCHES "matrix_exponent_policy" OR
   NOT sparse_power_mir_lowering_contract MATCHES "matrix_operation\.exponent_policy" OR
   NOT sparse_power_mir_verifier_contract MATCHES "valid_matrix_power_storage_contract" OR
   NOT sparse_power_mir_verifier_contract MATCHES "matrix_exponent_policy")
  message(FATAL_ERROR
    "Analyzer and MIR do not independently own sparse matrix power storage and exponent plans")
endif()
foreach(sparse_power_representation IN ITEMS
    src/backends/javascript/lir_representation.cpp
    src/backends/cpp/lir_representation.cpp)
  file(READ "${SOURCE_DIR}/${sparse_power_representation}"
       sparse_power_representation_contract)
  if(NOT sparse_power_representation_contract MATCHES "sparse_mpower" OR
     NOT sparse_power_representation_contract MATCHES "valid_matrix_power_storage_contract" OR
     NOT sparse_power_representation_contract MATCHES "matrix_exponent_policy" OR
     NOT sparse_power_representation_contract MATCHES "runtime_shape_arguments" OR
     NOT sparse_power_representation_contract MATCHES "runtime_integer_arguments")
    message(FATAL_ERROR
      "target representation does not own sparse matrix power helper and ABI selection: "
      "${sparse_power_representation}")
  endif()
endforeach()
foreach(sparse_power_planning IN ITEMS
    src/backends/javascript/lir_planning.cpp
    src/backends/cpp/lir_planning.cpp)
  file(READ "${SOURCE_DIR}/${sparse_power_planning}" sparse_power_planning_contract)
  if(NOT sparse_power_planning_contract MATCHES "RuntimeFeature::sparse_power" OR
     NOT sparse_power_planning_contract MATCHES "RuntimeFeature::sparse_product" OR
     NOT sparse_power_planning_contract MATCHES "RuntimeFragment::sparse_power" OR
     NOT sparse_power_planning_contract MATCHES
       "sparse-power runtime requires sparse-matrix and sparse-product")
    message(FATAL_ERROR
      "target resource planner does not own the sparse-power fragment dependency: "
      "${sparse_power_planning}")
  endif()
  string(FIND "${sparse_power_planning_contract}"
    "RuntimeFragment::sparse_matrices" sparse_power_base_fragment_index)
  string(FIND "${sparse_power_planning_contract}"
    "RuntimeFragment::sparse_product" sparse_power_product_fragment_index)
  string(FIND "${sparse_power_planning_contract}"
    "RuntimeFragment::sparse_power" sparse_power_fragment_index)
  if(sparse_power_base_fragment_index EQUAL -1 OR
     sparse_power_product_fragment_index EQUAL -1 OR
     sparse_power_fragment_index EQUAL -1 OR
     sparse_power_product_fragment_index LESS sparse_power_base_fragment_index OR
     sparse_power_fragment_index LESS sparse_power_product_fragment_index)
    message(FATAL_ERROR
      "sparse-power fragment is planned before a sparse runtime dependency: "
      "${sparse_power_planning}")
  endif()
endforeach()
foreach(renderer IN ITEMS src/backends/javascript/renderer.cpp src/backends/cpp/renderer.cpp)
  mpf_assert_file_excludes("${renderer}"
    "MatrixExponentPolicy|sparse_csc_power|valid_matrix_power_storage_contract|sparse_mpower"
    "target renderer recovered sparse matrix power semantics from source plans")
endforeach()
file(READ "${SOURCE_DIR}/src/backends/common/lir_dump.hpp" target_lir_dump_contract)
if(NOT target_lir_dump_contract MATCHES "runtime_integer_arguments" OR
   NOT target_lir_dump_contract MATCHES "runtime-integer-arguments")
  message(FATAL_ERROR "deterministic target LIR dump omits runtime integer ABI arguments")
endif()
foreach(representation IN ITEMS
    src/backends/javascript/lir_representation.cpp
    src/backends/cpp/lir_representation.cpp)
  file(READ "${SOURCE_DIR}/${representation}" condition_representation_contract)
  string(FIND "${condition_representation_contract}"
    "condition_policy != semantic::matrix_condition_policy(plan.solve)"
    condition_policy_verification)
  string(FIND "${condition_representation_contract}"
    "semantic::matrix_factorization_policy(plan.solve, plan.storage_policy)"
    factorization_policy_verification)
  string(FIND "${condition_representation_contract}"
    "plan.solve, plan.numeric_domain, plan.storage_policy"
    structure_policy_verification)
  string(FIND "${condition_representation_contract}"
    "semantic::matrix_storage_policy(plan.operation, plan.left_storage, plan.right_storage)"
    storage_policy_verification)
  if(condition_policy_verification EQUAL -1 OR
     factorization_policy_verification EQUAL -1 OR
     structure_policy_verification EQUAL -1 OR
     storage_policy_verification EQUAL -1)
    message(FATAL_ERROR
      "target representation verifier does not enforce matrix policies: ${representation}")
  endif()
endforeach()
foreach(matrix_runtime IN ITEMS
    src/backends/javascript/matrix_runtime.cpp
    src/backends/cpp/matrix_runtime.cpp)
  file(READ "${SOURCE_DIR}/${matrix_runtime}" condition_runtime_contract)
  if(NOT condition_runtime_contract MATCHES "lu_factor" OR
     NOT condition_runtime_contract MATCHES "lu_apply_transpose" OR
     NOT condition_runtime_contract MATCHES "lu_rcond" OR
     NOT condition_runtime_contract MATCHES "singular to working precision" OR
     NOT condition_runtime_contract MATCHES "close to singular or badly scaled" OR
     NOT condition_runtime_contract MATCHES "(square_structure|classify_square_structure)" OR
     NOT condition_runtime_contract MATCHES "diagonal_(apply|rcond)" OR
     NOT condition_runtime_contract MATCHES "triangular_(apply|rcond)" OR
     NOT condition_runtime_contract MATCHES "tridiagonal_factor" OR
     NOT condition_runtime_contract MATCHES "tridiagonal_apply_transpose" OR
     NOT condition_runtime_contract MATCHES "tridiagonal_rcond" OR
     NOT condition_runtime_contract MATCHES "cholesky_factor" OR
     NOT condition_runtime_contract MATCHES "cholesky_apply" OR
     NOT condition_runtime_contract MATCHES "cholesky_rcond" OR
     NOT condition_runtime_contract MATCHES "structured_real_square_solve" OR
     NOT condition_runtime_contract MATCHES "basic_least_squares" OR
     NOT condition_runtime_contract MATCHES "rank deficient to working precision")
    message(FATAL_ERROR
      "target matrix runtime does not provide real structured square and basic least-squares "
      "kernels: "
      "${matrix_runtime}")
  endif()
  mpf_assert_file_excludes("${matrix_runtime}" "minimum[_ -]?norm"
    "target matrix runtime restored the incorrect underdetermined minimum-norm contract")
  mpf_assert_file_excludes("${matrix_runtime}" "structured_square_solve"
    "target matrix runtime restored the obsolete diagonal/triangular-only helper contract")
endforeach()
foreach(complex_matrix_runtime IN ITEMS
    src/backends/javascript/complex_matrix_runtime.cpp
    src/backends/cpp/complex_matrix_runtime.cpp)
  if(NOT EXISTS "${SOURCE_DIR}/${complex_matrix_runtime}")
    message(FATAL_ERROR "target complex-matrix runtime is missing: ${complex_matrix_runtime}")
  endif()
  file(READ "${SOURCE_DIR}/${complex_matrix_runtime}" complex_matrix_runtime_contract)
  if(NOT complex_matrix_runtime_contract MATCHES "complex_mtimes" OR
     NOT complex_matrix_runtime_contract MATCHES "complex_lu_factor" OR
     NOT complex_matrix_runtime_contract MATCHES "complex_lu_rcond" OR
     NOT complex_matrix_runtime_contract MATCHES "is_hermitian" OR
     NOT complex_matrix_runtime_contract MATCHES "complex_cholesky_factor" OR
     NOT complex_matrix_runtime_contract MATCHES "complex_cholesky_rcond" OR
     NOT complex_matrix_runtime_contract MATCHES "complex_cpqr_factor" OR
     NOT complex_matrix_runtime_contract MATCHES "apply_complex_cpqr_qh" OR
     NOT complex_matrix_runtime_contract MATCHES "complex_basic_least_squares" OR
     NOT complex_matrix_runtime_contract MATCHES "complex_ctranspose" OR
     NOT complex_matrix_runtime_contract MATCHES "structured_complex_square_solve" OR
     NOT complex_matrix_runtime_contract MATCHES "mrdivide_structured_complex_square" OR
     NOT complex_matrix_runtime_contract MATCHES "complex_mpower")
    message(FATAL_ERROR
      "target complex-matrix runtime does not own multiply, Hermitian/LU/CPQR solve, "
      "right-division, condition and integer-power kernels: ${complex_matrix_runtime}")
  endif()
  mpf_assert_file_excludes("${complex_matrix_runtime}"
    "TranspileOptions|SourceLanguage::|[./]ir/(hir|mir)\\.hpp"
    "target complex-matrix runtime depends on compiler state")
  mpf_assert_file_excludes("${complex_matrix_runtime}" "minimum[_ -]?norm"
    "target complex-matrix runtime restored the incorrect underdetermined minimum-norm contract")
endforeach()
foreach(sparse_matrix_runtime IN ITEMS
    src/backends/javascript/sparse_matrix_runtime.cpp
    src/backends/cpp/sparse_matrix_runtime.cpp)
  if(NOT EXISTS "${SOURCE_DIR}/${sparse_matrix_runtime}")
    message(FATAL_ERROR "target sparse-matrix runtime is missing: ${sparse_matrix_runtime}")
  endif()
  file(READ "${SOURCE_DIR}/${sparse_matrix_runtime}" sparse_matrix_runtime_contract)
  if(NOT sparse_matrix_runtime_contract MATCHES "validate_sparse_csc" OR
     NOT sparse_matrix_runtime_contract MATCHES "(column_pointers|columnPointers)" OR
     NOT sparse_matrix_runtime_contract MATCHES "(row_indices|rowIndices)" OR
     NOT sparse_matrix_runtime_contract MATCHES "sparse_from_triplets" OR
     NOT sparse_matrix_runtime_contract MATCHES "triplet indices must be positive safe integers" OR
     NOT sparse_matrix_runtime_contract MATCHES "duplicate accumulation is not finite" OR
     NOT sparse_matrix_runtime_contract MATCHES
       "(sparse_value_logical|sparse_logical_any)" OR
     NOT sparse_matrix_runtime_contract MATCHES
       "(sparse_duplicate_logical_any|LogicalAny)" OR
     NOT sparse_matrix_runtime_contract MATCHES "sparse_transpose" OR
     NOT sparse_matrix_runtime_contract MATCHES "sparse_linear_element" OR
     NOT sparse_matrix_runtime_contract MATCHES "sparse_subscript_element" OR
     NOT sparse_matrix_runtime_contract MATCHES "sparse_linear_selection" OR
     NOT sparse_matrix_runtime_contract MATCHES "sparse_submatrix_selection" OR
     NOT sparse_matrix_runtime_contract MATCHES "sparse_is_full_slice" OR
     NOT sparse_matrix_runtime_contract MATCHES "sparse_assign" OR
     NOT sparse_matrix_runtime_contract MATCHES "sparse_erase" OR
     NOT sparse_matrix_runtime_contract MATCHES "sparse_scale_right" OR
     NOT sparse_matrix_runtime_contract MATCHES "sparse_scale_left" OR
     NOT sparse_matrix_runtime_contract MATCHES "sparse_times_scalar_right" OR
     NOT sparse_matrix_runtime_contract MATCHES "sparse_times_scalar_left" OR
     NOT sparse_matrix_runtime_contract MATCHES "sparse_times_dense" OR
     NOT sparse_matrix_runtime_contract MATCHES "dense_times_sparse" OR
     NOT sparse_matrix_runtime_contract MATCHES "sparse_times_sparse" OR
     NOT sparse_matrix_runtime_contract MATCHES "sparse_logical_not" OR
     NOT sparse_matrix_runtime_contract MATCHES "sparse_logical_and" OR
     NOT sparse_matrix_runtime_contract MATCHES "sparse_logical_or" OR
     NOT sparse_matrix_runtime_contract MATCHES
       "sparse logical storage plan is inconsistent" OR
     NOT sparse_matrix_runtime_contract MATCHES
       "sparse element-wise multiplication produced a nonfinite value" OR
     NOT sparse_matrix_runtime_contract MATCHES "sparse_tridiagonal_factor" OR
     NOT sparse_matrix_runtime_contract MATCHES "sparse_row_lu_factor" OR
     NOT sparse_matrix_runtime_contract MATCHES "sparse_rcond" OR
     NOT sparse_matrix_runtime_contract MATCHES "sparse_solve_shape" OR
     NOT sparse_matrix_runtime_contract MATCHES "sparse_mldivide_plan" OR
     NOT sparse_matrix_runtime_contract MATCHES "mldivide_sparse_real_square" OR
     NOT sparse_matrix_runtime_contract MATCHES "mrdivide_sparse_real_square")
    message(FATAL_ERROR
      "target sparse-matrix runtime does not own canonical CSC construction/index/mutation, "
      "sparse scale/element-wise logical/square-solve, transpose, and condition "
      "kernels: ${sparse_matrix_runtime}")
  endif()
  mpf_assert_file_excludes("${sparse_matrix_runtime}"
    "TranspileOptions|SourceLanguage::|[./]ir/(hir|mir)\\.hpp"
    "target sparse-matrix runtime depends on compiler state")
  mpf_assert_file_excludes("${sparse_matrix_runtime}"
    "sparse_logical_reduce|sparse logical reduction storage plan is invalid"
    "target sparse-matrix base runtime absorbed the optional sparse-reduction fragment")
endforeach()
foreach(sparse_reduction_runtime IN ITEMS
    src/backends/javascript/sparse_reduction_runtime.cpp
    src/backends/cpp/sparse_reduction_runtime.cpp)
  if(NOT EXISTS "${SOURCE_DIR}/${sparse_reduction_runtime}")
    message(FATAL_ERROR
      "target sparse-reduction runtime is missing: ${sparse_reduction_runtime}")
  endif()
  file(READ "${SOURCE_DIR}/${sparse_reduction_runtime}" sparse_reduction_runtime_contract)
  if(NOT sparse_reduction_runtime_contract MATCHES "sparse_logical_reduce" OR
     NOT sparse_reduction_runtime_contract MATCHES "validate_sparse_csc" OR
     NOT sparse_reduction_runtime_contract MATCHES "sparse_logical_(rows|columns)" OR
     NOT sparse_reduction_runtime_contract MATCHES
       "sparse logical reduction storage plan is invalid")
    message(FATAL_ERROR
      "target sparse-reduction fragment does not own the CSC reduction kernels and storage "
      "validation: ${sparse_reduction_runtime}")
  endif()
  mpf_assert_file_excludes("${sparse_reduction_runtime}"
    "TranspileOptions|SourceLanguage::|[./]ir/(hir|mir)\\.hpp"
    "target sparse-reduction runtime depends on compiler state")
endforeach()
foreach(sparse_arithmetic_runtime IN ITEMS
    src/backends/javascript/sparse_arithmetic_runtime.cpp
    src/backends/cpp/sparse_arithmetic_runtime.cpp)
  if(NOT EXISTS "${SOURCE_DIR}/${sparse_arithmetic_runtime}")
    message(FATAL_ERROR
      "target sparse-arithmetic runtime is missing: ${sparse_arithmetic_runtime}")
  endif()
  file(READ "${SOURCE_DIR}/${sparse_arithmetic_runtime}"
       sparse_arithmetic_runtime_contract)
  if(NOT sparse_arithmetic_runtime_contract MATCHES
       "emit_(javascript|cpp)_sparse_arithmetic_runtime" OR
     NOT sparse_arithmetic_runtime_contract MATCHES "sparse_(add|subtract)" OR
     NOT sparse_arithmetic_runtime_contract MATCHES "validate_sparse_csc" OR
     NOT sparse_arithmetic_runtime_contract MATCHES
       "sparse_arithmetic_(preserve|materialize)" OR
     NOT sparse_arithmetic_runtime_contract MATCHES
       "sparse arithmetic storage plan is invalid" OR
     NOT sparse_arithmetic_runtime_contract MATCHES
       "sparse arithmetic value-domain plan is invalid" OR
     NOT sparse_arithmetic_runtime_contract MATCHES "value(_d|D)omain" OR
     NOT sparse_arithmetic_runtime_contract MATCHES
       "sparse arithmetic produced a nonfinite value")
    message(FATAL_ERROR
      "target sparse-arithmetic fragment does not own storage, shape, numeric and CSC kernels: "
      "${sparse_arithmetic_runtime}")
  endif()
  mpf_assert_file_excludes("${sparse_arithmetic_runtime}"
    "TranspileOptions|SourceLanguage::|[./]ir/(hir|mir)\\.hpp"
    "target sparse-arithmetic runtime depends on compiler state")
endforeach()
foreach(sparse_matrix_runtime IN ITEMS
    src/backends/javascript/sparse_matrix_runtime.cpp
    src/backends/cpp/sparse_matrix_runtime.cpp)
  mpf_assert_file_excludes("${sparse_matrix_runtime}"
    "sparse_arithmetic|sparse_(add|subtract)|sparse arithmetic storage plan"
    "target sparse-matrix base runtime absorbed the optional sparse-arithmetic fragment")
  mpf_assert_file_excludes("${sparse_matrix_runtime}"
    "sparse_power|sparse_mpower|sparse matrix power plan"
    "target sparse-matrix base runtime absorbed the optional sparse-power fragment")
  mpf_assert_file_excludes("${sparse_matrix_runtime}"
    "sparse_(product|sparse_mtimes|dense_mtimes)|dense_sparse_mtimes|sparse matrix product"
    "target sparse-matrix base runtime absorbed the optional sparse-product fragment")
  mpf_assert_file_excludes("${sparse_matrix_runtime}"
    "function __mpf_sparse_ctranspose|sparse_matrix<T> sparse_ctranspose"
    "target sparse-matrix base runtime absorbed the complex-sparse fragment")
endforeach()
foreach(sparse_product_runtime IN ITEMS
    src/backends/javascript/sparse_product_runtime.cpp
    src/backends/cpp/sparse_product_runtime.cpp)
  if(NOT EXISTS "${SOURCE_DIR}/${sparse_product_runtime}")
    message(FATAL_ERROR "target sparse-product runtime is missing: ${sparse_product_runtime}")
  endif()
  file(READ "${SOURCE_DIR}/${sparse_product_runtime}" sparse_product_runtime_contract)
  if(NOT sparse_product_runtime_contract MATCHES
       "emit_(javascript|cpp)_sparse_product_runtime" OR
     NOT sparse_product_runtime_contract MATCHES "sparse_sparse_mtimes" OR
     NOT sparse_product_runtime_contract MATCHES "sparse_dense_mtimes" OR
     NOT sparse_product_runtime_contract MATCHES "dense_sparse_mtimes" OR
     NOT sparse_product_runtime_contract MATCHES "sparse_product_accumulate" OR
     NOT sparse_product_runtime_contract MATCHES
       "sparse matrix product shape plans are inconsistent" OR
     NOT sparse_product_runtime_contract MATCHES
       "sparse matrix product numeric-domain plan is invalid")
    message(FATAL_ERROR
      "target sparse-product fragment does not own shape, numeric-domain and storage kernels: "
      "${sparse_product_runtime}")
  endif()
  mpf_assert_file_excludes("${sparse_product_runtime}"
    "TranspileOptions|SourceLanguage::|[./]ir/(hir|mir)\\.hpp"
    "target sparse-product runtime depends on compiler state")
endforeach()
foreach(sparse_product_runtime_catalog IN ITEMS
    src/backends/javascript/runtime.cpp
    src/backends/cpp/runtime.cpp)
  file(READ "${SOURCE_DIR}/${sparse_product_runtime_catalog}"
       sparse_product_runtime_catalog_contract)
  if(NOT sparse_product_runtime_catalog_contract MATCHES
       "RuntimeFragment::sparse_product" OR
     NOT sparse_product_runtime_catalog_contract MATCHES
       "emit_(javascript|cpp)_sparse_product_runtime")
    message(FATAL_ERROR
      "target runtime catalog does not serialize the sparse-product fragment: "
      "${sparse_product_runtime_catalog}")
  endif()
  string(FIND "${sparse_product_runtime_catalog_contract}"
    "emit_javascript_sparse_matrix_runtime" javascript_sparse_product_base_emit_index)
  string(FIND "${sparse_product_runtime_catalog_contract}"
    "emit_javascript_sparse_product_runtime" javascript_sparse_product_emit_index)
  string(FIND "${sparse_product_runtime_catalog_contract}"
    "emit_cpp_sparse_matrix_runtime" cpp_sparse_product_base_emit_index)
  string(FIND "${sparse_product_runtime_catalog_contract}"
    "emit_cpp_sparse_product_runtime" cpp_sparse_product_emit_index)
  if((javascript_sparse_product_emit_index GREATER -1 AND
      (javascript_sparse_product_base_emit_index EQUAL -1 OR
       javascript_sparse_product_emit_index LESS javascript_sparse_product_base_emit_index)) OR
     (cpp_sparse_product_emit_index GREATER -1 AND
      (cpp_sparse_product_base_emit_index EQUAL -1 OR
       cpp_sparse_product_emit_index LESS cpp_sparse_product_base_emit_index)))
    message(FATAL_ERROR
      "target runtime emits sparse product before its sparse-matrix dependency: "
      "${sparse_product_runtime_catalog}")
  endif()
endforeach()
foreach(complex_sparse_runtime IN ITEMS
    src/backends/javascript/complex_sparse_runtime.cpp
    src/backends/cpp/complex_sparse_runtime.cpp)
  if(NOT EXISTS "${SOURCE_DIR}/${complex_sparse_runtime}")
    message(FATAL_ERROR
      "target complex-sparse runtime is missing: ${complex_sparse_runtime}")
  endif()
  file(READ "${SOURCE_DIR}/${complex_sparse_runtime}" complex_sparse_runtime_contract)
  if(NOT complex_sparse_runtime_contract MATCHES
       "emit_(javascript|cpp)_complex_sparse_runtime" OR
     NOT complex_sparse_runtime_contract MATCHES "sparse_ctranspose")
    message(FATAL_ERROR
      "target complex-sparse fragment does not own conjugate CSC transpose: "
      "${complex_sparse_runtime}")
  endif()
  mpf_assert_file_excludes("${complex_sparse_runtime}"
    "TranspileOptions|SourceLanguage::|[./]ir/(hir|mir)\\.hpp"
    "target complex-sparse runtime depends on compiler state")
endforeach()
foreach(complex_sparse_runtime_catalog IN ITEMS
    src/backends/javascript/runtime.cpp
    src/backends/cpp/runtime.cpp)
  file(READ "${SOURCE_DIR}/${complex_sparse_runtime_catalog}"
       complex_sparse_runtime_catalog_contract)
  if(NOT complex_sparse_runtime_catalog_contract MATCHES
       "RuntimeFragment::complex_sparse" OR
     NOT complex_sparse_runtime_catalog_contract MATCHES
       "emit_(javascript|cpp)_complex_sparse_runtime")
    message(FATAL_ERROR
      "target runtime catalog does not serialize the complex-sparse fragment: "
      "${complex_sparse_runtime_catalog}")
  endif()
endforeach()
foreach(complex_sparse_target IN ITEMS javascript cpp)
  file(READ "${SOURCE_DIR}/src/backends/${complex_sparse_target}/lir.hpp"
       complex_sparse_lir_contract)
  file(READ "${SOURCE_DIR}/src/backends/${complex_sparse_target}/lowering.cpp"
       complex_sparse_lowering_contract)
  file(READ "${SOURCE_DIR}/src/backends/${complex_sparse_target}/lir_planning.cpp"
       complex_sparse_planning_contract)
  if(NOT complex_sparse_lir_contract MATCHES "RuntimeFeature[^}]*complex_sparse" OR
     NOT complex_sparse_lowering_contract MATCHES
       "RuntimeFeature::complex_sparse" OR
     NOT complex_sparse_planning_contract MATCHES
       "RuntimeFeature::complex_sparse")
    message(FATAL_ERROR
      "${complex_sparse_target} does not own an explicit complex-sparse feature and fragment plan")
  endif()
endforeach()
file(READ "${SOURCE_DIR}/src/backends/javascript/lir.hpp"
     javascript_value_ownership_lir)
file(READ "${SOURCE_DIR}/src/backends/javascript/lir_representation.cpp"
     javascript_value_ownership_planning)
file(READ "${SOURCE_DIR}/src/backends/javascript/renderer.cpp"
     javascript_value_ownership_renderer)
file(READ "${SOURCE_DIR}/src/backends/javascript/sparse_matrix_runtime.cpp"
     javascript_value_ownership_sparse_runtime)
if(NOT javascript_value_ownership_lir MATCHES "AssignmentValueForm" OR
   NOT javascript_value_ownership_lir MATCHES "MutationOwnership" OR
   NOT javascript_value_ownership_planning MATCHES "copy_array" OR
   NOT javascript_value_ownership_planning MATCHES "replace_with_result" OR
   NOT javascript_value_ownership_renderer MATCHES "__mpf_copy_array" OR
   NOT javascript_value_ownership_sparse_runtime MATCHES
     "return __mpf_make_sparse_csc")
  message(FATAL_ERROR
    "JavaScript target LIR does not own Matlab dense-copy and immutable sparse-mutation policy")
endif()
foreach(sparse_power_runtime IN ITEMS
    src/backends/javascript/sparse_power_runtime.cpp
    src/backends/cpp/sparse_power_runtime.cpp)
  if(NOT EXISTS "${SOURCE_DIR}/${sparse_power_runtime}")
    message(FATAL_ERROR "target sparse-power runtime is missing: ${sparse_power_runtime}")
  endif()
  file(READ "${SOURCE_DIR}/${sparse_power_runtime}" sparse_power_runtime_contract)
  if(NOT sparse_power_runtime_contract MATCHES
       "emit_(javascript|cpp)_sparse_power_runtime" OR
     NOT sparse_power_runtime_contract MATCHES "sparse_mpower" OR
     NOT sparse_power_runtime_contract MATCHES "sparse_power_identity" OR
     NOT sparse_power_runtime_contract MATCHES "sparse_power_exponent" OR
     NOT sparse_power_runtime_contract MATCHES "sparse_sparse_mtimes" OR
     NOT sparse_power_runtime_contract MATCHES
       "sparse matrix power plan is inconsistent" OR
     NOT sparse_power_runtime_contract MATCHES
       "nonnegative safe integer exponent")
    message(FATAL_ERROR
      "target sparse-power fragment does not own exponent, identity, plan and repeated-square "
      "kernels: ${sparse_power_runtime}")
  endif()
  mpf_assert_file_excludes("${sparse_power_runtime}"
    "TranspileOptions|SourceLanguage::|[./]ir/(hir|mir)\\.hpp"
    "target sparse-power runtime depends on compiler state")
endforeach()
foreach(sparse_power_runtime_catalog IN ITEMS
    src/backends/javascript/runtime.cpp
    src/backends/cpp/runtime.cpp)
  file(READ "${SOURCE_DIR}/${sparse_power_runtime_catalog}"
       sparse_power_runtime_catalog_contract)
  if(NOT sparse_power_runtime_catalog_contract MATCHES "RuntimeFragment::sparse_power" OR
     NOT sparse_power_runtime_catalog_contract MATCHES
       "emit_(javascript|cpp)_sparse_power_runtime")
    message(FATAL_ERROR
      "target runtime catalog does not serialize the sparse-power fragment: "
      "${sparse_power_runtime_catalog}")
  endif()
  string(FIND "${sparse_power_runtime_catalog_contract}"
    "emit_javascript_sparse_matrix_runtime" javascript_sparse_power_base_emit_index)
  string(FIND "${sparse_power_runtime_catalog_contract}"
    "emit_javascript_sparse_product_runtime" javascript_sparse_power_product_emit_index)
  string(FIND "${sparse_power_runtime_catalog_contract}"
    "emit_javascript_sparse_power_runtime" javascript_sparse_power_emit_index)
  string(FIND "${sparse_power_runtime_catalog_contract}"
    "emit_cpp_sparse_matrix_runtime" cpp_sparse_power_base_emit_index)
  string(FIND "${sparse_power_runtime_catalog_contract}"
    "emit_cpp_sparse_product_runtime" cpp_sparse_power_product_emit_index)
  string(FIND "${sparse_power_runtime_catalog_contract}"
    "emit_cpp_sparse_power_runtime" cpp_sparse_power_emit_index)
  if((javascript_sparse_power_emit_index GREATER -1 AND
      (javascript_sparse_power_base_emit_index EQUAL -1 OR
       javascript_sparse_power_product_emit_index EQUAL -1 OR
       javascript_sparse_power_product_emit_index LESS
         javascript_sparse_power_base_emit_index OR
       javascript_sparse_power_emit_index LESS
         javascript_sparse_power_product_emit_index)) OR
     (cpp_sparse_power_emit_index GREATER -1 AND
      (cpp_sparse_power_base_emit_index EQUAL -1 OR
       cpp_sparse_power_product_emit_index EQUAL -1 OR
       cpp_sparse_power_product_emit_index LESS cpp_sparse_power_base_emit_index OR
       cpp_sparse_power_emit_index LESS cpp_sparse_power_product_emit_index)))
    message(FATAL_ERROR
      "target runtime emits sparse power before its sparse-matrix dependency: "
      "${sparse_power_runtime_catalog}")
  endif()
endforeach()
foreach(sparse_arithmetic_runtime_catalog IN ITEMS
    src/backends/javascript/runtime.cpp
    src/backends/cpp/runtime.cpp)
  file(READ "${SOURCE_DIR}/${sparse_arithmetic_runtime_catalog}"
       sparse_arithmetic_runtime_catalog_contract)
  if(NOT sparse_arithmetic_runtime_catalog_contract MATCHES
       "RuntimeFragment::sparse_arithmetic" OR
     NOT sparse_arithmetic_runtime_catalog_contract MATCHES
       "emit_(javascript|cpp)_sparse_arithmetic_runtime")
    message(FATAL_ERROR
      "target runtime catalog does not serialize the sparse-arithmetic fragment: "
      "${sparse_arithmetic_runtime_catalog}")
  endif()
  string(FIND "${sparse_arithmetic_runtime_catalog_contract}"
    "emit_javascript_sparse_matrix_runtime" javascript_sparse_matrix_emit_index)
  string(FIND "${sparse_arithmetic_runtime_catalog_contract}"
    "emit_javascript_sparse_arithmetic_runtime" javascript_sparse_arithmetic_emit_index)
  string(FIND "${sparse_arithmetic_runtime_catalog_contract}"
    "emit_cpp_sparse_matrix_runtime" cpp_sparse_matrix_emit_index)
  string(FIND "${sparse_arithmetic_runtime_catalog_contract}"
    "emit_cpp_sparse_arithmetic_runtime" cpp_sparse_arithmetic_emit_index)
  if((javascript_sparse_arithmetic_emit_index GREATER -1 AND
      (javascript_sparse_matrix_emit_index EQUAL -1 OR
       javascript_sparse_arithmetic_emit_index LESS javascript_sparse_matrix_emit_index)) OR
     (cpp_sparse_arithmetic_emit_index GREATER -1 AND
      (cpp_sparse_matrix_emit_index EQUAL -1 OR
       cpp_sparse_arithmetic_emit_index LESS cpp_sparse_matrix_emit_index)))
    message(FATAL_ERROR
      "target runtime emits sparse arithmetic before its sparse-matrix dependency: "
      "${sparse_arithmetic_runtime_catalog}")
  endif()
endforeach()
foreach(sparse_representation IN ITEMS
    src/backends/javascript/lir_representation.cpp
    src/backends/cpp/lir_representation.cpp)
  file(READ "${SOURCE_DIR}/${sparse_representation}" sparse_representation_contract)
  if(NOT sparse_representation_contract MATCHES "valid_sparse_construction" OR
     NOT sparse_representation_contract MATCHES "matlab_sparse_transpose" OR
     NOT sparse_representation_contract MATCHES "valid_sparse_index_contract" OR
     NOT sparse_representation_contract MATCHES "matlab_sparse_index" OR
     NOT sparse_representation_contract MATCHES "valid_sparse_mutation_contract" OR
     NOT sparse_representation_contract MATCHES "sparse_mutation" OR
     NOT sparse_representation_contract MATCHES "sparse_csc_multiply" OR
     NOT sparse_representation_contract MATCHES "sparse_csc_scale" OR
     NOT sparse_representation_contract MATCHES "sparse_scale_right" OR
     NOT sparse_representation_contract MATCHES "sparse_scale_left" OR
     NOT sparse_representation_contract MATCHES "valid_sparse_elementwise_plan" OR
     NOT sparse_representation_contract MATCHES "sparse_times_scalar_right" OR
     NOT sparse_representation_contract MATCHES "sparse_times_scalar_left" OR
     NOT sparse_representation_contract MATCHES "sparse_times_dense" OR
     NOT sparse_representation_contract MATCHES "dense_times_sparse" OR
     NOT sparse_representation_contract MATCHES "sparse_times_sparse" OR
     NOT sparse_representation_contract MATCHES "valid_sparse_logical_plan" OR
     NOT sparse_representation_contract MATCHES "sparse_logical_not" OR
     NOT sparse_representation_contract MATCHES "sparse_logical_and" OR
     NOT sparse_representation_contract MATCHES "sparse_logical_or" OR
     NOT sparse_representation_contract MATCHES
       "valid_logical_reduction_storage_contract" OR
     NOT sparse_representation_contract MATCHES "sparse_logical_reduce" OR
     NOT sparse_representation_contract MATCHES "runtime_integer_arguments" OR
     NOT sparse_representation_contract MATCHES "sparse_sparse_mtimes" OR
     NOT sparse_representation_contract MATCHES "sparse_dense_mtimes" OR
     NOT sparse_representation_contract MATCHES "dense_sparse_mtimes" OR
     NOT sparse_representation_contract MATCHES "matrix_operation.numeric_domain" OR
     NOT sparse_representation_contract MATCHES "runtime_shape_arguments")
    message(FATAL_ERROR
      "target representation does not verify sparse construction/index/mutation/scale/element-wise/logical/product plans or select sparse forms: "
      "${sparse_representation}")
  endif()
endforeach()
foreach(complex_sparse_product_target IN ITEMS javascript cpp)
  file(READ
    "${SOURCE_DIR}/src/backends/${complex_sparse_product_target}/lowering.cpp"
    complex_sparse_product_lowering_contract)
  if(NOT complex_sparse_product_lowering_contract MATCHES
       "MatrixNumericDomain::complex" OR
     NOT complex_sparse_product_lowering_contract MATCHES "sparse_csc_multiply" OR
     NOT complex_sparse_product_lowering_contract MATCHES
       "RuntimeFeature::complex_numbers" OR
     NOT complex_sparse_product_lowering_contract MATCHES
       "RuntimeFeature::sparse_product")
    message(FATAL_ERROR
      "${complex_sparse_product_target} lowering does not own complex sparse-product runtime pruning")
  endif()
endforeach()
foreach(renderer IN ITEMS src/backends/javascript/renderer.cpp src/backends/cpp/renderer.cpp)
  mpf_assert_file_excludes("${renderer}"
    "Sparse(ConstructionKind|ValueDomain|DuplicatePolicy)|sparse_construction"
    "target renderer recovered sparse-construction semantics from source plans")
  mpf_assert_file_excludes("${renderer}" "MatrixStoragePolicy|sparse_csc_(multiply|scale)"
    "target renderer recovered sparse-product semantics from source plans")
  mpf_assert_file_excludes("${renderer}"
    "SparseElementwise(Operation|StoragePolicy)|valid_sparse_elementwise_contract"
    "target renderer recovered sparse element-wise semantics from source plans")
  mpf_assert_file_excludes("${renderer}"
    "SparseLogical(Operation|StoragePolicy)|valid_sparse_logical_contract|sparse_logical_result_storage"
    "target renderer recovered sparse logical semantics from source plans")
  mpf_assert_file_excludes("${renderer}"
    "Reduction(StoragePolicy|AxisPolicy|ShapeSource)|valid_logical_reduction|(plan|expression)\.reduction|reduction\.(operation|axis|shape|storage|scalar)"
    "target renderer recovered logical reduction semantics from source plans")
  file(READ "${SOURCE_DIR}/${renderer}" renderer_contract)
  if(NOT renderer_contract MATCHES "runtime_shape_arguments" OR
     NOT renderer_contract MATCHES "runtime_integer_arguments")
    message(FATAL_ERROR
      "target renderer does not serialize verified target-call shape and integer ABIs: ${renderer}")
  endif()
endforeach()
file(READ "${SOURCE_DIR}/src/backends/javascript/lir.hpp" javascript_sparse_call_abi)
file(READ "${SOURCE_DIR}/src/backends/javascript/lir_representation.cpp"
     javascript_sparse_call_planner)
if(NOT javascript_sparse_call_abi MATCHES "runtime_integer_arguments" OR
   NOT javascript_sparse_call_planner MATCHES
     "runtime_integer_arguments =" OR
   NOT javascript_sparse_call_planner MATCHES "__mpf_sparse_from_dense")
  message(FATAL_ERROR
    "JavaScript target LIR does not own the logical sparse runtime integer ABI")
endif()
file(READ "${SOURCE_DIR}/src/backends/cpp/lir_representation.cpp" cpp_sparse_call_planner)
if(NOT cpp_sparse_call_planner MATCHES "sparse_logical_from_dense" OR
   NOT cpp_sparse_call_planner MATCHES "sparse_logical_any")
  message(FATAL_ERROR "cpp target LIR does not own logical sparse helper selection")
endif()
mpf_assert_file_excludes("src/backends/javascript/runtime.cpp"
  "function __mpf_matlab_lu_"
  "generic JavaScript runtime regained matrix factorization ownership")
mpf_assert_file_excludes("src/backends/javascript/runtime.cpp"
  "function __mpf_matlab_complex_(lu|cholesky|mtimes|mpower)"
  "generic JavaScript runtime regained complex-matrix kernel ownership")
mpf_assert_file_excludes("src/backends/cpp/runtime.cpp"
  "inline[^\n]*matlab_lu_(factor|solve|rcond)"
  "generic cpp runtime regained matrix factorization ownership")
mpf_assert_file_excludes("src/backends/cpp/runtime.cpp"
  "inline[^\n]*matlab_complex_(lu|cholesky|mtimes|mpower)"
  "generic cpp runtime regained complex-matrix kernel ownership")
mpf_assert_file_excludes("src/backends/javascript/runtime.cpp"
  "function __mpf_(validate_sparse_csc|sparse_row_lu|sparse_tridiagonal|sparse_sparse_mtimes|sparse_dense_mtimes|dense_sparse_mtimes)"
  "generic JavaScript runtime regained sparse-matrix kernel ownership")
mpf_assert_file_excludes("src/backends/cpp/runtime.cpp"
  "(struct|inline|template)[^\n]*(validate_sparse_csc|sparse_row_lu|sparse_tridiagonal|sparse_sparse_mtimes|sparse_dense_mtimes|dense_sparse_mtimes)"
  "generic cpp runtime regained sparse-matrix kernel ownership")
file(READ "${SOURCE_DIR}/src/backends/javascript/runtime.cpp" javascript_runtime_contract)
file(READ "${SOURCE_DIR}/src/backends/cpp/runtime.cpp" cpp_runtime_contract)
if(NOT javascript_runtime_contract MATCHES
     "column-major coordinates require positive safe extents" OR
   NOT javascript_runtime_contract MATCHES
     "__mpf_column_major_coordinates\\(linear, shape\\)" OR
   NOT cpp_runtime_contract MATCHES
     "column-major coordinates require nonzero extents" OR
   cpp_runtime_contract MATCHES "linear % shape\\[axis\\]")
  message(FATAL_ERROR
    "target coordinate runtimes do not fail closed before zero-extent division")
endif()

foreach(renderer IN ITEMS src/backends/javascript/renderer.cpp src/backends/cpp/renderer.cpp)
  file(READ "${SOURCE_DIR}/${renderer}" renderer_contract)
  if(NOT renderer_contract MATCHES "plan\\.call_arguments" OR
     NOT renderer_contract MATCHES "plan\\.form" OR
     NOT renderer_contract MATCHES "plan\\.token" OR
     NOT renderer_contract MATCHES "plan\\.index" OR
     NOT renderer_contract MATCHES "plan\\.index_extents" OR
     NOT renderer_contract MATCHES "plan\\.sparse_index\\.kind" OR
     NOT renderer_contract MATCHES "plan\\.sparse_mutation" OR
     NOT renderer_contract MATCHES "statement\\.plan\\.form" OR
     NOT renderer_contract MATCHES "statement\\.plan\\.condition" OR
     NOT renderer_contract MATCHES "statement\\.plan\\.target_access" OR
     NOT renderer_contract MATCHES "plan\\.evaluation" OR
     NOT renderer_contract MATCHES "source_segments_->find")
    message(FATAL_ERROR "target renderer ignores the lowered expression/statement representation plan: ${renderer}")
  endif()
  if(renderer_contract MATCHES "mangler_->temporary|parameter_intents|parameter_optional|TranspileOptions|options_|program\\.runtime|program\\.function_graph|program\\.emission|emission_|has_executable_statements|MPF_VERSION|collect_(assignments|declarations)|StatementKind|ExpressionKind::|IntrinsicId::|BindingKind::|ValueType|AssignmentPattern|argument_transfer_|active_(reference|optional)_parameters|mark\\(expression\\.location|mark\\(\\{statement\\.line" OR
     NOT renderer_contract MATCHES "temporaries_->find" OR
     NOT renderer_contract MATCHES "function_abi" OR
     NOT renderer_contract MATCHES "program_scope" OR
     NOT renderer_contract MATCHES "function_scope")
    message(FATAL_ERROR "target renderer still plans layout, temporaries, declarations, or source-level ABI: ${renderer}")
  endif()
endforeach()

if(NOT EXISTS "${SOURCE_DIR}/src/backends/common/source_segments.hpp")
  message(FATAL_ERROR "target LIR source-segment planning utility is missing")
endif()
file(READ "${SOURCE_DIR}/src/backends/common/source_segments.hpp" source_segment_contract)
if(NOT source_segment_contract MATCHES "build_source_segment_plan" OR
   NOT source_segment_contract MATCHES "LirNodeId" OR
   NOT source_segment_contract MATCHES "SourceSegmentPlan")
  message(FATAL_ERROR "target source-map segments are not a dense LIR plan")
endif()

file(READ "${SOURCE_DIR}/src/backends/javascript/renderer.cpp" javascript_renderer_contract)
if(NOT javascript_renderer_contract MATCHES "program\\.module" OR
   NOT javascript_renderer_contract MATCHES "body_order" OR
   NOT javascript_renderer_contract MATCHES "control_prelude_order" OR
   NOT javascript_renderer_contract MATCHES "controlled_body_order" OR
   NOT javascript_renderer_contract MATCHES "emit_javascript_runtime_fragment")
  message(FATAL_ERROR "JavaScript renderer does not consume the module plan")
endif()

file(READ "${SOURCE_DIR}/src/backends/cpp/renderer.cpp" cpp_renderer_contract)
if(NOT cpp_renderer_contract MATCHES "program\\.translation_unit" OR
   NOT cpp_renderer_contract MATCHES "forward_declarations" OR
   NOT cpp_renderer_contract MATCHES "entry_statements" OR
   NOT cpp_renderer_contract MATCHES "emit_cpp_runtime")
  message(FATAL_ERROR "cpp renderer does not consume the translation-unit plan")
endif()
if(NOT javascript_renderer_contract MATCHES "__mpf_extent" OR
   NOT cpp_renderer_contract MATCHES "__mpf_extent")
  message(FATAL_ERROR "target renderers do not consume planned runtime index extents")
endif()

foreach(runtime IN ITEMS src/backends/javascript/runtime.cpp src/backends/cpp/runtime.cpp)
  if(NOT EXISTS "${SOURCE_DIR}/${runtime}")
    message(FATAL_ERROR "target runtime catalog is missing: ${runtime}")
  endif()
  mpf_assert_file_excludes("${runtime}" "TranspileOptions|SourceLanguage::|[./]ir/(hir|mir)\\.hpp"
    "target runtime catalog depends on source or target-independent compiler state")
endforeach()

file(READ "${SOURCE_DIR}/src/backends/cpp/lir.hpp" cpp_lir_contract)
if(NOT cpp_lir_contract MATCHES "DeclarationTypeKind" OR
   NOT cpp_lir_contract MATCHES "type_probe" OR
   NOT cpp_lir_contract MATCHES "fixed_shape" OR
   NOT cpp_lir_contract MATCHES "fixed_nested_types")
  message(FATAL_ERROR "cpp LIR does not own declaration type and initialization plans")
endif()

if(NOT cpp_lir_contract MATCHES "concrete_type" OR
   NOT cpp_lir_contract MATCHES "widen_children" OR
   NOT cpp_lir_contract MATCHES "ComparisonPlan")
  message(FATAL_ERROR "cpp LIR does not own expression type and comparison representation")
endif()

if(NOT cpp_lir_contract MATCHES "TranslationUnitPlan" OR
   NOT cpp_lir_contract MATCHES "standard_headers" OR
   NOT cpp_lir_contract MATCHES "forward_declarations" OR
   NOT cpp_lir_contract MATCHES "entry_statements")
  message(FATAL_ERROR "cpp LIR does not own translation-unit topology")
endif()

file(READ "${SOURCE_DIR}/src/backends/javascript/lir.hpp" javascript_lir_contract)
if(NOT javascript_lir_contract MATCHES "ModulePlan" OR
   NOT javascript_lir_contract MATCHES "directives" OR
   NOT javascript_lir_contract MATCHES "body_order" OR
   NOT javascript_lir_contract MATCHES "control_prelude_order" OR
   NOT javascript_lir_contract MATCHES "controlled_body_order" OR
   NOT javascript_lir_contract MATCHES "return_outputs" OR
   NOT javascript_lir_contract MATCHES "return_program")
  message(FATAL_ERROR "JavaScript LIR does not own module topology")
endif()
if(NOT cpp_lir_contract MATCHES "return_outputs" OR
   NOT cpp_lir_contract MATCHES "return_program")
  message(FATAL_ERROR "cpp LIR does not own function-output and program return forms")
endif()
if(NOT javascript_lir_contract MATCHES "ComparisonPlan" OR
   NOT javascript_lir_contract MATCHES "binary_comparison" OR
   NOT javascript_lir_contract MATCHES "not_membership" OR
   NOT javascript_lir_contract MATCHES "reference_box_uninitialized")
  message(FATAL_ERROR "JavaScript LIR does not own expression and call representation")
endif()

foreach(comparison_ir IN ITEMS
    src/compiler/expression_ast.hpp
    src/frontends/common/ast.hpp
    src/ir/hir.hpp
    src/ir/mir.hpp
    src/backends/javascript/lir.hpp
    src/backends/cpp/lir.hpp)
  file(READ "${SOURCE_DIR}/${comparison_ir}" comparison_contract)
  if(NOT comparison_contract MATCHES "ComparisonOperator" OR
     comparison_contract MATCHES "vector<std::string> operators")
    message(FATAL_ERROR
      "comparison operators are not strongly typed through the pipeline: ${comparison_ir}")
  endif()
endforeach()

foreach(binary_operator_ir IN ITEMS
    src/compiler/expression_ast.hpp
    src/frontends/common/ast.hpp
    src/ir/hir.hpp
    src/ir/mir.hpp
    src/backends/javascript/lir.hpp
    src/backends/cpp/lir.hpp)
  file(READ "${SOURCE_DIR}/${binary_operator_ir}" binary_operator_contract)
  if(NOT binary_operator_contract MATCHES "BinaryOperator")
    message(FATAL_ERROR
      "binary operator identity is not strongly typed through ${binary_operator_ir}")
  endif()
endforeach()
foreach(operator_consumer IN ITEMS
    src/semantic/expression_analyzer.cpp
    src/backends/javascript/lir_representation.cpp
    src/backends/cpp/lir_representation.cpp)
  mpf_assert_file_excludes("${operator_consumer}"
    "expression\\.value == \\\"(\\.\\*|\\./|\\.\\\\\\\\|\\.\\^|\\\\\\\\|\\^)\\\""
    "Matlab operator semantics regressed to source-spelling decisions")
endforeach()

mpf_assert_file_excludes("src/backends/common/identifier_mangler.hpp" "temporary\\("
  "renderer-facing identifier mangler still allocates temporaries")

foreach(required_file IN ITEMS
    include/mpf/source_map.hpp
    tests/fuzz/fuzz_smoke.cpp
    tests/fuzz/transpiler_fuzzer.cpp
    tests/performance/benchmark.cpp
    cmake/verify_performance.cmake)
  if(NOT EXISTS "${SOURCE_DIR}/${required_file}")
    message(FATAL_ERROR "commercial release gate file is missing: ${required_file}")
  endif()
endforeach()

file(READ "${SOURCE_DIR}/tests/CMakeLists.txt" test_build_contract)
if(NOT test_build_contract MATCHES
   "mpf\\.performance\\.release-gate[^)]*CONFIGURATIONS Release RelWithDebInfo[^)]*COMMAND" OR
   NOT test_build_contract MATCHES
   "mpf\\.performance\\.release-gate PROPERTIES[^)]*RUN_SERIAL TRUE")
  message(FATAL_ERROR
    "performance release gate must run serially and only in optimized configurations")
endif()

foreach(lir_header IN ITEMS
    src/backends/javascript/lir.hpp
    src/backends/cpp/lir.hpp)
  mpf_assert_file_excludes("${lir_header}" "code_binding\\.hpp|[./]ir/(hir|mir)\\.hpp"
    "target LIR header imports an analysis/validation layer")
endforeach()

if(EXISTS "${SOURCE_DIR}/src/lexers")
  message(FATAL_ERROR
    "language lexers must be owned by their frontend instead of a parallel src/lexers tree")
endif()
foreach(common_lexer_file IN ITEMS
    src/lexer/lexer.cpp
    src/lexer/lexer.hpp
    src/lexer/scanner.cpp
    src/lexer/scanner.hpp)
  mpf_assert_file_excludes("${common_lexer_file}"
    "lex_(python|matlab|fortran|typescript)_expression|SourceLanguage"
    "common lexer infrastructure dispatches concrete source languages")
endforeach()
if(EXISTS "${SOURCE_DIR}/include/mpf/version.hpp.in" OR
   NOT EXISTS "${SOURCE_DIR}/cmake/templates/version.hpp.in")
  message(FATAL_ERROR
    "generated-header templates must live under cmake/templates, outside the public include tree")
endif()
if(NOT EXISTS "${SOURCE_DIR}/include/mpf/mpf.hpp")
  message(FATAL_ERROR "public umbrella header include/mpf/mpf.hpp is missing")
endif()

file(GLOB flat_frontend_files
  "${SOURCE_DIR}/src/frontends/*.cpp"
  "${SOURCE_DIR}/src/frontends/*.hpp")
if(flat_frontend_files)
  message(FATAL_ERROR
    "frontend implementation files must live in common or language-owned directories: "
    "${flat_frontend_files}")
endif()
foreach(source_language IN ITEMS python matlab fortran typescript)
  foreach(required_component IN ITEMS
      expression_lexer.cpp
      expression_lexer.hpp
      frontend.cpp
      statement_lexer.cpp
      statement_lexer.hpp
      statement_parser.cpp
      statement_parser.hpp)
    if(NOT EXISTS
       "${SOURCE_DIR}/src/frontends/${source_language}/${required_component}")
      message(FATAL_ERROR
        "source frontend directory is missing "
        "${source_language}/${required_component}")
    endif()
  endforeach()
  file(READ
    "${SOURCE_DIR}/src/frontends/${source_language}/statement_parser.cpp"
    statement_parser_layout)
  if(NOT statement_parser_layout MATCHES
     "&lex_${source_language}_expression")
    message(FATAL_ERROR
      "source frontend does not own its expression lexer callback: ${source_language}")
  endif()
endforeach()

file(GLOB_RECURSE frontend_files
  "${SOURCE_DIR}/src/frontends/*.cpp"
  "${SOURCE_DIR}/src/frontends/*.hpp")
foreach(file IN LISTS frontend_files)
  file(READ "${file}" contents)
  if(contents MATCHES "[./](backends|ir/mir)" OR
     contents MATCHES "(javascript|cpp)_(lir|lowering|renderer|emitter|validator|bindings|backend)")
    message(FATAL_ERROR "frontend crosses the HIR extension boundary: ${file}")
  endif()
endforeach()

file(GLOB_RECURSE implementation_files
  "${SOURCE_DIR}/src/*.cpp"
  "${SOURCE_DIR}/src/*.hpp")
foreach(file IN LISTS implementation_files)
  file(READ "${file}" contents)
  if(contents MATCHES [=[#include[ \t]+"\.\./]=])
    message(FATAL_ERROR
      "internal cross-component includes must be source-root-qualified: ${file}")
  endif()
endforeach()

file(GLOB public_ir_files
  "${SOURCE_DIR}/src/ir/*.cpp"
  "${SOURCE_DIR}/src/ir/*.hpp"
  "${SOURCE_DIR}/src/semantic/*.cpp"
  "${SOURCE_DIR}/src/semantic/*.hpp")
foreach(file IN LISTS public_ir_files)
  file(READ "${file}" contents)
  if(contents MATCHES "[./]backends/" OR
     contents MATCHES "(javascript|cpp)_(lir|lowering|renderer|emitter|validator|bindings|backend)")
    message(FATAL_ERROR "public analysis/IR layer depends on a target backend: ${file}")
  endif()
endforeach()

file(GLOB javascript_backend_files
  "${SOURCE_DIR}/src/backends/javascript/*.cpp"
  "${SOURCE_DIR}/src/backends/javascript/*.hpp")
foreach(file IN LISTS javascript_backend_files)
  file(READ "${file}" contents)
  if(contents MATCHES "[./]cpp/" OR
     contents MATCHES "cpp_(lir|lowering|renderer|emitter|validator|bindings|backend)")
    message(FATAL_ERROR "JavaScript backend depends on cpp backend: ${file}")
  endif()
endforeach()

file(GLOB cpp_backend_files
  "${SOURCE_DIR}/src/backends/cpp/*.cpp"
  "${SOURCE_DIR}/src/backends/cpp/*.hpp")
foreach(file IN LISTS cpp_backend_files)
  file(READ "${file}" contents)
  if(contents MATCHES "[./]javascript/" OR
     contents MATCHES "javascript_(lir|lowering|renderer|emitter|validator|bindings|backend)")
    message(FATAL_ERROR "cpp backend depends on JavaScript backend: ${file}")
  endif()
endforeach()

file(GLOB legacy_prefixed_backend_files
  "${SOURCE_DIR}/src/backends/javascript_*.cpp"
  "${SOURCE_DIR}/src/backends/javascript_*.hpp"
  "${SOURCE_DIR}/src/backends/cpp_*.cpp"
  "${SOURCE_DIR}/src/backends/cpp_*.hpp")
if(legacy_prefixed_backend_files)
  message(FATAL_ERROR
    "target backend files must live in language directories without language filename prefixes: "
    "${legacy_prefixed_backend_files}")
endif()
file(GLOB flat_backend_files
  "${SOURCE_DIR}/src/backends/*.cpp"
  "${SOURCE_DIR}/src/backends/*.hpp")
if(flat_backend_files)
  message(FATAL_ERROR
    "backend implementation files must live in common or target-owned directories: "
    "${flat_backend_files}")
endif()
foreach(common_backend_component IN ITEMS
    artifact.cpp
    artifact.hpp
    conformance.cpp
    conformance.hpp
    descriptor.hpp
    identifier_mangler.cpp
    identifier_mangler.hpp
    lir_builder.hpp
    lir_dump.hpp
    pipeline.cpp
    pipeline.hpp
    registry.cpp
    registry.hpp
    source_segments.hpp)
  if(NOT EXISTS
     "${SOURCE_DIR}/src/backends/common/${common_backend_component}")
    message(FATAL_ERROR
      "common backend directory is missing ${common_backend_component}")
  endif()
endforeach()
foreach(target_directory IN ITEMS javascript cpp)
  foreach(required_component IN ITEMS
      backend bindings emitter lir lir_planning lir_representation lowering renderer runtime validator)
    if(NOT EXISTS "${SOURCE_DIR}/src/backends/${target_directory}/${required_component}.hpp")
      message(FATAL_ERROR
        "target backend directory is missing ${target_directory}/${required_component}.hpp")
    endif()
    if(NOT required_component STREQUAL "lir" AND
       NOT EXISTS "${SOURCE_DIR}/src/backends/${target_directory}/${required_component}.cpp")
      message(FATAL_ERROR
        "target backend directory is missing ${target_directory}/${required_component}.cpp")
    endif()
  endforeach()
  foreach(complex_matrix_component IN ITEMS
      complex_matrix_runtime.cpp
      complex_matrix_runtime.hpp
      complex_sparse_runtime.cpp
      complex_sparse_runtime.hpp)
    if(NOT EXISTS
       "${SOURCE_DIR}/src/backends/${target_directory}/${complex_matrix_component}")
      message(FATAL_ERROR
        "target backend directory is missing ${target_directory}/${complex_matrix_component}")
    endif()
  endforeach()
  foreach(sparse_matrix_component IN ITEMS sparse_matrix_runtime.cpp sparse_matrix_runtime.hpp)
    if(NOT EXISTS
       "${SOURCE_DIR}/src/backends/${target_directory}/${sparse_matrix_component}")
      message(FATAL_ERROR
        "target backend directory is missing ${target_directory}/${sparse_matrix_component}")
    endif()
  endforeach()
  foreach(optional_sparse_component IN ITEMS
      sparse_arithmetic_runtime.cpp
      sparse_arithmetic_runtime.hpp
      sparse_product_runtime.cpp
      sparse_product_runtime.hpp
      sparse_reduction_runtime.cpp
      sparse_reduction_runtime.hpp)
    if(NOT EXISTS
       "${SOURCE_DIR}/src/backends/${target_directory}/${optional_sparse_component}")
      message(FATAL_ERROR
        "target backend directory is missing ${target_directory}/${optional_sparse_component}")
    endif()
  endforeach()
endforeach()

file(READ "${SOURCE_DIR}/src/core/transpiler.cpp" driver)
foreach(required IN ITEMS
    "frontend->verify"
    "frontend->lower"
    "mir::lower_from_hir"
    "mir::run_default_optimization_pipeline"
    "mir::analyze_alias_effects"
    "mir::verify_alias_effects"
    "mir::analyze_memory_dependences"
    "mir::verify_memory_dependences"
    "backend->lower"
    "backend->verify"
    "backend->emit"
    "build_source_map"
    "artifact->dependencies")
  if(NOT driver MATCHES "${required}")
    message(FATAL_ERROR "compiler driver is missing production stage: ${required}")
  endif()
endforeach()
string(FIND "${driver}" "mir::run_default_optimization_pipeline" mir_optimization_index)
string(FIND "${driver}" "mir::analyze_alias_effects" mir_analysis_index)
string(FIND "${driver}" "mir::analyze_memory_dependences" memory_dependence_index)
string(FIND "${driver}" "backend->lower" backend_lowering_index)
if(mir_optimization_index LESS 0 OR mir_analysis_index LESS mir_optimization_index OR
   memory_dependence_index LESS mir_analysis_index OR
   backend_lowering_index LESS memory_dependence_index)
  message(FATAL_ERROR
    "shared MIR optimization and alias/effect analysis must precede memory dependence and target lowering")
endif()
