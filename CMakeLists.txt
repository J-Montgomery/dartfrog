cmake_minimum_required(VERSION 3.20)

set(CMAKE_CXX_STANDARD 20)
project(
    dartfrog
    VERSION 0.0.1
    LANGUAGES CXX
)

include(ProcessorCount)
find_package(GTest REQUIRED)

set(CMAKE_EXPORT_COMPILE_COMMANDS True)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build Type" FORCE)
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g1")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_LINKER_FLAGS_DEBUG} -fno-omit-frame-pointer -Wall")
endif()

# ProcessorCount(n)
# if(NOT n EQUAL 0)
#     set(SOUFFLE_PARALLEL_N -j${n})
# else()
#     set(SOUFFLE_PARALLEL_N -j1)
# endif()

set(SOUFFLE_PARALLEL_N -j1)

find_program(SOUFFLE_EXECUTABLE NAMES souffle)
function(add_souffle_benchmark TARGET_NAME DL_FILE DATA_DIR BENCHMARK_TARGET)
    set(GEN_EXEC "${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME}_exec")
    # Extract benchmark name from target name (e.g., souffle_galen -> galen)
    string(REGEX REPLACE "^souffle_" "" BENCH_NAME "${TARGET_NAME}")

    add_custom_command(
        OUTPUT "${GEN_EXEC}"
        COMMAND "${SOUFFLE_EXECUTABLE}" -o "${GEN_EXEC}" "${DL_FILE}" "${SOUFFLE_PARALLEL_N}"
        DEPENDS "${DL_FILE}"
        VERBATIM
    )

    add_custom_target(${TARGET_NAME}_target DEPENDS "${GEN_EXEC}" unzip_${BENCH_NAME})
    target_compile_definitions(${BENCHMARK_TARGET} PRIVATE
        SOUFFLE_EXEC_PATH="${GEN_EXEC}"
    )
    add_dependencies(${BENCHMARK_TARGET} ${TARGET_NAME}_target unzip_${BENCH_NAME})
endfunction()

file(GLOB_RECURSE ALL_SOURCE_FILES ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp ${CMAKE_CURRENT_SOURCE_DIR}/headers/*.hpp ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/*.hpp)
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
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/all_partitions_tests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/dsl_basic_tests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/dsl_planner_tests.cpp
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
set(BENCHMARK_DATA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/input")
set(BENCHMARK_SCRIPT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/benchmarks")

#####################################

set(UNZIPPED_INPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/input")

function(unzip_benchmark_input ZIP_NAME OUTPUT_SUBDIR)
    add_custom_target(unzip_${ZIP_NAME} ALL
        COMMAND ${CMAKE_COMMAND} -E make_directory "${UNZIPPED_INPUT_DIR}" && unzip -o "${CMAKE_CURRENT_SOURCE_DIR}/input/${ZIP_NAME}.zip" -d "${UNZIPPED_INPUT_DIR}"
        COMMENT "Extracting ${ZIP_NAME} benchmark inputs"
    )
endfunction()

unzip_benchmark_input(doop doop)
unzip_benchmark_input(galen galen)
unzip_benchmark_input(crdt crdt)

#####################################

add_executable(tc_benchmark benchmarks/tc_benchmark.cpp)
target_link_libraries(tc_benchmark PRIVATE dartfrog benchmark::benchmark)

#####################################

add_executable(galen_benchmark benchmarks/galen_benchmark.cpp)
target_link_libraries(galen_benchmark PRIVATE dartfrog benchmark::benchmark)

set(BENCHMARK_DATA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/input")
set(BENCHMARK_SCRIPT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/benchmarks")

target_compile_definitions(galen_benchmark PRIVATE
    BIN_PATH="${CMAKE_CURRENT_BINARY_DIR}/souffle_galen_exec"
    DATA_DIR="${UNZIPPED_INPUT_DIR}/galen"
    SOUFFLE_ARGS="${SOUFFLE_PARALLEL_N}"
    OUT_DIR="${CMAKE_CURRENT_BINARY_DIR}"
)

add_souffle_benchmark(
    souffle_galen
    "${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/galen.dl"
    "${UNZIPPED_INPUT_DIR}/galen"
    galen_benchmark
)

#####################################

add_executable(crdt_benchmark benchmarks/crdt_benchmark.cpp)
target_link_libraries(crdt_benchmark PRIVATE dartfrog benchmark::benchmark)

target_compile_definitions(crdt_benchmark PRIVATE
    BIN_PATH="${CMAKE_CURRENT_BINARY_DIR}/souffle_crdt_exec"
    DATA_DIR="${UNZIPPED_INPUT_DIR}/crdt"
    SOUFFLE_ARGS="${SOUFFLE_PARALLEL_N}"
    OUT_DIR="${CMAKE_CURRENT_BINARY_DIR}"
)

add_souffle_benchmark(
    souffle_crdt
    "${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/crdt.dl"
    "${UNZIPPED_INPUT_DIR}/crdt"
    crdt_benchmark
)

#####################################

add_executable(doop_benchmark benchmarks/doop_benchmark.cpp)
target_link_libraries(doop_benchmark PRIVATE dartfrog benchmark::benchmark)

target_compile_definitions(doop_benchmark PRIVATE
    BIN_PATH="${CMAKE_CURRENT_BINARY_DIR}/souffle_doop_exec"
    DATA_DIR="${UNZIPPED_INPUT_DIR}/doop"
    SOUFFLE_ARGS="${SOUFFLE_PARALLEL_N}"
    OUT_DIR="${CMAKE_CURRENT_BINARY_DIR}"
)

add_souffle_benchmark(
    souffle_doop
    "${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/doop.dl"
    "${UNZIPPED_INPUT_DIR}/doop"
    doop_benchmark
)

