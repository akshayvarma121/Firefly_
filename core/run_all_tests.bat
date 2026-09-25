@echo off
setlocal

cd /d "%~dp0"

echo ========================================
echo     Firefly Solver - Core Test Runner
echo ========================================

echo [1/3] Configuring CMake...
if not exist build mkdir build
cd build
cmake ..

echo [2/3] Building core and tests...
cmake --build . --config Debug

echo [3/3] Running tests...
ctest -C Debug -V
set CTEST_EXIT_CODE=%ERRORLEVEL%

echo ========================================
if %CTEST_EXIT_CODE% EQU 0 (
    echo [PASS] All tests passed successfully. (FULLY GREEN)
    echo ========================================
    pause
    exit /b 0
) else (
    echo [FAIL] One or more tests failed.
    echo ========================================
    pause
    exit /b %CTEST_EXIT_CODE%
)
