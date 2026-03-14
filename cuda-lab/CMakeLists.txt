cmake_minimum_required(VERSION 3.18)
project(cuda_lab LANGUAGES CXX CUDA)

# CUDA standard
set(CMAKE_CUDA_STANDARD 17)
set(CMAKE_CUDA_STANDARD_REQUIRED ON)

# C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find CUDA toolkit
find_package(CUDAToolkit REQUIRED)

# Compiler flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")
set(CMAKE_CUDA_FLAGS "${CMAKE_CUDA_FLAGS} --expt-relaxed-constexpr")

# Debug/Release configurations
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")
set(CMAKE_CUDA_FLAGS_DEBUG "-g -O0 -G")
set(CMAKE_CUDA_FLAGS_RELEASE "-O3 -DNDEBUG")

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/src)

# Kernel sources
set(KERNEL_SOURCES
    src/kernels/page_copy_naive.cu
    src/kernels/page_copy_coalesced.cu
    src/kernels/page_copy_shared_mem.cu
)

# Benchmark sources
set(BENCHMARK_SOURCES
    src/benchmarks/bench_page_copy.cpp
)

# Create kernel library
add_library(kernels STATIC ${KERNEL_SOURCES})
target_link_libraries(kernels PUBLIC CUDA::cudart)
target_include_directories(kernels PUBLIC ${CMAKE_SOURCE_DIR}/src)

# Create benchmark executable
add_executable(bench_page_copy ${BENCHMARK_SOURCES})
target_link_libraries(bench_page_copy kernels CUDA::cudart)

# Tests
option(BUILD_TESTS "Build tests" ON)
if(BUILD_TESTS)
    find_package(GTest QUIET)
    if(GTest_FOUND)
        enable_testing()
        
        add_executable(test_kernels 
            tests/test_kernels.cu 
            tests/test_coalesced.cu 
            tests/test_shared_mem.cu
            tests/test_comprehensive.cu
        )
        target_link_libraries(test_kernels kernels GTest::gtest_main CUDA::cudart)
        add_test(NAME KernelTests COMMAND test_kernels)
    else()
        message(WARNING "Google Test not found - tests will not be built")
    endif()
endif()

# Installation
install(TARGETS kernels bench_page_copy
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)

# Install Nsight scripts
install(PROGRAMS
    nsight/scripts/profile_page_copy.sh
    nsight/scripts/profile_multi_stream.sh
    nsight/scripts/profile_comparison.sh
    nsight/scripts/profile_ncu.sh
    nsight/scripts/compare_kernels.sh
    DESTINATION bin
)

# Print configuration summary
message(STATUS "")
message(STATUS "CUDA Lab Configuration Summary:")
message(STATUS "  CUDA Standard: ${CMAKE_CUDA_STANDARD}")
message(STATUS "  C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  Build Tests: ${BUILD_TESTS}")
message(STATUS "")
