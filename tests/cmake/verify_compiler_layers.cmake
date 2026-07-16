if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "compiler layer verification requires SOURCE_DIR")
endif()

function(mpf_assert_file_excludes file pattern message_text)
  file(READ "${SOURCE_DIR}/${file}" contents)
  if(contents MATCHES "${pattern}")
    message(FATAL_ERROR "${message_text}: ${file}")
  endif()
endfunction()

foreach(emitter IN ITEMS
    src/backends/javascript_emitter.cpp
    src/backends/cpp_emitter.cpp)
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
    src/frontends/python_frontend.cpp
    src/frontends/matlab_frontend.cpp
    src/frontends/fortran_frontend.cpp)
  mpf_assert_file_excludes("${frontend}" "lower_from_syntax|\\.syntax|inventory_ast"
    "production frontend still wraps or lowers the shared parser syntax tree")
endforeach()
mpf_assert_file_excludes("src/frontends/frontend_ast.hpp" "Program syntax|AstInventory"
  "language AST artifact still embeds the transitional shared syntax program")

foreach(lowering IN ITEMS
    src/backends/javascript_lowering.cpp
    src/backends/cpp_lowering.cpp)
  mpf_assert_file_excludes("${lowering}" "expression\\.argument_(intents|optional_forward)"
    "target lowering reinterprets source call intent instead of MIR transfer facts")
  file(READ "${SOURCE_DIR}/${lowering}" lowering_contents)
  if(NOT lowering_contents MATCHES "materialize_chunks\\(render_" OR
     NOT lowering_contents MATCHES "SemanticProgram" OR
     NOT lowering_contents MATCHES "plan_lir_resources")
    message(FATAL_ERROR "target lowering does not materialize an independent serialized LIR: ${lowering}")
  endif()
endforeach()

foreach(planning IN ITEMS
    src/backends/javascript_lir_planning.cpp
    src/backends/cpp_lir_planning.cpp)
  if(NOT EXISTS "${SOURCE_DIR}/${planning}")
    message(FATAL_ERROR "target LIR planning layer is missing: ${planning}")
  endif()
  file(READ "${SOURCE_DIR}/${planning}" planning_contents)
  if(NOT planning_contents MATCHES "plan_lir_resources" OR
     NOT planning_contents MATCHES "verify_lir_resources" OR
     NOT planning_contents MATCHES "program_scope" OR
     NOT planning_contents MATCHES "function_scope")
    message(FATAL_ERROR "target LIR planning layer does not own scope resources: ${planning}")
  endif()
endforeach()

file(READ "${SOURCE_DIR}/src/ir/mir.hpp" mir_contract)
foreach(required IN ITEMS
    "successor_arguments"
    "BlockArgument"
    "AliasEffectTable"
    "ArgumentTransfer"
    "StorageLifetime"
    "StorageViewKind")
  if(NOT mir_contract MATCHES "${required}")
    message(FATAL_ERROR "MIR contract is missing commercial CFG/alias field: ${required}")
  endif()
endforeach()
if(mir_contract MATCHES "struct LoweringResult[^{]*\\{[^}]*AliasEffectTable" OR
   mir_contract MATCHES "struct Instruction[^{]*\\{[^}]*Effect[ \t]+effects")
  message(FATAL_ERROR "alias/effect analysis is coupled back into MIR lowering or instructions")
endif()
if(mir_contract MATCHES "argument_(types|storages|omitted)")
  message(FATAL_ERROR "MIR call sites regressed to parallel argument arrays")
endif()

foreach(target_lir IN ITEMS src/backends/javascript_lir.hpp src/backends/cpp_lir.hpp)
  file(READ "${SOURCE_DIR}/${target_lir}" target_lir_contract)
  if(NOT target_lir_contract MATCHES "argument_transfers" OR
     NOT target_lir_contract MATCHES "FunctionAbi" OR
     NOT target_lir_contract MATCHES "TemporaryPlan" OR
     NOT target_lir_contract MATCHES "ScopePlan" OR
     NOT target_lir_contract MATCHES "program_scope" OR
     NOT target_lir_contract MATCHES "function_scope" OR
     NOT target_lir_contract MATCHES "offsets" OR
     target_lir_contract MATCHES "argument_intents")
    message(FATAL_ERROR "target LIR does not own a lowered argument transfer plan: ${target_lir}")
  endif()
endforeach()

foreach(renderer IN ITEMS src/backends/javascript_renderer.cpp src/backends/cpp_renderer.cpp)
  file(READ "${SOURCE_DIR}/${renderer}" renderer_contract)
  if(NOT renderer_contract MATCHES "argument_transfer_(writes|copies|forwards_optional)")
    message(FATAL_ERROR "target renderer ignores the lowered argument transfer plan: ${renderer}")
  endif()
  if(renderer_contract MATCHES "mangler_->temporary|parameter_intents|parameter_optional|options_\\.module_kind|collect_(assignments|declarations)" OR
     NOT renderer_contract MATCHES "temporaries_->find" OR
     NOT renderer_contract MATCHES "function_abi" OR
     NOT renderer_contract MATCHES "program_scope" OR
     NOT renderer_contract MATCHES "function_scope")
    message(FATAL_ERROR "target renderer still plans temporaries, declarations, or source-level ABI: ${renderer}")
  endif()
endforeach()

file(READ "${SOURCE_DIR}/src/backends/cpp_lir.hpp" cpp_lir_contract)
if(NOT cpp_lir_contract MATCHES "DeclarationTypeKind" OR
   NOT cpp_lir_contract MATCHES "type_probe" OR
   NOT cpp_lir_contract MATCHES "fixed_shape" OR
   NOT cpp_lir_contract MATCHES "fixed_nested_types")
  message(FATAL_ERROR "cpp LIR does not own declaration type and initialization plans")
endif()

mpf_assert_file_excludes("src/backends/identifier_mangler.hpp" "temporary\\("
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

foreach(lir_header IN ITEMS
    src/backends/javascript_lir.hpp
    src/backends/cpp_lir.hpp)
  mpf_assert_file_excludes("${lir_header}" "code_binding\\.hpp|[./]ir/(hir|mir)\\.hpp"
    "target LIR header imports an analysis/validation layer")
endforeach()

file(GLOB frontend_files
  "${SOURCE_DIR}/src/frontends/*.cpp"
  "${SOURCE_DIR}/src/frontends/*.hpp")
foreach(file IN LISTS frontend_files)
  file(READ "${file}" contents)
  if(contents MATCHES "[./](backends|ir/mir)" OR
     contents MATCHES "(javascript|cpp)_(lir|lowering|renderer|emitter|validator|bindings|backend)")
    message(FATAL_ERROR "frontend crosses the HIR extension boundary: ${file}")
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
  "${SOURCE_DIR}/src/backends/javascript_*.cpp"
  "${SOURCE_DIR}/src/backends/javascript_*.hpp")
foreach(file IN LISTS javascript_backend_files)
  file(READ "${file}" contents)
  if(contents MATCHES "cpp_(lir|lowering|renderer|emitter|validator|bindings|backend)")
    message(FATAL_ERROR "JavaScript backend depends on cpp backend: ${file}")
  endif()
endforeach()

file(GLOB cpp_backend_files
  "${SOURCE_DIR}/src/backends/cpp_*.cpp"
  "${SOURCE_DIR}/src/backends/cpp_*.hpp")
foreach(file IN LISTS cpp_backend_files)
  file(READ "${file}" contents)
  if(contents MATCHES "javascript_(lir|lowering|renderer|emitter|validator|bindings|backend)")
    message(FATAL_ERROR "cpp backend depends on JavaScript backend: ${file}")
  endif()
endforeach()

file(READ "${SOURCE_DIR}/src/core/transpiler.cpp" driver)
foreach(required IN ITEMS
    "frontend->verify"
    "frontend->lower"
    "mir::lower_from_hir"
    "mir::analyze_alias_effects"
    "mir::verify_alias_effects"
    "backend->lower"
    "backend->verify"
    "backend->emit"
    "build_source_map"
    "artifact->dependencies")
  if(NOT driver MATCHES "${required}")
    message(FATAL_ERROR "compiler driver is missing production stage: ${required}")
  endif()
endforeach()
