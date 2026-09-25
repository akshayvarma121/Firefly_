# Firefly Solver - Quick Installer for Windows
# Run via: irm https://bit.ly/install-firefly | iex

$ErrorActionPreference = "Stop"

Write-Host ""
Write-Host "  ___  _            __  _       " -ForegroundColor Yellow
Write-Host " | __|(_) _ _  ___ / _|| | _  _ " -ForegroundColor Yellow
Write-Host " | _| | || '_|/ -_)|  _|| || || |" -ForegroundColor Yellow
Write-Host " |_|  |_||_|  \___||_|  |_| \_, |" -ForegroundColor Yellow
Write-Host "                            |__/ " -ForegroundColor Yellow
Write-Host ""
Write-Host "       LP/MILP/QP Solver Engine - SIH 2026" -ForegroundColor Gray
Write-Host ""

$installDir = "$env:LOCALAPPDATA\Firefly\bin"
$exePath = "$installDir\firefly.exe"

# 1. Create directory
if (-not (Test-Path $installDir)) {
    Write-Host "Creating installation directory: $installDir..."
    New-Item -ItemType Directory -Force -Path $installDir | Out-Null
}

# 2. Download latest executable
$exeUrl = "https://github.com/akshayvarma121/Firefly_solver/releases/download/v0.1.0/firefly.exe"
Write-Host "Downloading Firefly CLI from GitHub Releases..."
try {
    $ProgressPreference = 'SilentlyContinue'
    Invoke-WebRequest -Uri $exeUrl -OutFile $exePath -UseBasicParsing
    $ProgressPreference = 'Continue'
} catch {
    Write-Host "Error downloading firefly.exe. Please ensure a GitHub Release exists with this asset." -ForegroundColor Red
    Write-Host "URL Attempted: $exeUrl" -ForegroundColor Red
    exit 1
}

# 3. Add to User PATH
$userPath = [Environment]::GetEnvironmentVariable("PATH", "User")
if ($userPath -notlike "*$installDir*") {
    Write-Host "Adding $installDir to User PATH..."
    $newPath = "$userPath;$installDir"
    [Environment]::SetEnvironmentVariable("PATH", $newPath, "User")
    $env:PATH = "$env:PATH;$installDir"
}

# 4. Install Context Menu (Solve with Firefly)
Write-Host "Registering Windows Context Menu ('Solve with Firefly')..."
$regPath = "HKCU:\Software\Classes\SystemFileAssociations\.mps\shell\Firefly"
$commandPath = "$regPath\command"

try {
    if (-not (Test-Path $regPath)) { New-Item -Path $regPath -Force | Out-Null }
    New-ItemProperty -Path $regPath -Name "Icon" -Value "`"$exePath`"" -Force | Out-Null
    New-ItemProperty -Path $regPath -Name "MUIVerb" -Value "Solve with Firefly" -Force | Out-Null
    if (-not (Test-Path $commandPath)) { New-Item -Path $commandPath -Force | Out-Null }
    New-ItemProperty -Path $commandPath -Name "(default)" -Value "`"$exePath`" solve `"%1`"" -Force | Out-Null
} catch {
    Write-Host "Failed to register context menu." -ForegroundColor Yellow
}

Write-Host ""
Write-Host "Installation Complete!" -ForegroundColor Green
Write-Host "You can now open a new Command Prompt or PowerShell window and type:" -ForegroundColor White
Write-Host "  firefly solve my_problem.mps" -ForegroundColor Cyan
Write-Host "Or just right-click any .mps file and select 'Solve with Firefly'." -ForegroundColor Cyan
Write-Host ""
