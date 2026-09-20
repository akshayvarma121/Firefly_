cmake_minimum_required(VERSION 3.20)
project(FireflySolver LANGUAGES CXX)

option(BUILD_PYTHON_BINDINGS "Build pybind11 Python module (firefly_solver)" OFF)

include(FetchContent)
FetchContent_Declare(
    eigen
    URL https://gitlab.com/libeigen/eigen/-/archive/3.4.0/eigen-3.4.0.tar.gz
)
set(BUILD_TESTING OFF CACHE BOOL "Disable Eigen tests" FORCE)
set(EIGEN_BUILD_TESTING OFF CACHE BOOL "Disable Eigen tests" FORCE)
FetchContent_MakeAvailable(eigen)
set(BUILD_TESTING ON CACHE BOOL "Re-enable tests for firefly" FORCE)


set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(CheckLanguage)
check_language(CUDA)

if(CMAKE_CUDA_COMPILER)
    option(WITH_CUDA "Enable CUDA support" ON)
else()
    option(WITH_CUDA "Enable CUDA support" OFF)
endif()

option(WITH_ASAN "Enable Address Sanitizer" OFF)
if(WITH_ASAN AND MSVC)
    add_compile_options(/fsanitize=address /Zi)
    add_link_options(/INCREMENTAL:NO /DEBUG)
endif()

if(WITH_CUDA)
    enable_language(CUDA)
    # Set CUDA architecture to sm_89 (RTX 4050)
    set(CMAKE_CUDA_ARCHITECTURES 89)
    
    # Find CUDAToolkit to link against specific libraries
    find_package(CUDAToolkit REQUIRED)
endif()

# Prefer STATIC libraries for internal components
add_library(firefly_core STATIC src/core_dummy.cpp src/mps_parser.cpp src/presolve.cpp src/simplex.cpp src/pdlp.cpp src/pdlp_cpu.cpp src/branch_and_bound.cpp)
target_include_directories(firefly_core PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${eigen_SOURCE_DIR}
)

if(WITH_CUDA)
    target_sources(firefly_core PRIVATE src/pdlp_cuda.cu)
    target_compile_definitions(firefly_core PRIVATE FIREFLY_WITH_CUDA)
    target_link_libraries(firefly_core PUBLIC CUDA::cudart CUDA::cusparse CUDA::cublas)
endif()

# Enable testing for the project
enable_testing()

add_subdirectory(tests)

# ---------------------------------------------------------------------------
# Python bindings — built only when scikit-build-core sets BUILD_PYTHON_BINDINGS=ON
# ---------------------------------------------------------------------------
if(BUILD_PYTHON_BINDINGS)
    # Silence the CMP0148 deprecation from pybind11's old FindPythonLibsNew path.
    # pybind11 3.x supports the modern FindPython when PYBIND11_FINDPYTHON is ON.
    set(PYBIND11_FINDPYTHON ON)
    find_package(pybind11 CONFIG REQUIRED)

    pybind11_add_module(firefly_solver bindings/bindings.cpp)

    target_include_directories(firefly_solver PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${eigen_SOURCE_DIR}
    )

    # Link against the static core (already built above)
    target_link_libraries(firefly_solver PRIVATE firefly_core)

    target_compile_features(firefly_solver PRIVATE cxx_std_20)

    if(WITH_CUDA)
        target_compile_definitions(firefly_solver PRIVATE FIREFLY_WITH_CUDA)
    endif()

    # CRITICAL: DESTINATION . places the .pyd at the wheel root.
    # Any subdirectory here will cause "builds but won't import" failures.
    install(TARGETS firefly_solver LIBRARY DESTINATION .)
endif()
