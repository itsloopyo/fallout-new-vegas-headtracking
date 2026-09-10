#!/usr/bin/env pwsh
#Requires -Version 5.1
# Run uninstall.cmd against every installed copy of Fallout: New Vegas.
#
# uninstall.cmd is the launcher contract and takes one game path, which is
# right for the launcher: it knows which install it deployed to. A dev
# uninstall wants the other thing - the mod removed from every copy on the
# machine, so a later test cannot pick up a dsound.dll left behind in the copy
# the single-path resolver did not choose.

param(
    [switch]$Force
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
Import-Module (Join-Path $scriptDir 'FnvInstalls.psm1') -Force

$gamePaths = @(Get-FnvInstalls)
$uninstall = Join-Path $scriptDir 'uninstall.cmd'

foreach ($path in $gamePaths) {
    Write-Host ""
    Write-Host "--- $path" -ForegroundColor Cyan
    if ($Force) {
        & cmd.exe /c $uninstall $path /y /force
    } else {
        & cmd.exe /c $uninstall $path /y
    }
    if ($LASTEXITCODE -ne 0) {
        throw "uninstall.cmd failed for $path (exit $LASTEXITCODE)"
    }
}
