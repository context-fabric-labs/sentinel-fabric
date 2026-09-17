cmake_minimum_required(VERSION 3.20)
project(atmos-sim VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Compiler warnings
add_compile_options(-Wall -Wextra -Wpedantic -Wno-unused-parameter)

# Options
option(ATMOS_SIM_BUILD_TESTS "Build unit tests" ON)
option(ATMOS_SIM_BUILD_BENCHMARKS "Build benchmarks" OFF)

# Main library
add_library(atmos_sim STATIC
    src/sim_core/sim_time.cpp
    src/sim_core/event.cpp
    src/sim_core/event_queue.cpp
    src/sim_core/sim_clock.cpp
    src/sim_core/resource.cpp
    src/sim_core/resource_pool.cpp
    src/sim_core/bounded_queue.cpp
    src/sim_core/arbiter.cpp
    src/sim_core/dependency_tracker.cpp
    src/sim_core/sim_engine.cpp
    src/virtual_devices/virtual_npu.cpp
    src/virtual_devices/hbf_controller.cpp
    src/virtual_devices/lpddr_controller.cpp
    src/virtual_devices/dma_engine.cpp
    src/virtual_devices/pcie_endpoint.cpp
    src/virtual_devices/atmos_module.cpp
    src/topology/topology_graph.cpp
    src/topology/pcie_link.cpp
    src/topology/pcie_switch.cpp
    src/topology/root_complex.cpp
    src/model_graph/sim_op.cpp
    src/model_graph/model_graph.cpp
    src/model_graph/transformer_compiler.cpp
    src/model_graph/request_generator.cpp
    src/metrics/metrics_collector.cpp
    src/metrics/trace_recorder.cpp
    src/metrics/stall_attribution.cpp
    src/calibration/calibration_database.cpp
    src/calibration/parameter_sweep.cpp
)

target_include_directories(atmos_sim PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Tests
if(ATMOS_SIM_BUILD_TESTS)
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.14.0
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    enable_testing()

    # Core tests
    add_executable(test_sim_core
        tests/test_sim_core.cpp
    )
    target_link_libraries(test_sim_core PRIVATE atmos_sim GTest::gtest_main)
    add_test(NAME test_sim_core COMMAND test_sim_core)

    # Virtual device tests
    add_executable(test_virtual_devices
        tests/test_virtual_devices.cpp
    )
    target_link_libraries(test_virtual_devices PRIVATE atmos_sim GTest::gtest_main)
    add_test(NAME test_virtual_devices COMMAND test_virtual_devices)

    # Topology tests
    add_executable(test_topology
        tests/test_topology.cpp
    )
    target_link_libraries(test_topology PRIVATE atmos_sim GTest::gtest_main)
    add_test(NAME test_topology COMMAND test_topology)

    # Model graph tests
    add_executable(test_model_graph
        tests/test_model_graph.cpp
    )
    target_link_libraries(test_model_graph PRIVATE atmos_sim GTest::gtest_main)
    add_test(NAME test_model_graph COMMAND test_model_graph)

    # Metrics tests
    add_executable(test_metrics
        tests/test_metrics.cpp
    )
    target_link_libraries(test_metrics PRIVATE atmos_sim GTest::gtest_main)
    add_test(NAME test_metrics COMMAND test_metrics)

    # Golden trace tests
    add_executable(test_golden_traces
        tests/test_golden_traces.cpp
    )
    target_link_libraries(test_golden_traces PRIVATE atmos_sim GTest::gtest_main)
    add_test(NAME test_golden_traces COMMAND test_golden_traces)

    # End-to-end tests
    add_executable(test_e2e
        tests/test_e2e.cpp
    )
    target_link_libraries(test_e2e PRIVATE atmos_sim GTest::gtest_main)
    add_test(NAME test_e2e COMMAND test_e2e)
endif()

message(STATUS "ATMOS Digital Twin Simulator v${PROJECT_VERSION}")
message(STATUS "  Tests: ${ATMOS_SIM_BUILD_TESTS}")
message(STATUS "  Benchmarks: ${ATMOS_SIM_BUILD_BENCHMARKS}")
