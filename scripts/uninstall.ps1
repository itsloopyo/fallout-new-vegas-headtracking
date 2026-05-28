#!/usr/bin/env pwsh
# Remove HeadTracking mod only (keeps NVSE for other mods)
# Usage: pixi run uninstall

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path

# Import shared modules
$coreModulesDir = Join-Path $scriptDir "..\cameraunlock-core\powershell"
$modulesDir = Join-Path $scriptDir "modules"

if (Test-Path $modulesDir) {
    Import-Module (Join-Path $modulesDir "GamePathDetection.psm1") -Force
} elseif (Test-Path $coreModulesDir) {
    Import-Module (Join-Path $coreModulesDir "GamePathDetection.psm1") -Force
} else {
    Write-Host "ERROR: Could not find required PowerShell modules." -ForegroundColor Red
    exit 1
}

$gamePath = Find-GamePath -GameId 'fallout-new-vegas'
if (-not $gamePath) {
    Write-Host "ERROR: Fallout: New Vegas not found." -ForegroundColor Red
    exit 1
}

$pluginsDir = Join-Path $gamePath "Data\NVSE\Plugins"

Write-Host "Uninstalling HeadTracking mod..." -ForegroundColor Cyan
Write-Host "  Game path: $gamePath" -ForegroundColor Gray

$removed = $false

$modFiles = @("HeadTracking.dll", "HeadTracking.dll.bak", "HeadTracking.ini")
foreach ($file in $modFiles) {
    $path = Join-Path $pluginsDir $file
    if (Test-Path $path) {
        Remove-Item $path -Force
        Write-Host "  Removed: $file" -ForegroundColor Green
        $removed = $true
    }
}

if (-not $removed) {
    Write-Host "  No mod files found - already uninstalled" -ForegroundColor Yellow
}

Write-Host ""
Write-Host "HeadTracking mod uninstalled" -ForegroundColor Cyan
Write-Host "NVSE remains intact for other mods" -ForegroundColor Gray
Write-Host "Run 'pixi run install' to reinstall" -ForegroundColor Gray
