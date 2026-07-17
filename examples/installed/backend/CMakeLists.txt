cmake_minimum_required(VERSION 3.20)
project(mpf-installed-backend-example LANGUAGES CXX)

find_package(mpf 0.4.8 EXACT CONFIG REQUIRED COMPONENTS core javascript cpp)

add_executable(mpf-installed-backend-example main.cpp)
target_link_libraries(mpf-installed-backend-example PRIVATE mpf::mpf)
target_compile_features(mpf-installed-backend-example PRIVATE cxx_std_17)
