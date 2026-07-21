cmake_minimum_required(VERSION 3.16)
project(distributed_key_value_store VERSION 1.0.0 LANGUAGES CXX)

option(KV_BUILD_TESTS "Build tests" ON)
option(KV_ENABLE_ASAN "Enable address and undefined behavior sanitizers" OFF)
option(KV_ENABLE_TSAN "Enable thread sanitizer" OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Threads REQUIRED)

add_library(kvstore
  src/config.cpp src/failure_detector.cpp src/hash_ring.cpp src/http.cpp src/metrics.cpp
  src/node.cpp src/peer_client.cpp src/replication.cpp src/storage_engine.cpp src/thread_pool.cpp
  src/types.cpp src/wal.cpp)
target_include_directories(kvstore PUBLIC include)
target_link_libraries(kvstore PUBLIC Threads::Threads)
target_compile_options(kvstore PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)

if(KV_ENABLE_ASAN AND KV_ENABLE_TSAN)
  message(FATAL_ERROR "ASan/UBSan and TSan cannot be enabled together")
elseif(KV_ENABLE_ASAN)
  target_compile_options(kvstore PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(kvstore PUBLIC -fsanitize=address,undefined)
elseif(KV_ENABLE_TSAN)
  target_compile_options(kvstore PUBLIC -fsanitize=thread -fno-omit-frame-pointer)
  target_link_options(kvstore PUBLIC -fsanitize=thread)
endif()

add_executable(kv_node apps/kv_node.cpp)
add_executable(kv_cli apps/kv_cli.cpp)
add_executable(kv_benchmark apps/kv_benchmark.cpp)
execute_process(COMMAND git rev-parse --short HEAD WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                OUTPUT_VARIABLE KV_GIT_COMMIT OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(NOT KV_GIT_COMMIT)
  set(KV_GIT_COMMIT "unknown")
endif()
target_compile_definitions(kv_benchmark PRIVATE KV_GIT_COMMIT="${KV_GIT_COMMIT}")
foreach(target kv_node kv_cli kv_benchmark)
  target_link_libraries(${target} PRIVATE kvstore)
  target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endforeach()

include(CTest)
if(KV_BUILD_TESTS)
  include(FetchContent)
  FetchContent_Declare(googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.15.2
    GIT_SHALLOW TRUE)
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)

  add_executable(kv_unit_tests tests/unit/unit_tests.cpp)
  target_link_libraries(kv_unit_tests PRIVATE kvstore GTest::gtest_main)
  add_test(NAME unit COMMAND kv_unit_tests)

  add_executable(kv_integration_tests tests/integration/integration_tests.cpp)
  target_link_libraries(kv_integration_tests PRIVATE kvstore GTest::gtest)
  add_test(NAME integration COMMAND kv_integration_tests $<TARGET_FILE:kv_node>)
  set_tests_properties(integration PROPERTIES TIMEOUT 45)

  add_executable(kv_failure_tests tests/failure/failure_tests.cpp)
  target_link_libraries(kv_failure_tests PRIVATE kvstore GTest::gtest_main)
  add_test(NAME failure_injection COMMAND kv_failure_tests)
endif()
