# Firefly Solver - Quick Installer for Windows
# Run via: irm https://bit.ly/install-firefly | iex

$ErrorActionPreference = "Stop"

Write-Host ""
Write-Host "      \ /         " -ForegroundColor Yellow -NoNewline; Write-Host "  ___ _            __ _       " -ForegroundColor White
Write-Host "======= ======= " -ForegroundColor Yellow -NoNewline; Write-Host " | __|(_) _ _  ___ / _|| | _  _ " -ForegroundColor White
Write-Host "  ====   ====   " -ForegroundColor Yellow -NoNewline; Write-Host " | _| | || '_|/ -_)|  _|| || || |" -ForegroundColor White
Write-Host "   /  | |  \    " -ForegroundColor Yellow -NoNewline; Write-Host " |_|  |_||_|  \___||_|  |_| \_, |" -ForegroundColor White
Write-Host "  /   | |   \   " -ForegroundColor Yellow -NoNewline; Write-Host "                            |__/ " -ForegroundColor White
Write-Host "      | |       " -ForegroundColor Yellow
Write-Host "      | |       " -ForegroundColor Yellow -NoNewline; Write-Host "LP/MILP/QP Solver Engine - SIH 2026" -ForegroundColor Gray
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
Write-Host ""
try {
    $wc = New-Object System.Net.WebClient
    $totalBytes = 0
    $receivedBytes = 0
    $startTime = [DateTime]::Now

    # Get total size via HEAD
    $req = [System.Net.WebRequest]::Create($exeUrl)
    $req.Method = "HEAD"
    try {
        $resp = $req.GetResponse()
        $totalBytes = $resp.ContentLength
        $resp.Close()
    } catch {}

    $wc.Headers.Add("User-Agent", "firefly-installer")

    $done = $false
    $wc.DownloadFileCompleted += { $done = $true }
    $wc.DownloadProgressChanged += {
        param($s, $e)
        $receivedBytes = $e.BytesReceived
        $elapsed = ([DateTime]::Now - $startTime).TotalSeconds
        $speedMBs = if ($elapsed -gt 0) { ($receivedBytes / 1MB) / $elapsed } else { 0 }
        $recvMB = [math]::Round($receivedBytes / 1MB, 1)
        $totalMB = if ($totalBytes -gt 0) { [math]::Round($totalBytes / 1MB, 1) } else { "?" }
        $pct = $e.ProgressPercentage
        $line = "  $pct% — $recvMB MB / $totalMB MB  |  $([math]::Round($speedMBs,1)) MB/s"
        Write-Host -NoNewline "`r$line                    "
    }

    $wc.DownloadFileAsync([Uri]$exeUrl, $exePath)
    while (-not $done) { Start-Sleep -Milliseconds 200 }
    Write-Host ""

    if (-not (Test-Path $exePath) -or (Get-Item $exePath).Length -lt 1MB) {
        throw "Downloaded file is missing or too small."
    }
} catch {
    Write-Host ""
    Write-Host "Error downloading firefly.exe: $_" -ForegroundColor Red
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
Write-Host "      \ /         " -ForegroundColor Yellow -NoNewline; Write-Host "  ___ _            __ _       " -ForegroundColor White
Write-Host "======= ======= " -ForegroundColor Yellow -NoNewline; Write-Host " | __|(_) _ _  ___ / _|| | _  _ " -ForegroundColor White
Write-Host "  ====   ====   " -ForegroundColor Yellow -NoNewline; Write-Host " | _| | || '_|/ -_)|  _|| || || |" -ForegroundColor White
Write-Host "   /  | |  \    " -ForegroundColor Yellow -NoNewline; Write-Host " |_|  |_||_|  \___||_|  |_| \_, |" -ForegroundColor White
Write-Host "  /   | |   \   " -ForegroundColor Yellow -NoNewline; Write-Host "                            |__/ " -ForegroundColor White
Write-Host "      | |       " -ForegroundColor Yellow
Write-Host "      | |       " -ForegroundColor Yellow -NoNewline; Write-Host "LP/MILP/QP Solver Engine - SIH 2026" -ForegroundColor Gray
Write-Host ""
Write-Host "You can now open a new Command Prompt or PowerShell window and type:" -ForegroundColor White
Write-Host "  firefly solve my_problem.mps" -ForegroundColor Cyan
Write-Host "Or just right-click any .mps file and select 'Solve with Firefly'." -ForegroundColor Cyan
Write-Host ""
