cmake_minimum_required(VERSION 3.20)

foreach(required SOURCE_DIR CORPUS_DIR PREPARER)
  if(NOT DEFINED ${required})
    message(FATAL_ERROR "prepare_corpus.cmake requires ${required}")
  endif()
endforeach()

execute_process(COMMAND "${PREPARER}" "${SOURCE_DIR}" "${CORPUS_DIR}"
  RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Fuzz seed preparation failed: ${output}${error}")
endif()
message(STATUS "${output}")
