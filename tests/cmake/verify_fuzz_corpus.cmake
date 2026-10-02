cmake_minimum_required(VERSION 3.20)

foreach(required SOURCE_DIR TEST_BINARY_DIR)
  if(NOT DEFINED ${required})
    message(FATAL_ERROR "verify_fuzz_corpus.cmake requires ${required}")
  endif()
endforeach()

set(corpus "${TEST_BINARY_DIR}/corpus")
execute_process(COMMAND "${CMAKE_COMMAND}"
  "-DSOURCE_DIR=${SOURCE_DIR}" "-DCORPUS_DIR=${corpus}"
  -P "${SOURCE_DIR}/tests/fuzz/prepare_corpus.cmake"
  RESULT_VARIABLE prepared OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT prepared EQUAL 0)
  message(FATAL_ERROR "Fuzz seed preparation failed: ${output}${error}")
endif()

set(language_code 4)
set(checked 0)
foreach(language python matlab fortran typescript)
  set(seed_root "${SOURCE_DIR}/tests/fuzz/corpus/${language}")
  file(GLOB_RECURSE seeds LIST_DIRECTORIES false RELATIVE "${seed_root}" "${seed_root}/*")
  if(NOT seeds)
    message(FATAL_ERROR "No ${language} seeds are covered")
  endif()
  foreach(seed IN LISTS seeds)
    file(READ "${seed_root}/${seed}" payload HEX)
    set(target_code 2)
    foreach(target javascript cpp)
      file(READ "${corpus}/${language}/${target}/${seed}" framed HEX)
      set(expected "0${language_code}0${target_code}${payload}")
      if(NOT framed STREQUAL expected)
        message(FATAL_ERROR "Fuzz seed framing changed source bytes: ${language}/${target}/${seed}")
      endif()
      math(EXPR target_code "${target_code} + 1")
      math(EXPR checked "${checked} + 1")
    endforeach()
  endforeach()
  math(EXPR language_code "${language_code} + 1")
endforeach()

execute_process(COMMAND "${CMAKE_COMMAND}"
  "-DSOURCE_DIR=${SOURCE_DIR}" "-DCORPUS_DIR=${SOURCE_DIR}/tests/fuzz/corpus"
  -P "${SOURCE_DIR}/tests/fuzz/prepare_corpus.cmake"
  RESULT_VARIABLE unsafe OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(unsafe EQUAL 0 OR NOT "${output}${error}" MATCHES "beneath the project's root build/")
  message(FATAL_ERROR "Fuzz preparation did not reject writing into the checked-in corpus")
endif()
message(STATUS "Verified ${checked} framed seeds and protected source corpus")
