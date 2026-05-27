#!/usr/bin/env pwsh
# Install HeadTracking NVSE plugin
# Usage: pixi run install

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

Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  HeadTracking for Fallout: NV" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

$gamePath = Find-GamePath -GameId 'fallout-new-vegas'
if (-not $gamePath) {
    Write-GameNotFoundError -GameName 'Fallout: New Vegas' -EnvVar 'FalloutNVPath' -SteamFolder 'Fallout New Vegas'
    exit 1
}

Write-Host "Found Fallout: New Vegas at:" -ForegroundColor Green
Write-Host "  $gamePath" -ForegroundColor White
Write-Host ""

# Check for xNVSE - auto-install if missing
$nvseLoader = Join-Path $gamePath "nvse_loader.exe"
if (-not (Test-Path $nvseLoader)) {
    Write-Host "xNVSE not detected - installing automatically..." -ForegroundColor Yellow

    $ProgressPreference = 'SilentlyContinue'
    $apiUrl = "https://api.github.com/repos/xNVSE/NVSE/releases/latest"

    try {
        $release = Invoke-RestMethod -Uri $apiUrl -Headers @{ "User-Agent" = "HeadTracking-Installer" }
    } catch {
        Write-Host "ERROR: Failed to fetch xNVSE release info from GitHub: $_" -ForegroundColor Red
        exit 1
    }

    # Find the zip asset (exclude source archives)
    $asset = $release.assets | Where-Object {
        $_.name -match '\.zip$' -and $_.name -notmatch 'source|src'
    } | Select-Object -First 1

    if (-not $asset) {
        Write-Host "ERROR: Could not find xNVSE download asset in release" -ForegroundColor Red
        exit 1
    }

    $tempZip = Join-Path $env:TEMP "xNVSE_install.zip"
    Write-Host "  Downloading: $($asset.name)..." -ForegroundColor Gray

    try {
        Invoke-WebRequest -Uri $asset.browser_download_url -OutFile $tempZip -UseBasicParsing
    } catch {
        Write-Host "ERROR: Failed to download xNVSE: $_" -ForegroundColor Red
        exit 1
    }

    # Extract to temp directory first to handle nested folder structure
    $tempExtract = Join-Path $env:TEMP "xNVSE_extract"
    if (Test-Path $tempExtract) {
        Remove-Item $tempExtract -Recurse -Force
    }

    Write-Host "  Extracting to game directory..." -ForegroundColor Gray
    try {
        Expand-Archive -Path $tempZip -DestinationPath $tempExtract -Force
    } catch {
        Write-Host "ERROR: Failed to extract xNVSE: $_" -ForegroundColor Red
        exit 1
    }

    # xNVSE zips often have a nested folder - find where nvse_loader.exe is
    $nvseLoaderInExtract = Get-ChildItem -Path $tempExtract -Recurse -Filter "nvse_loader.exe" | Select-Object -First 1
    if ($nvseLoaderInExtract) {
        $sourceDir = $nvseLoaderInExtract.DirectoryName
        Copy-Item -Path "$sourceDir\*" -Destination $gamePath -Recurse -Force
    } else {
        # Flat structure - copy everything
        Copy-Item -Path "$tempExtract\*" -Destination $gamePath -Recurse -Force
    }

    # Cleanup
    Remove-Item $tempZip -Force -ErrorAction SilentlyContinue
    Remove-Item $tempExtract -Recurse -Force -ErrorAction SilentlyContinue

    # Verify installation
    if (-not (Test-Path $nvseLoader)) {
        Write-Host "ERROR: xNVSE installation failed - nvse_loader.exe not found after extraction" -ForegroundColor Red
        exit 1
    }

    Write-Host "  xNVSE installed successfully!" -ForegroundColor Green
    Write-Host ""
}

# Create plugins directory if needed
$pluginsDir = Join-Path $gamePath "Data\NVSE\Plugins"
if (-not (Test-Path $pluginsDir)) {
    New-Item -ItemType Directory -Path $pluginsDir -Force | Out-Null
    Write-Host "Created: Data\NVSE\Plugins" -ForegroundColor Gray
}

# Check for required files
$pluginDir = Join-Path $scriptDir "plugin"
$dllSource = Join-Path $pluginDir "HeadTracking.dll"
$iniSource = Join-Path $pluginDir "HeadTracking.ini"

if (-not (Test-Path $dllSource)) {
    Write-Host "ERROR: HeadTracking.dll not found in plugin folder" -ForegroundColor Red
    exit 1
}

# Copy mod files
Write-Host "Installing mod files..." -ForegroundColor Yellow

Copy-Item $dllSource (Join-Path $pluginsDir "HeadTracking.dll") -Force
Write-Host "  Copied: HeadTracking.dll" -ForegroundColor Gray

if (Test-Path $iniSource) {
    $iniDest = Join-Path $pluginsDir "HeadTracking.ini"
    if (-not (Test-Path $iniDest)) {
        Copy-Item $iniSource $iniDest -Force
        Write-Host "  Copied: HeadTracking.ini" -ForegroundColor Gray
    } else {
        Write-Host "  Skipped: HeadTracking.ini (already exists)" -ForegroundColor Gray
    }
}

Write-Host ""
Write-Host "========================================" -ForegroundColor Green
Write-Host "  Installation Complete!" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Green
Write-Host ""
Write-Host "Installed to:" -ForegroundColor White
Write-Host "  $pluginsDir" -ForegroundColor Cyan
Write-Host ""
Write-Host "Next steps:" -ForegroundColor Yellow
Write-Host "  1. Configure OpenTrack to output UDP to 127.0.0.1:4242" -ForegroundColor Gray
Write-Host "  2. Start OpenTrack and enable tracking" -ForegroundColor Gray
Write-Host "  3. Launch Fallout: New Vegas via xNVSE" -ForegroundColor Gray
Write-Host ""
Write-Host "Controls:" -ForegroundColor Yellow
Write-Host "  Home - Recenter head tracking" -ForegroundColor Gray
Write-Host "  End  - Toggle head tracking on/off" -ForegroundColor Gray
Write-Host ""
