cmake_minimum_required(VERSION 3.20)
project(mpf-installed-frontend-example LANGUAGES CXX)

find_package(mpf 0.5.1 EXACT CONFIG REQUIRED COMPONENTS core javascript)

add_executable(mpf-installed-frontend-example main.cpp)
target_link_libraries(mpf-installed-frontend-example PRIVATE mpf::mpf)
target_compile_features(mpf-installed-frontend-example PRIVATE cxx_std_17)
