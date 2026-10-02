cmake_minimum_required(VERSION 3.20)

include("${CMAKE_CURRENT_LIST_DIR}/generated_toolchain.cmake")
set(CXX_COMPILER "/toolchain with spaces/cxx")
set(OSX_DEPLOYMENT_TARGET "14.0")
set(GENERATOR "Ninja Multi-Config")
set(GENERATOR_PLATFORM "arm64")
set(GENERATOR_TOOLSET "test-toolset")
set(arguments "-DGENERATED_SOURCE=fixture.cpp")
mpf_append_generated_toolchain(arguments)
set(expected
  "-DGENERATED_SOURCE=fixture.cpp"
  "-DCMAKE_CXX_COMPILER=/toolchain with spaces/cxx"
  "-DCMAKE_OSX_DEPLOYMENT_TARGET=14.0"
  -G "Ninja Multi-Config" -A arm64 -T test-toolset)
if(NOT arguments STREQUAL expected)
  message(FATAL_ERROR "generated toolchain did not preserve the parent configuration: ${arguments}")
endif()
if(NOT "$ENV{MACOSX_DEPLOYMENT_TARGET}" STREQUAL "14.0")
  message(FATAL_ERROR "deployment target was not available to compiler identification subprocesses")
endif()
foreach(variable IN ITEMS
    CXX_COMPILER OSX_DEPLOYMENT_TARGET GENERATOR GENERATOR_PLATFORM GENERATOR_TOOLSET)
  unset(${variable})
endforeach()
set(arguments "-DGENERATED_SOURCE=fixture.cpp")
set(ENV{MACOSX_DEPLOYMENT_TARGET} "12.0")
mpf_append_generated_toolchain(arguments)
if(NOT arguments STREQUAL "-DGENERATED_SOURCE=fixture.cpp")
  message(FATAL_ERROR "empty parent configuration added unexpected generated-toolchain options")
endif()
if(NOT "$ENV{MACOSX_DEPLOYMENT_TARGET}" STREQUAL "12.0")
  message(FATAL_ERROR "empty parent configuration changed the inherited deployment environment")
endif()
