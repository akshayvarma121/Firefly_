$ErrorActionPreference = "Stop"

Write-Host "Running test_pdlp on CPU build..."
$cpuOutput = & "..\build_cpu\tests\Debug\test_pdlp.exe"
if ($LASTEXITCODE -ne 0) {
    Write-Error "CPU test failed"
    exit 1
}

Write-Host "Running test_pdlp on CUDA build..."
$cudaOutput = & "..\build_cuda\tests\Debug\test_pdlp.exe"
if ($LASTEXITCODE -ne 0) {
    Write-Error "CUDA test failed"
    exit 1
}

$cpuObj = ($cpuOutput | Select-String -Pattern "Objective:\s+([-\d.]+)").Matches.Groups[1].Value
$cudaObj = ($cudaOutput | Select-String -Pattern "Objective:\s+([-\d.]+)").Matches.Groups[1].Value

Write-Host "CPU Objective: $cpuObj"
Write-Host "CUDA Objective: $cudaObj"

if ($cpuObj -ne $cudaObj) {
    Write-Error "Parity mismatch! CPU=$cpuObj, CUDA=$cudaObj"
    exit 1
}

Write-Host "CPU-Fallback Parity Test passed! (WITH_CUDA=ON vs OFF produce matching results)"
exit 0
