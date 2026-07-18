cmake_minimum_required(VERSION 3.20)
project(mpf-embedding-example LANGUAGES CXX)

if(NOT DEFINED MPF_REQUIRED_VERSION OR
  NOT MPF_REQUIRED_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
  message(FATAL_ERROR
    "Set MPF_REQUIRED_VERSION to the exact MPF MAJOR.MINOR.PATCH version")
endif()
find_package(mpf ${MPF_REQUIRED_VERSION} EXACT CONFIG REQUIRED
  COMPONENTS core javascript cpp)

add_executable(mpf-embedding-example main.cpp)
target_link_libraries(mpf-embedding-example PRIVATE mpf::mpf)
target_compile_features(mpf-embedding-example PRIVATE cxx_std_17)
