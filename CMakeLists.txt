cmake_minimum_required(VERSION 3.14)
project(OSWarRoom VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
if(MSVC)
  add_compile_options(/W4 /utf-8)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()
find_package(Threads REQUIRED)

add_library(owr_core STATIC
  kernel/kernel.cpp
  kernel/kernel_json.cpp
  simulation/failure_lab.cpp
  simulation/experiment_lab.cpp
  simulation/detective.cpp)
target_include_directories(owr_core PUBLIC ${CMAKE_SOURCE_DIR})

add_executable(owr_server dashboard/backend/api_server.cpp)
target_link_libraries(owr_server PRIVATE owr_core Threads::Threads)
if(WIN32)
  target_link_libraries(owr_server PRIVATE ws2_32)
endif()

add_executable(owr_cli tools/owr_cli.cpp)
target_link_libraries(owr_cli PRIVATE owr_core)

enable_testing()
add_executable(owr_tests tests/test_main.cpp tests/test_scheduler.cpp tests/test_memory.cpp tests/test_sync_ipc_io.cpp tests/test_kernel.cpp)
target_link_libraries(owr_tests PRIVATE owr_core)
target_compile_definitions(owr_tests PRIVATE OWR_ROOT="${CMAKE_SOURCE_DIR}")
add_test(NAME unit_and_integration COMMAND owr_tests)
