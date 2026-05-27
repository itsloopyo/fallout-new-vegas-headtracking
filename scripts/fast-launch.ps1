#!/usr/bin/env pwsh
# Toggle fast-launch tweaks for debug testing.
#   pixi run fast-launch      -> apply tweaks (idempotent)
#   pixi run restore-launch   -> revert tweaks
#
# Tweaks:
#   - Rename Data\Video\FNVIntro.bik -> FNVIntro.bik.fastlaunch-disabled
#   - Blank sIntroMovie= in Fallout.ini (saved into state file for restore)

[CmdletBinding()]
param([switch]$Restore)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Find-GamePath {
    if ($env:FalloutNVPath -and (Test-Path "$env:FalloutNVPath\FalloutNV.exe")) {
        return $env:FalloutNVPath
    }
    foreach ($p in @(
        "C:\Program Files (x86)\Steam\steamapps\common\Fallout New Vegas",
        "C:\Program Files\Steam\steamapps\common\Fallout New Vegas",
        "D:\Steam\steamapps\common\Fallout New Vegas",
        "D:\SteamLibrary\steamapps\common\Fallout New Vegas"
    )) {
        if (Test-Path "$p\FalloutNV.exe") { return $p }
    }
    throw "Fallout: New Vegas not found. Set `$env:FalloutNVPath."
}

$gamePath  = Find-GamePath
$iniPath   = "$env:USERPROFILE\Documents\My Games\FalloutNV\Fallout.ini"
$bikLive   = Join-Path $gamePath "Data\Video\FNVIntro.bik"
$bikParked = Join-Path $gamePath "Data\Video\FNVIntro.bik.fastlaunch-disabled"
$stateFile = Join-Path $gamePath ".fast-launch-state.json"

if (-not (Test-Path $iniPath)) {
    throw "Fallout.ini not found at $iniPath. Launch the game once to generate it."
}

function Set-IniKey([string]$path, [string]$key, [string]$value) {
    $lines = Get-Content -LiteralPath $path
    $hit = $false
    for ($i = 0; $i -lt $lines.Count; $i++) {
        if ($lines[$i] -match "^\s*$([regex]::Escape($key))\s*=") {
            $lines[$i] = "$key=$value"
            $hit = $true
            break
        }
    }
    if (-not $hit) { throw "Key '$key' not found in $path" }
    $item = Get-Item -LiteralPath $path
    $wasReadOnly = $item.IsReadOnly
    if ($wasReadOnly) { $item.IsReadOnly = $false }
    try {
        Set-Content -LiteralPath $path -Value $lines -Encoding ASCII
    } finally {
        if ($wasReadOnly) { (Get-Item -LiteralPath $path).IsReadOnly = $true }
    }
}

function Get-IniKey([string]$path, [string]$key) {
    $m = Select-String -LiteralPath $path -Pattern "^\s*$([regex]::Escape($key))\s*=(.*)$"
    if (-not $m) { throw "Key '$key' not found in $path" }
    return $m.Matches[0].Groups[1].Value
}

if ($Restore) {
    Write-Host "Restoring vanilla launch sequence..." -ForegroundColor Cyan

    if (-not (Test-Path $stateFile)) {
        throw "No state file at $stateFile - nothing to restore. (Were fast-launch tweaks ever applied?)"
    }
    $state = Get-Content -LiteralPath $stateFile -Raw | ConvertFrom-Json

    Set-IniKey $iniPath 'sIntroMovie' $state.sIntroMovie
    Write-Host "  Fallout.ini  sIntroMovie restored to '$($state.sIntroMovie)'" -ForegroundColor Green

    if (Test-Path $bikParked) {
        if (Test-Path $bikLive) {
            throw "Both $bikLive and $bikParked exist - resolve manually before restoring."
        }
        Rename-Item -LiteralPath $bikParked -NewName "FNVIntro.bik"
        Write-Host "  Video        FNVIntro.bik renamed back" -ForegroundColor Green
    } else {
        Write-Host "  Video        FNVIntro.bik already in place" -ForegroundColor Gray
    }

    Remove-Item -LiteralPath $stateFile -Force
    Write-Host "Done. Game will play its full intro sequence again." -ForegroundColor Cyan
    return
}

Write-Host "Applying fast-launch tweaks..." -ForegroundColor Cyan
Write-Host "  Game path: $gamePath" -ForegroundColor Gray

$existing = if (Test-Path $stateFile) { Get-Content -LiteralPath $stateFile -Raw | ConvertFrom-Json } else { $null }

$origIntroMovie = if ($existing) { $existing.sIntroMovie } else { Get-IniKey $iniPath 'sIntroMovie' }
$state = [pscustomobject]@{
    sIntroMovie = $origIntroMovie
    appliedAt   = (Get-Date).ToString('o')
}
$state | ConvertTo-Json | Set-Content -LiteralPath $stateFile -Encoding UTF8

$currentIntroMovie = Get-IniKey $iniPath 'sIntroMovie'
if ([string]::IsNullOrEmpty($currentIntroMovie)) {
    Write-Host "  Fallout.ini  sIntroMovie already blank" -ForegroundColor Gray
} else {
    Set-IniKey $iniPath 'sIntroMovie' ''
    Write-Host "  Fallout.ini  sIntroMovie cleared (was '$currentIntroMovie')" -ForegroundColor Green
}

if (Test-Path $bikLive) {
    if (Test-Path $bikParked) {
        throw "Both $bikLive and $bikParked exist - resolve manually before applying."
    }
    Rename-Item -LiteralPath $bikLive -NewName "FNVIntro.bik.fastlaunch-disabled"
    Write-Host "  Video        FNVIntro.bik parked" -ForegroundColor Green
} elseif (Test-Path $bikParked) {
    Write-Host "  Video        FNVIntro.bik already parked" -ForegroundColor Gray
} else {
    Write-Host "  Video        FNVIntro.bik not present (DLC-less or pre-stripped install)" -ForegroundColor Yellow
}

Write-Host ""
Write-Host "Fast-launch tweaks applied. Run 'pixi run restore-launch' to revert." -ForegroundColor Cyan
Write-Host "Already-blank chain: SIntroSequence, SMainMenuMovieIntro." -ForegroundColor Gray
Write-Host "For fastest test loop: keep a save just outside Doc Mitchell's house and load it directly from the main menu." -ForegroundColor Gray
