@echo off
setlocal

echo Starting FastAPI backend...
cd api
start "FastAPI Backend" cmd /c "uvicorn main:app --reload"
cd ..

echo Starting Vite frontend...
cd web
start "Vite Frontend" cmd /c "npm run dev"
cd ..

echo Waiting for servers to start...
timeout /t 3 /nobreak >nul

echo Opening browser...
start http://localhost:5173

echo Servers are running in separate windows. Close those windows to stop the servers.
endlocal
