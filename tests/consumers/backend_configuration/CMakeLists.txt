cmake_minimum_required(VERSION 3.20)
project(mpf-backend-configuration-consumer LANGUAGES CXX)

if(NOT DEFINED MPF_EXPECT_VERSION)
  message(FATAL_ERROR "MPF_EXPECT_VERSION is required")
endif()
if(NOT DEFINED MPF_REQUIRED_COMPONENTS)
  set(MPF_REQUIRED_COMPONENTS core)
endif()

find_package(mpf ${MPF_EXPECT_VERSION} EXACT CONFIG REQUIRED
  COMPONENTS ${MPF_REQUIRED_COMPONENTS})

add_executable(mpf-backend-configuration-consumer main.cpp)
target_link_libraries(mpf-backend-configuration-consumer PRIVATE mpf::mpf)
target_compile_features(mpf-backend-configuration-consumer PRIVATE cxx_std_17)
target_compile_definitions(mpf-backend-configuration-consumer PRIVATE
  MPF_EXPECT_JAVASCRIPT=$<BOOL:${MPF_EXPECT_JAVASCRIPT}>
  MPF_EXPECT_CPP=$<BOOL:${MPF_EXPECT_CPP}>
  MPF_EXPECT_VERSION=\"${mpf_VERSION}\")
