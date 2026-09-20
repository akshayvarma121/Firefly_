cmake -B build
cmake --build build --config Debug --target benchmark_gpu

$job = Start-Job { & E:\firefly\core\build\tests\Debug\benchmark_gpu.exe }
Start-Sleep -Seconds 1

while ($job.State -eq 'Running') {
    nvidia-smi --query-gpu=memory.used --format=csv,noheader
    Start-Sleep -Milliseconds 500
}

Receive-Job $job
