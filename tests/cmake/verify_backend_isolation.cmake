cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED SOURCE_DIR OR NOT DEFINED BUILD_DIR OR
   NOT DEFINED ENABLE_JAVASCRIPT OR NOT DEFINED ENABLE_CPP OR
   NOT DEFINED PROJECT_VERSION OR NOT DEFINED CONFIG)
  message(FATAL_ERROR "backend isolation test is missing required arguments")
endif()

file(REMOVE_RECURSE "${BUILD_DIR}")
execute_process(
  COMMAND "${CMAKE_COMMAND}" -S "${SOURCE_DIR}" -B "${BUILD_DIR}"
    -DCMAKE_BUILD_TYPE=${CONFIG}
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
    -DMPF_BUILD_TESTS=OFF
    -DMPF_BUILD_CLI=ON
    -DMPF_ENABLE_WERROR=ON
    -DMPF_ENABLE_JAVASCRIPT_BACKEND=${ENABLE_JAVASCRIPT}
    -DMPF_ENABLE_CPP_BACKEND=${ENABLE_CPP}
  RESULT_VARIABLE configure_result
  OUTPUT_VARIABLE configure_output
  ERROR_VARIABLE configure_error)
if(NOT configure_result EQUAL 0)
  message(FATAL_ERROR "backend-isolated configure failed:\n${configure_output}\n${configure_error}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" --build "${BUILD_DIR}" --config "${CONFIG}" --parallel
  RESULT_VARIABLE build_result
  OUTPUT_VARIABLE build_output
  ERROR_VARIABLE build_error)
if(NOT build_result EQUAL 0)
  message(FATAL_ERROR "backend-isolated build failed:\n${build_output}\n${build_error}")
endif()

set(compile_commands "${BUILD_DIR}/compile_commands.json")
if(EXISTS "${compile_commands}")
  file(READ "${compile_commands}" compilation_database)
  if(NOT ENABLE_JAVASCRIPT AND
     compilation_database MATCHES "backends[/\\\\]javascript[/\\\\](emitter|renderer|backend|bindings|lowering|validator)\\.cpp")
    message(FATAL_ERROR "disabled JavaScript backend was compiled")
  endif()
  if(NOT ENABLE_CPP AND
     compilation_database MATCHES "backends[/\\\\]cpp[/\\\\](emitter|renderer|backend|bindings|lowering|validator)\\.cpp")
    message(FATAL_ERROR "disabled C++17 backend was compiled")
  endif()
  if(NOT ENABLE_JAVASCRIPT AND NOT ENABLE_CPP AND
     compilation_database MATCHES "identifier_mangler\\.cpp")
    message(FATAL_ERROR "core-only build compiled backend-common sources")
  endif()
endif()

if(WIN32)
  set(mpfc "${BUILD_DIR}/${CONFIG}/mpfc.exe")
else()
  set(mpfc "${BUILD_DIR}/mpfc")
endif()
file(WRITE "${BUILD_DIR}/input.py" "print(42)\n")

foreach(target IN ITEMS javascript cpp)
  if(target STREQUAL "javascript")
    set(enabled "${ENABLE_JAVASCRIPT}")
  else()
    set(enabled "${ENABLE_CPP}")
  endif()
  execute_process(
    COMMAND "${mpfc}" --language python --target "${target}" --no-banner "${BUILD_DIR}/input.py"
    RESULT_VARIABLE transpile_result
    OUTPUT_VARIABLE transpile_output
    ERROR_VARIABLE transpile_error)
  if(enabled AND NOT transpile_result EQUAL 0)
    message(FATAL_ERROR "enabled ${target} backend failed:\n${transpile_error}")
  endif()
  if(NOT enabled AND transpile_result EQUAL 0)
    message(FATAL_ERROR "disabled ${target} backend unexpectedly succeeded")
  endif()
  if(NOT enabled AND NOT transpile_error MATCHES "MPF0003")
    message(FATAL_ERROR "disabled ${target} backend did not report MPF0003:\n${transpile_error}")
  endif()
endforeach()

set(stage "${BUILD_DIR}/stage")
execute_process(
  COMMAND "${CMAKE_COMMAND}" --install "${BUILD_DIR}" --config "${CONFIG}" --prefix "${stage}"
  RESULT_VARIABLE install_result
  OUTPUT_VARIABLE install_output
  ERROR_VARIABLE install_error)
if(NOT install_result EQUAL 0)
  message(FATAL_ERROR "backend-isolated install failed:\n${install_output}\n${install_error}")
endif()

set(required_components core)
if(ENABLE_JAVASCRIPT)
  list(APPEND required_components javascript)
endif()
if(ENABLE_CPP)
  list(APPEND required_components cpp)
endif()
list(JOIN required_components ";" required_components_argument)
set(consumer_build "${BUILD_DIR}/consumer")
execute_process(
  COMMAND "${CMAKE_COMMAND}"
    -S "${SOURCE_DIR}/tests/consumers/backend_configuration"
    -B "${consumer_build}"
    -DCMAKE_BUILD_TYPE=${CONFIG}
    -DCMAKE_PREFIX_PATH=${stage}
    "-DMPF_REQUIRED_COMPONENTS=${required_components_argument}"
    -DMPF_EXPECT_VERSION=${PROJECT_VERSION}
    -DMPF_EXPECT_JAVASCRIPT=${ENABLE_JAVASCRIPT}
    -DMPF_EXPECT_CPP=${ENABLE_CPP}
  RESULT_VARIABLE consumer_configure_result
  OUTPUT_VARIABLE consumer_configure_output
  ERROR_VARIABLE consumer_configure_error)
if(NOT consumer_configure_result EQUAL 0)
  message(FATAL_ERROR
    "isolated installed-package consumer configure failed:\n"
    "${consumer_configure_output}\n${consumer_configure_error}")
endif()
execute_process(
  COMMAND "${CMAKE_COMMAND}" --build "${consumer_build}" --config "${CONFIG}" --parallel
  RESULT_VARIABLE consumer_build_result
  OUTPUT_VARIABLE consumer_build_output
  ERROR_VARIABLE consumer_build_error)
if(NOT consumer_build_result EQUAL 0)
  message(FATAL_ERROR
    "isolated installed-package consumer build failed:\n"
    "${consumer_build_output}\n${consumer_build_error}")
endif()
if(WIN32)
  set(consumer "${consumer_build}/${CONFIG}/mpf-backend-configuration-consumer.exe")
else()
  set(consumer "${consumer_build}/mpf-backend-configuration-consumer")
endif()
execute_process(COMMAND "${consumer}" RESULT_VARIABLE consumer_result)
if(NOT consumer_result EQUAL 0)
  message(FATAL_ERROR "isolated installed-package consumer failed (${consumer_result})")
endif()

set(missing_component "")
if(NOT ENABLE_JAVASCRIPT)
  set(missing_component javascript)
elseif(NOT ENABLE_CPP)
  set(missing_component cpp)
endif()
if(NOT missing_component STREQUAL "")
  execute_process(
    COMMAND "${CMAKE_COMMAND}"
      -S "${SOURCE_DIR}/tests/consumers/backend_configuration"
      -B "${BUILD_DIR}/missing-component-consumer"
      -DCMAKE_BUILD_TYPE=${CONFIG}
      -DCMAKE_PREFIX_PATH=${stage}
      "-DMPF_REQUIRED_COMPONENTS=core;${missing_component}"
      -DMPF_EXPECT_VERSION=${PROJECT_VERSION}
      -DMPF_EXPECT_JAVASCRIPT=${ENABLE_JAVASCRIPT}
      -DMPF_EXPECT_CPP=${ENABLE_CPP}
    RESULT_VARIABLE missing_component_result
    OUTPUT_VARIABLE missing_component_output
    ERROR_VARIABLE missing_component_error)
  if(missing_component_result EQUAL 0)
    message(FATAL_ERROR "unavailable package component '${missing_component}' was accepted")
  endif()
endif()
