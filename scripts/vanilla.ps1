#!/usr/bin/env pwsh
# Revert to vanilla (unmodded) game
# Removes HeadTracking mod, and xNVSE ONLY if we installed it
# Usage: pixi run vanilla

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$StateFileName = ".headtracking-state.json"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectRoot = Split-Path -Parent $scriptDir
$modulePath = Join-Path $projectRoot "cameraunlock-core\powershell\GamePathDetection.psm1"
Import-Module $modulePath -Force
Import-Module (Join-Path $scriptDir "FnvInstalls.psm1") -Force

Write-Host "Reverting to vanilla (unmodded) game..." -ForegroundColor Cyan
$gamePaths = Get-FnvInstalls

foreach ($gamePath in $gamePaths) {
    Write-Host ""
    Write-Host "Game path: $gamePath" -ForegroundColor Cyan

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

    # The mod itself: the proxy and its config, next to the exe.
    $modFiles = @("dsound.dll", "HeadTracking.ini", "HeadTracking.log", "HeadTracking.prev.log")
    foreach ($file in $modFiles) {
        $path = Join-Path $gamePath $file
        if (Test-Path $path) {
            Remove-Item $path -Force
            Write-Host "  Removed: $file" -ForegroundColor Green
            $removed = $true
        }
    }

    # Older builds of this mod deployed as an xNVSE plugin.
    $legacyDir = Join-Path $gamePath "Data\NVSE\Plugins"
    foreach ($file in @("HeadTracking.dll", "HeadTracking.dll.bak", "HeadTracking.ini",
                        "HeadTracking.log", "HeadTracking.prev.log")) {
        $path = Join-Path $legacyDir $file
        if (Test-Path $path) {
            Remove-Item $path -Force
            Write-Host "  Removed: Data\NVSE\Plugins\$file (legacy)" -ForegroundColor Green
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
        Write-Host "  Now completely vanilla (unmodded)" -ForegroundColor Cyan
    } else {
        Write-Host "  HeadTracking removed, xNVSE preserved for other mods" -ForegroundColor Cyan
    }
}

Write-Host ""
Write-Host "Use 'pixi run uninstall' to remove only HeadTracking mod" -ForegroundColor Gray
