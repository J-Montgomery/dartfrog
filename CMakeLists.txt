cmake_minimum_required(VERSION 3.20)

set(CMAKE_CXX_STANDARD 20)
project(
    dartfrog
    VERSION 0.0.1
    LANGUAGES CXX
)

find_package(GTest REQUIRED)

set(CMAKE_BUILD_TYPE "Debug")
set(CMAKE_EXPORT_COMPILE_COMMANDS True)
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g1")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_LINKER_FLAGS_DEBUG} -fno-omit-frame-pointer -Wall")

file(GLOB_RECURSE ALL_SOURCE_FILES ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp ${CMAKE_CURRENT_SOURCE_DIR}/headers/*.hpp)
add_custom_target(format COMMAND clang-format-18 -i ${ALL_SOURCE_FILES})

add_library(dartfrog INTERFACE)
target_include_directories(dartfrog INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/headers)

add_executable(dartfrog_tests)
target_link_libraries(dartfrog_tests dartfrog GTest::gtest_main)
target_sources(dartfrog_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/extends_tests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/filter_tests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/join_tests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/iteration_tests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/leapjoin_tests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/merge_unique_tests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/relation_tests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/seek_tests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/usage_tests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/variable_tests.cpp
)

add_executable(dsl_tests)
target_link_libraries(dsl_tests dartfrog GTest::gtest_main)
target_sources(dsl_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/dsl_basic_tests.cpp
)

option(ENABLE_ASAN "Enable ASAN" OFF)
if(ENABLE_ASAN)
target_compile_options(dartfrog_tests PRIVATE -fsanitize=address)
target_link_options(dartfrog_tests PRIVATE -fsanitize=address)

target_compile_options(dsl_tests PRIVATE -fsanitize=address)
target_link_options(dsl_tests PRIVATE -fsanitize=address)
endif()

#####################################

enable_testing()
gtest_discover_tests(dartfrog_tests)
gtest_discover_tests(dsl_tests)

#####################################

find_package(benchmark REQUIRED)

add_executable(tc_benchmark tests/tc_benchmark.cpp)
target_link_libraries(tc_benchmark dartfrog benchmark::benchmark)
target_compile_options(tc_benchmark PRIVATE -O3)
