#!/usr/bin/env pwsh
#Requires -Version 5.1
Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$ProgressPreference = 'SilentlyContinue'

<#
.SYNOPSIS
    Creates a release ZIP archive for distribution.

.DESCRIPTION
    This script reads the version from version.h and produces both
    distribution ZIPs in release/. xNVSE has no upstream license, so it is
    NOT bundled - install.cmd downloads it from the pinned URL at install
    time (see scripts/install.cmd CONFIG BLOCK and vendor/xnvse/README.md).

    Installer ZIP (FalloutNVHeadTracking-v<version>-installer.zip):
    - install.cmd, uninstall.cmd (root)
    - plugins/HeadTracking.dll, plugins/HeadTracking.ini
    - shared/ (find-game.ps1 + games.json detection bundle)
    - README.md, CHANGELOG.md, LICENSE

    Nexus ZIP (FalloutNVHeadTracking-v<version>-nexus.zip):
    - Data/NVSE/Plugins/HeadTracking.dll + HeadTracking.ini only
      (no loader, no scripts, no docs)

.NOTES
    Run via: pixi run package
#>

function Get-VersionFromHeader {
    $versionHeader = "src/version.h"
    if (-not (Test-Path $versionHeader)) {
        Write-Host "ERROR: version.h not found" -ForegroundColor Red
        exit 1
    }

    $content = Get-Content $versionHeader -Raw

    $major = [regex]::Match($content, 'VERSION_MAJOR\s*=\s*(\d+)').Groups[1].Value
    $minor = [regex]::Match($content, 'VERSION_MINOR\s*=\s*(\d+)').Groups[1].Value
    $patch = [regex]::Match($content, 'VERSION_PATCH\s*=\s*(\d+)').Groups[1].Value

    if (-not $major -or -not $minor -or -not $patch) {
        Write-Host "ERROR: Could not parse version from version.h" -ForegroundColor Red
        exit 1
    }

    return "$major.$minor.$patch"
}

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectRoot = Split-Path -Parent $scriptDir
$releaseDir = Join-Path $projectRoot "release"

Import-Module (Join-Path $projectRoot "cameraunlock-core\powershell\ReleaseWorkflow.psm1") -Force

Write-Host "=== FalloutNVHeadTracking - Package Release ===" -ForegroundColor Magenta
Write-Host ""

# Get version
$version = Get-VersionFromHeader
Write-Host "Version: $version" -ForegroundColor Cyan
Write-Host ""

# Verify build output exists
$dllPath = "build/bin/Release/HeadTracking.dll"
if (-not (Test-Path $dllPath)) {
    Write-Host "ERROR: Build output not found at: $dllPath" -ForegroundColor Red
    Write-Host "Run 'pixi run build-release' first." -ForegroundColor Yellow
    exit 1
}

$iniPath = "config/HeadTracking.ini"
if (-not (Test-Path $iniPath)) {
    Write-Host "ERROR: Config not found: $iniPath" -ForegroundColor Red
    exit 1
}

# Create release directory
if (-not (Test-Path $releaseDir)) {
    New-Item -ItemType Directory -Path $releaseDir -Force | Out-Null
}

# Create staging directory
$stagingDir = Join-Path $releaseDir "staging"
if (Test-Path $stagingDir) {
    Remove-Item -Recurse -Force $stagingDir
}
New-Item -ItemType Directory -Path $stagingDir -Force | Out-Null

Write-Host "Staging release files..." -ForegroundColor Cyan

# Copy install/uninstall scripts to root
foreach ($script in @("install.cmd", "uninstall.cmd")) {
    $scriptPath = Join-Path $scriptDir $script
    if (-not (Test-Path $scriptPath)) {
        Write-Host "ERROR: $script not found at $scriptPath" -ForegroundColor Red
        exit 1
    }
    Copy-Item $scriptPath -Destination $stagingDir -Force
    Write-Host "  $script" -ForegroundColor Green
}

# Copy mod files to plugins subfolder
$pluginsDestDir = Join-Path $stagingDir "plugins"
New-Item -ItemType Directory -Path $pluginsDestDir -Force | Out-Null

Copy-Item $dllPath -Destination $pluginsDestDir -Force
Write-Host "  plugins/HeadTracking.dll" -ForegroundColor Green

Copy-Item $iniPath -Destination $pluginsDestDir -Force
Write-Host "  plugins/HeadTracking.ini" -ForegroundColor Green

# xNVSE is NOT bundled (no upstream license to redistribute). install.cmd
# downloads it from the pinned URL and verifies the SHA-256 at install time.

# Bundle the shared detection bundle (find-game.ps1 + games.json) for install.cmd's shim.
Copy-SharedBundle -StagingDir $stagingDir -CoreRoot (Join-Path $projectRoot 'cameraunlock-core')

# Copy documentation
$docFiles = @("README.md", "CHANGELOG.md", "LICENSE")
foreach ($doc in $docFiles) {
    $docPath = Join-Path $projectRoot $doc
    if (Test-Path $docPath) {
        Copy-Item $docPath -Destination $stagingDir -Force
        Write-Host "  $doc" -ForegroundColor Green
    }
}

Write-Host ""

# Installer ZIP (GitHub Releases): scripts + plugins + shared shim + docs.
$installerZip = Join-Path $releaseDir "FalloutNVHeadTracking-v$version-installer.zip"
if (Test-Path $installerZip) {
    Remove-Item $installerZip -Force
}

Write-Host "Creating installer ZIP..." -ForegroundColor Cyan
Push-Location $stagingDir
try {
    Compress-Archive -Path ".\*" -DestinationPath $installerZip -Force
} finally {
    Pop-Location
}
Remove-Item -Recurse -Force $stagingDir

# Nexus ZIP (extract-to-game-folder): only the deploy subtree, no loader,
# no scripts, no docs. Users on Nexus manage their own xNVSE.
$nexusStaging = Join-Path $releaseDir "staging-nexus"
if (Test-Path $nexusStaging) {
    Remove-Item -Recurse -Force $nexusStaging
}
$nexusPluginsDir = Join-Path $nexusStaging "Data\NVSE\Plugins"
New-Item -ItemType Directory -Path $nexusPluginsDir -Force | Out-Null
Copy-Item $dllPath -Destination $nexusPluginsDir -Force
Copy-Item $iniPath -Destination $nexusPluginsDir -Force

$nexusZip = Join-Path $releaseDir "FalloutNVHeadTracking-v$version-nexus.zip"
if (Test-Path $nexusZip) {
    Remove-Item $nexusZip -Force
}

Write-Host "Creating nexus ZIP..." -ForegroundColor Cyan
Push-Location $nexusStaging
try {
    Compress-Archive -Path ".\*" -DestinationPath $nexusZip -Force
} finally {
    Pop-Location
}
Remove-Item -Recurse -Force $nexusStaging

Write-Host ""
Write-Host "=== Package Complete ===" -ForegroundColor Magenta
Write-Host ""
foreach ($z in @($installerZip, $nexusZip)) {
    $kb = (Get-Item $z).Length / 1KB
    Write-Host ("  {0}  ({1:N1} KB)" -f $z, $kb) -ForegroundColor Green
}

# Output zip paths for CI capture
Write-Output $installerZip
Write-Output $nexusZip
