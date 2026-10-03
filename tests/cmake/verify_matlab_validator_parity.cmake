cmake_minimum_required(VERSION 3.20)

function(verify_matlab_validator_parity source_root reference_root build_root revision matlab_version
    differential_result observations)
  check_artifact("${reference_root}/validator-parity-provenance.json" "${reference_root}")
  file(READ "${reference_root}/validator-parity-provenance.json" provenance)
  foreach(field schemaVersion matlabRelease matlabVersion sourceRevision caseName source sourceSnapshot)
    read_json(${field} "${provenance}" ${field})
  endforeach()
  if(NOT schemaVersion STREQUAL "1" OR NOT matlabRelease STREQUAL "R2024b" OR
      NOT matlabVersion STREQUAL matlab_version OR NOT sourceRevision STREQUAL revision OR
      NOT caseName STREQUAL "matlab-validator-identities" OR
      NOT source STREQUAL "tests/fixtures/matlab_validator_exception_identity.m" OR
      NOT sourceSnapshot STREQUAL "source/matlab_validator_exception_identity.m")
    message(FATAL_ERROR "Native validator parity has invalid runtime/revision/source provenance")
  endif()
  check_artifact("${reference_root}/${sourceSnapshot}" "${reference_root}")
  file(SHA256 "${source_root}/${source}" original_hash)
  file(SHA256 "${reference_root}/${sourceSnapshot}" snapshot_hash)
  if(NOT original_hash STREQUAL snapshot_hash)
    message(FATAL_ERROR "Native validator source snapshot differs from the translated input")
  endif()
  check_artifact("${reference_root}/${caseName}.stdout" "${reference_root}")
  file(READ "${reference_root}/${caseName}.stdout" native_text)
  normalize_tokens(native_output "${native_text}")
  string(JSON count LENGTH "${observations}" cases)
  math(EXPR last "${count} - 1")
  foreach(index RANGE 0 ${last})
    read_json(name "${observations}" cases ${index} name)
    read_json(identifier "${observations}" cases ${index} exceptionIdentifier)
    set(expected_${name} "${identifier}")
  endforeach()
  set(keys
    numeric numeric_or_logical floating real finite
    non_nan positive nonpositive nonnegative negative
    nonzero integer nonempty scalar_or_empty vector
    row column matrix nonmissing nonzero_length_text
    text text_scalar valid_variable_name greater_than greater_than_or_equal
    less_than less_than_or_equal in_range positive-complex positive-text
    integer-complex integer-text valid-variable-name-type greater-than-complex greater-than-text
    nonzero-length-text-empty-double range-exclusive range-exclude-lower range-exclude-upper
  )
  set(expected_tokens)
  foreach(key IN LISTS keys)
    foreach(context input output)
      if(NOT DEFINED expected_${key}-${context} OR expected_${key}-${context} STREQUAL "")
        message(FATAL_ERROR "Native validator parity has no recorded failing observation: ${key}")
      endif()
      list(APPEND expected_tokens "${expected_${key}-${context}}")
    endforeach()
  endforeach()
  list(APPEND expected_tokens accepted-text accepted-text accepted-empty accepted-empty)
  string(JOIN " " expected_output ${expected_tokens})
  if(NOT native_output STREQUAL expected_output)
    message(FATAL_ERROR "Native validator fixture output differs from recorded R2024b identities")
  endif()
  check_artifact("${differential_result}" "${build_root}")
  file(STRINGS "${differential_result}" differential_lines ENCODING UTF-8)
  read_result(generated_case case)
  read_result(generated_input input)
  read_result(node node)
  read_result(compiler cxx-compiler)
  read_result(javascript javascript)
  read_result(cpp cpp)
  if(NOT generated_case STREQUAL caseName OR NOT generated_input STREQUAL "${source_root}/${source}")
    message(FATAL_ERROR "Generated targets did not execute the native validator source")
  endif()
  normalize_tokens(javascript "${javascript}")
  normalize_tokens(cpp "${cpp}")
  if(NOT javascript STREQUAL native_output OR NOT cpp STREQUAL native_output)
    message(FATAL_ERROR "R2024b/JavaScript/cpp validator identity execution mismatch")
  endif()
  list(LENGTH expected_tokens token_count)
  message(STATUS "Verified ${token_count} R2024b/JavaScript/cpp validator tokens on ${revision}")
endfunction()
