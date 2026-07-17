cmake_minimum_required(VERSION 3.20)
project(mpf-embedding-example LANGUAGES CXX)

find_package(mpf 0.4.6 EXACT CONFIG REQUIRED COMPONENTS core javascript cpp)

add_executable(mpf-embedding-example main.cpp)
target_link_libraries(mpf-embedding-example PRIVATE mpf::mpf)
target_compile_features(mpf-embedding-example PRIVATE cxx_std_17)
