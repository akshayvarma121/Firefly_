#!/bin/bash

# Exit immediately if a command exits with a non-zero status.
set -e

# Change to the directory where the script is located
cd "$(dirname "$0")"

echo "========================================"
echo "    Firefly Solver - Core Test Runner"
echo "========================================"

echo "[1/3] Configuring CMake..."
mkdir -p build
cd build
cmake ..

echo "[2/3] Building core and tests..."
cmake --build . --config Debug

echo [3/3] Running tests...
# Disable exit-on-error temporarily so we can format the output
set +e
ctest -C Debug -V
CTEST_EXIT_CODE=$?
set -e

echo "========================================"
if [ $CTEST_EXIT_CODE -eq 0 ]; then
    echo "✅ PASS: All tests passed successfully. (FULLY GREEN)"
    echo "========================================"
    exit 0
else
    echo "❌ FAIL: One or more tests failed."
    echo "========================================"
    exit $CTEST_EXIT_CODE
fi
