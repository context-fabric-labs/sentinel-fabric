cmake_minimum_required(VERSION 3.18)
project(pm_nixl VERSION 0.1.0 LANGUAGES CXX CUDA)

# C++17 standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# CUDA standard
set(CMAKE_CUDA_STANDARD 17)
set(CMAKE_CUDA_STANDARD_REQUIRED ON)

# Find CUDA toolkit
find_package(CUDAToolkit REQUIRED)

# Compiler flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic")
set(CMAKE_CUDA_FLAGS "${CMAKE_CUDA_FLAGS} --expt-relaxed-constexpr")

# Debug/Release configurations
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")
set(CMAKE_CUDA_FLAGS_DEBUG "-g -O0")
set(CMAKE_CUDA_FLAGS_RELEASE "-O3 -DNDEBUG")

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/include)

# Library sources
set(PM_NIXL_SOURCES
    src/buffer.cpp
    src/descriptor.cpp
    src/error.cpp
    src/transfer.cpp
    src/transfer_engine.cpp
    src/benchmark.cpp
    src/kv_page.cpp
    src/page_gather_engine.cpp
    src/cuda/page_gather.cu
)

# Create static library
add_library(pm_nixl STATIC ${PM_NIXL_SOURCES})
target_link_libraries(pm_nixl PUBLIC CUDA::cudart)
target_include_directories(pm_nixl PUBLIC ${CMAKE_SOURCE_DIR}/include)

# Tests (if BUILD_TESTING is enabled)
option(BUILD_TESTING "Build tests" ON)
if(BUILD_TESTING)
    find_package(GTest QUIET)
    if(GTest_FOUND)
        enable_testing()
        
        # Buffer tests
        add_executable(test_buffer tests/test_buffer.cpp)
        target_link_libraries(test_buffer pm_nixl GTest::gtest_main)
        add_test(NAME BufferTests COMMAND test_buffer)
    else()
        message(WARNING "Google Test not found - tests will not be built")
    endif()
endif()

# Benchmarks (optional)
option(BUILD_BENCHMARKS "Build benchmarks" OFF)
if(BUILD_BENCHMARKS)
    find_package(benchmark QUIET)
    if(benchmark_FOUND)
        add_executable(bench_buffer bench/bench_buffer.cpp)
        target_link_libraries(bench_buffer pm_nixl benchmark::benchmark)
    else()
        message(WARNING "Google Benchmark not found - benchmarks will not be built")
    endif()
    
    # Transfer benchmark (standalone, no Google Benchmark required)
    add_executable(bench_transfer bench/bench_transfer.cpp)
    target_link_libraries(bench_transfer pm_nixl)
    
    # KV page gather benchmark
    add_executable(bench_kv_gather bench/bench_kv_gather.cpp)
    target_link_libraries(bench_kv_gather pm_nixl)
endif()

# Installation
install(TARGETS pm_nixl
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
)

install(DIRECTORY include/pm_nixl
    DESTINATION include
)

# Print configuration summary
message(STATUS "")
message(STATUS "PM-NIXL Configuration Summary:")
message(STATUS "  Version: ${PROJECT_VERSION}")
message(STATUS "  C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  CUDA Standard: ${CMAKE_CUDA_STANDARD}")
message(STATUS "  Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  Build Tests: ${BUILD_TESTING}")
message(STATUS "  Build Benchmarks: ${BUILD_BENCHMARKS}")
message(STATUS "")
