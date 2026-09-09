#!/usr/bin/env pwsh
#Requires -Version 5.1
# Remove the mod from every install of the game on this machine.
# Usage: pixi run uninstall
#
# uninstall.cmd is the launcher contract and targets one install: the launcher
# passes the path it means and expects exactly that copy touched. This wrapper
# is the dev-side equivalent, calling it once per detected install so a second
# copy is not left holding a stale HeadTracking.dll.

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
Import-Module (Join-Path $scriptDir "FnvInstalls.psm1") -Force

$gamePaths = Get-FnvInstalls

$uninstallCmd = Join-Path $scriptDir "uninstall.cmd"
if (-not (Test-Path $uninstallCmd)) {
    throw "uninstall.cmd not found at $uninstallCmd"
}

$failed = @()
foreach ($gamePath in $gamePaths) {
    Write-Host ""
    Write-Host "Game path: $gamePath" -ForegroundColor Cyan
    # Invoked directly rather than through `cmd /c "<script> <args>"`: /c
    # re-parses the reassembled line, and the Steam path's "(x86)" then reaches
    # cmd unquoted and fails with "The filename, directory name, or volume
    # label syntax is incorrect."
    & $uninstallCmd $gamePath /y
    if ($LASTEXITCODE -ne 0) { $failed += "$gamePath (exit $LASTEXITCODE)" }
}

if ($failed.Count -gt 0) {
    Write-Host ""
    Write-Host "Uninstall failed for:" -ForegroundColor Red
    $failed | ForEach-Object { Write-Host "  $_" -ForegroundColor Red }
    exit 1
}
