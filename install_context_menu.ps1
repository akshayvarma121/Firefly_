# Requires -RunAsAdministrator
# This script adds "Solve with Firefly" to the right-click context menu for .mps files

$ErrorActionPreference = "Stop"

# Get the absolute path to the firefly.exe built by PyInstaller
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$exePath = Join-Path $scriptDir "core\dist\firefly.exe"

if (-not (Test-Path $exePath)) {
    Write-Host "Error: firefly.exe not found at $exePath" -ForegroundColor Red
    Write-Host "Please build the PyInstaller executable first."
    Read-Host "Press Enter to exit..."
    exit 1
}

$regPath = "HKCU:\Software\Classes\SystemFileAssociations\.mps\shell\Firefly"
$commandPath = "$regPath\command"

try {
    if (-not (Test-Path $regPath)) {
        New-Item -Path $regPath -Force | Out-Null
    }
    
    # Set the icon to be the firefly executable itself (if it had an icon, it would show, otherwise default exe icon)
    New-ItemProperty -Path $regPath -Name "Icon" -Value "`"$exePath`"" -Force | Out-Null
    
    # Set the text that appears in the context menu
    New-ItemProperty -Path $regPath -Name "MUIVerb" -Value "Solve with Firefly" -Force | Out-Null
    
    # Create the command key
    if (-not (Test-Path $commandPath)) {
        New-Item -Path $commandPath -Force | Out-Null
    }
    
    # Set the actual command to execute
    # The `%1` will be replaced by the path of the right-clicked .mps file
    New-ItemProperty -Path $commandPath -Name "(default)" -Value "`"$exePath`" solve `"%1`"" -Force | Out-Null

    Write-Host "Successfully installed 'Solve with Firefly' context menu!" -ForegroundColor Green
    Write-Host "You can now right-click any .mps file in Windows Explorer and select 'Solve with Firefly'."
} catch {
    Write-Host "Failed to install context menu. Ensure you are running as Administrator if required." -ForegroundColor Red
    Write-Host $_.Exception.Message
}

Read-Host "Press Enter to exit..."
