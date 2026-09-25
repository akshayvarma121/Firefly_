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
    $req = [System.Net.HttpWebRequest]::Create($exeUrl)
    $req.UserAgent = "firefly-installer"
    $req.AllowAutoRedirect = $true
    $response = $req.GetResponse()
    $totalBytes = $response.ContentLength
    $stream = $response.GetResponseStream()
    $outStream = [System.IO.File]::Create($exePath)

    $buffer = New-Object byte[] 65536   # 64 KB chunks
    $received = 0
    $startTime = [DateTime]::Now

    while ($true) {
        $read = $stream.Read($buffer, 0, $buffer.Length)
        if ($read -le 0) { break }
        $outStream.Write($buffer, 0, $read)
        $received += $read

        $elapsed = ([DateTime]::Now - $startTime).TotalSeconds
        $speedMBs  = if ($elapsed -gt 0.1) { [math]::Round(($received / 1MB) / $elapsed, 1) } else { 0 }
        $recvMB    = [math]::Round($received / 1MB, 1)
        $totalMB   = [math]::Round($totalBytes / 1MB, 1)
        $pct       = [math]::Round(($received / $totalBytes) * 100, 0)
        Write-Host -NoNewline "`r  $pct% — $recvMB MB / $totalMB MB  |  $speedMBs MB/s   "
    }

    $outStream.Close()
    $stream.Close()
    Write-Host ""

    if ((Get-Item $exePath).Length -lt 1MB) { throw "Downloaded file too small — may be corrupt." }
} catch {
    Write-Host ""
    Write-Host "Error downloading firefly.exe: $_" -ForegroundColor Red
    Write-Host "URL: $exeUrl" -ForegroundColor Red
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
Write-Host "  Engine installed successfully." -ForegroundColor Green
Write-Host ""
Write-Host "  Path: $installDir" -ForegroundColor DarkGray
Write-Host ""
Write-Host "  Execute 'firefly home' to view the command registry." -ForegroundColor Gray
Write-Host "  Context menu integration: Right-click any .mps file -> 'Solve with Firefly'." -ForegroundColor DarkGray
Write-Host ""
