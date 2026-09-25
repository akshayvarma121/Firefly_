@echo off
echo Starting Firefly Audit Suite...
cd core

:: Ensure Git Bash is used to execute the script
set BASH_EXE="C:\Program Files\Git\bin\bash.exe"

if exist %BASH_EXE% (
    %BASH_EXE% -c "./audit.sh"
) else (
    echo [ERROR] Git Bash not found at %BASH_EXE%
    echo Please install Git for Windows, or run audit.sh manually inside WSL/Bash.
)

echo.
echo Press any key to close this window...
pause >nul
