cmake_minimum_required(VERSION 3.20)
project(mpf-installed-backend-example LANGUAGES CXX)

if(NOT DEFINED MPF_REQUIRED_VERSION OR
   NOT MPF_REQUIRED_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
  message(FATAL_ERROR "MPF_REQUIRED_VERSION must name the exact MPF version")
endif()
find_package(mpf ${MPF_REQUIRED_VERSION} EXACT CONFIG REQUIRED
  COMPONENTS core javascript cpp)

add_executable(mpf-installed-backend-example main.cpp)
target_link_libraries(mpf-installed-backend-example PRIVATE mpf::mpf)
target_compile_features(mpf-installed-backend-example PRIVATE cxx_std_17)
