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
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g3 -ggdb")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_LINKER_FLAGS_DEBUG} -fno-omit-frame-pointer -Wall")

file(GLOB_RECURSE ALL_SOURCE_FILES ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp ${CMAKE_CURRENT_SOURCE_DIR}/headers/*.hh)
add_custom_target(format COMMAND clang-format-18 -i ${ALL_SOURCE_FILES})

add_library(dartfrog INTERFACE)
target_include_directories(dartfrog INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/headers)

add_executable(dartfrog_tests)
target_link_libraries(dartfrog_tests dartfrog GTest::gtest_main)
target_sources(dartfrog_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/basic_tests.cpp)

enable_testing()
gtest_discover_tests(dartfrog_tests)

#####################################

