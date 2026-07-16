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
  file(READ "${SOURCE_DIR}/${lowering}" lowering_contents)
  if(NOT lowering_contents MATCHES "materialize_chunks\\(render_" OR
     NOT lowering_contents MATCHES "SemanticProgram")
    message(FATAL_ERROR "target lowering does not materialize an independent serialized LIR: ${lowering}")
  endif()
endforeach()

file(READ "${SOURCE_DIR}/src/ir/mir.hpp" mir_contract)
foreach(required IN ITEMS
    "successor_arguments"
    "BlockArgument"
    "AliasEffectTable"
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
