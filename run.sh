#!/bin/bash

# Cleanup function to kill background processes on exit
cleanup() {
    echo "Stopping servers..."
    kill $API_PID $WEB_PID 2>/dev/null
    exit
}

# Trap Ctrl+C and termination signals
trap cleanup SIGINT SIGTERM

echo "Starting FastAPI backend..."
cd api || exit
uvicorn main:app --reload &
API_PID=$!
cd ..

echo "Starting Vite frontend..."
cd web || exit
npm run dev &
WEB_PID=$!
cd ..

echo "Waiting for servers to start..."
sleep 3

echo "Opening browser..."
if which xdg-open > /dev/null; then
  xdg-open http://localhost:5173
elif which open > /dev/null; then
  open http://localhost:5173
else
  echo "Please open http://localhost:5173 in your browser."
fi

echo "Servers are running. Press Ctrl+C to stop."
wait $API_PID $WEB_PID
