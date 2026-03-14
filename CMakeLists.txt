cmake_minimum_required(VERSION 3.20)

project(agner LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT DEFINED AGNER_ENABLE_COVERAGE)
  if(CMAKE_CONFIGURATION_TYPES)
    set(AGNER_ENABLE_COVERAGE ON)
  elseif(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(AGNER_ENABLE_COVERAGE ON)
  else()
    set(AGNER_ENABLE_COVERAGE OFF)
  endif()
endif()

add_library(agner INTERFACE)

target_include_directories(agner INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)

target_compile_features(agner INTERFACE cxx_std_20)

include(CTest)

include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(googletest)

set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  benchmark
  URL https://github.com/google/benchmark/archive/refs/tags/v1.8.3.zip
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(benchmark)

add_executable(agner_tests
  test/actor_test.cpp
  test/actor_control_test.cpp
  test/genserver_test.cpp
  test/scheduler_test.cpp
  test/supervisor_test.cpp
  test/task_test.cpp
)

add_executable(agner_benchmarks
  benchmark/actor_benchmarks.cpp
)

target_compile_options(agner_tests PRIVATE -pedantic -Werror -Wall)
target_compile_options(agner_benchmarks PRIVATE -pedantic -Werror -Wall)

target_include_directories(agner_tests
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/test/include
)

target_link_libraries(agner_tests
  PRIVATE
    agner
    GTest::gtest_main
)

target_link_libraries(agner_benchmarks
  PRIVATE
    agner
    benchmark::benchmark_main
)

if(AGNER_ENABLE_COVERAGE)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set(AGNER_COVERAGE_COMPILE_FLAGS --coverage -fprofile-instr-generate -fcoverage-mapping -fcoverage-mcdc)
    set(AGNER_COVERAGE_LINK_FLAGS --coverage -fprofile-instr-generate -fcoverage-mapping)
  else()
    set(AGNER_COVERAGE_COMPILE_FLAGS --coverage)
    set(AGNER_COVERAGE_LINK_FLAGS --coverage)
  endif()
  target_compile_options(agner_tests PRIVATE $<$<CONFIG:Debug>:${AGNER_COVERAGE_COMPILE_FLAGS}>)
  target_link_options(agner_tests PRIVATE $<$<CONFIG:Debug>:${AGNER_COVERAGE_LINK_FLAGS}>)
endif()


add_custom_command(TARGET agner_tests POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E rm -f
          "${CMAKE_SOURCE_DIR}/compile_commands.json"
  COMMAND ${CMAKE_COMMAND} -E create_symlink
          "${CMAKE_BINARY_DIR}/compile_commands.json"
          "${CMAKE_SOURCE_DIR}/compile_commands.json"
)

include(GoogleTest)
gtest_discover_tests(agner_tests)
