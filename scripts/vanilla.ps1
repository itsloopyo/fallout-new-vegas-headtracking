#!/usr/bin/env pwsh
# Revert to vanilla (unmodded) game
# Removes HeadTracking mod, and xNVSE ONLY if we installed it
# Usage: pixi run vanilla

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$StateFileName = ".headtracking-state.json"

function Find-GamePath {
    if ($env:FalloutNVPath -and (Test-Path "$env:FalloutNVPath\FalloutNV.exe")) {
        return $env:FalloutNVPath
    }
    $steamPaths = @(
        "C:\Program Files (x86)\Steam\steamapps\common\Fallout New Vegas",
        "C:\Program Files\Steam\steamapps\common\Fallout New Vegas",
        "D:\Steam\steamapps\common\Fallout New Vegas",
        "D:\SteamLibrary\steamapps\common\Fallout New Vegas"
    )
    foreach ($path in $steamPaths) {
        if (Test-Path "$path\FalloutNV.exe") { return $path }
    }
    return $null
}

$gamePath = Find-GamePath
if (-not $gamePath) {
    Write-Host "ERROR: Fallout: New Vegas not found." -ForegroundColor Red
    Write-Host 'Set $env:FalloutNVPath = "C:\Games\Fallout New Vegas"' -ForegroundColor Yellow
    exit 1
}

Write-Host "Reverting to vanilla (unmodded) game..." -ForegroundColor Cyan
Write-Host "  Game path: $gamePath" -ForegroundColor Gray
Write-Host ""

# Read state file
$stateFile = Join-Path $gamePath $StateFileName
$frameworkInstalledByUs = $false

if (Test-Path $stateFile) {
    try {
        $state = Get-Content $stateFile -Raw | ConvertFrom-Json
        $frameworkInstalledByUs = $state.framework.installed_by_us
        Write-Host "  Found state file - respecting installation history" -ForegroundColor Gray
    } catch {
        Write-Host "  Warning: Could not read state file, assuming full removal" -ForegroundColor Yellow
        $frameworkInstalledByUs = $true
    }
} else {
    Write-Host "  No state file found - will remove everything" -ForegroundColor Yellow
    $frameworkInstalledByUs = $true
}

$removed = $false

# Remove HeadTracking mod files
$pluginsDir = Join-Path $gamePath "Data\NVSE\Plugins"
$modFiles = @("HeadTracking.dll", "HeadTracking.dll.bak", "HeadTracking.ini")
foreach ($file in $modFiles) {
    $path = Join-Path $pluginsDir $file
    if (Test-Path $path) {
        Remove-Item $path -Force
        Write-Host "  Removed: Data\NVSE\Plugins\$file" -ForegroundColor Green
        $removed = $true
    }
}

# Only remove xNVSE if we installed it
if ($frameworkInstalledByUs) {
    $nvseDir = Join-Path $gamePath "Data\NVSE"
    if (Test-Path $nvseDir) {
        Remove-Item $nvseDir -Recurse -Force
        Write-Host "  Removed: Data\NVSE\ (entire folder)" -ForegroundColor Green
        $removed = $true
    }

    $nvseFiles = @("nvse_loader.exe", "nvse_steam_loader.dll", "nvse_1_4.dll", "nvse_editor_1_4.dll")
    foreach ($file in $nvseFiles) {
        $path = Join-Path $gamePath $file
        if (Test-Path $path) {
            Remove-Item $path -Force
            Write-Host "  Removed: $file" -ForegroundColor Green
            $removed = $true
        }
    }
} else {
    Write-Host "  xNVSE preserved (was not installed by us)" -ForegroundColor Cyan
}

# Remove state file
if (Test-Path $stateFile) {
    Remove-Item $stateFile -Force
    Write-Host "  Removed: $StateFileName" -ForegroundColor Gray
}

if (-not $removed) {
    Write-Host "  No mod files found - game is already vanilla" -ForegroundColor Yellow
}

Write-Host ""
if ($frameworkInstalledByUs) {
    Write-Host "Game is now completely vanilla (unmodded)" -ForegroundColor Cyan
} else {
    Write-Host "HeadTracking removed, xNVSE preserved for other mods" -ForegroundColor Cyan
}
Write-Host "Use 'pixi run uninstall' to remove only HeadTracking mod" -ForegroundColor Gray
