cmake_minimum_required(VERSION 3.24)
project(firefly LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(WITH_CUDA "Enable CUDA acceleration" ON)

if(WITH_CUDA)
    include(CheckLanguage)
    check_language(CUDA)
    if(CMAKE_CUDA_COMPILER)
        enable_language(CUDA)
        set(CMAKE_CUDA_ARCHITECTURES 89)
        add_compile_definitions(FIREFLY_USE_CUDA)
    else()
        message(WARNING "CUDA compiler not found. Falling back to CPU-only mode.")
        set(WITH_CUDA OFF CACHE BOOL "Enable CUDA acceleration" FORCE)
    endif()
endif()

# FetchContent for Pybind11
include(FetchContent)
FetchContent_Declare(
    pybind11
    GIT_REPOSITORY https://github.com/pybind/pybind11.git
    GIT_TAG        v2.11.1
)
FetchContent_MakeAvailable(pybind11)

# FetchContent for Eigen
FetchContent_Declare(
    eigen
    GIT_REPOSITORY https://gitlab.com/libeigen/eigen.git
    GIT_TAG        3.4.0
)
FetchContent_MakeAvailable(eigen)

# Core library
add_library(firefly_core src/main.cpp src/MPSParser.cpp src/Presolve.cpp src/Standardizer.cpp src/SimplexSolver.cpp src/PDLPSolver.cpp src/PDLP_CPU.cpp src/BranchAndBoundSolver.cpp)
if(WITH_CUDA)
    find_package(CUDAToolkit REQUIRED)
    target_sources(firefly_core PRIVATE src/PDLP_CUDA.cu)
    target_link_libraries(firefly_core PUBLIC CUDA::cublas CUDA::cusparse)
endif()

target_include_directories(firefly_core PUBLIC include)
target_link_libraries(firefly_core PUBLIC Eigen3::Eigen)

# Main Executable
add_executable(firefly_bin src/main.cpp)
target_link_libraries(firefly_bin PRIVATE firefly_core)
if(WITH_CUDA)
    # E.g. target_sources(firefly_bin PRIVATE src/solver.cu)
endif()

# Python Bindings
pybind11_add_module(firefly_solver bindings/solver_bindings.cpp)
target_link_libraries(firefly_solver PRIVATE firefly_core)
install(TARGETS firefly_solver DESTINATION .)

# Minimal Test Runner
add_executable(firefly_test tests/test_main.cpp)
target_link_libraries(firefly_test PRIVATE firefly_core)

# Presolve Test
add_executable(firefly_presolve_test tests/test_presolve.cpp)
target_link_libraries(firefly_presolve_test PRIVATE firefly_core)

# Simplex Test
add_executable(firefly_simplex_test tests/test_simplex.cpp)
target_link_libraries(firefly_simplex_test PRIVATE firefly_core)

# PDLP Test
add_executable(firefly_pdlp_test tests/test_pdlp.cpp)
target_link_libraries(firefly_pdlp_test PRIVATE firefly_core)

add_executable(firefly_bnb_test tests/test_bnb.cpp)
target_link_libraries(firefly_bnb_test PRIVATE firefly_core)
