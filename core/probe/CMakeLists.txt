cmake_minimum_required(VERSION 3.20)
project(firefly_probe LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# scikit-build-core injects the pybind11 CMake dir via SKBUILD_PYBIND11_CMAKE_DIR
# or we can ask pybind11 directly.
find_package(pybind11 CONFIG REQUIRED)

pybind11_add_module(firefly_probe src/probe.cpp)

# CRITICAL: DESTINATION . puts the .pyd at the wheel root — exactly where
# Python's import machinery looks.  Any subdirectory here breaks the import.
install(TARGETS firefly_probe LIBRARY DESTINATION .)
