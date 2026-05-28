#!/usr/bin/env pwsh
#Requires -Version 5.1
<#
.SYNOPSIS
    Automated release workflow for Fallout: New Vegas Head Tracking mod.

.DESCRIPTION
    This script:
    1. Updates version in version.h
    2. Builds release and copies DLL to prebuilt/
    3. Generates CHANGELOG from commits
    4. Commits all changes
    5. Creates and pushes an annotated git tag to trigger CI release

.PARAMETER Version
    The version to release (e.g., "1.0.0", "1.2.3")

.EXAMPLE
    pixi run release 1.0.0

.NOTES
    Run via: pixi run release <version>
#>
param(
    [Parameter(Position=0)]
    [string]$Version = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectDir = Split-Path -Parent $scriptDir
$versionHeader = Join-Path $projectDir "src\version.h"

Import-Module (Join-Path $projectDir "cameraunlock-core\powershell\ReleaseWorkflow.psm1") -Force

# Function to get current version from version.h
function Get-CurrentVersion {
    if (-not (Test-Path $versionHeader)) {
        return "0.0.0"
    }

    $content = Get-Content $versionHeader -Raw

    $major = [regex]::Match($content, "VERSION_MAJOR\s*=\s*(\d+)").Groups[1].Value
    $minor = [regex]::Match($content, "VERSION_MINOR\s*=\s*(\d+)").Groups[1].Value
    $patch = [regex]::Match($content, "VERSION_PATCH\s*=\s*(\d+)").Groups[1].Value

    if (-not $major) { $major = "0" }
    if (-not $minor) { $minor = "0" }
    if (-not $patch) { $patch = "0" }

    return "$major.$minor.$patch"
}

# Function to set version in version.h
function Set-Version {
    param([string]$NewVersion)

    $versionParts = $NewVersion -split "\."
    if ($versionParts.Count -ne 3) {
        Write-Host "Error: Version must be in format X.Y.Z" -ForegroundColor Red
        exit 1
    }

    $major = $versionParts[0]
    $minor = $versionParts[1]
    $patch = $versionParts[2]

    if (-not (Test-Path $versionHeader)) {
        Write-Host "Error: version.h not found at $versionHeader" -ForegroundColor Red
        exit 1
    }

    $content = Get-Content $versionHeader -Raw

    $content = $content -replace "(VERSION_MAJOR\s*=\s*)\d+", "`${1}$major"
    $content = $content -replace "(VERSION_MINOR\s*=\s*)\d+", "`${1}$minor"
    $content = $content -replace "(VERSION_PATCH\s*=\s*)\d+", "`${1}$patch"

    Set-Content $versionHeader $content -NoNewline
}

Write-Host "=== Fallout: New Vegas Head Tracking Release ===" -ForegroundColor Cyan
Write-Host ""

$currentVersion = Get-CurrentVersion

# If no version provided, show current and exit
if ([string]::IsNullOrWhiteSpace($Version)) {
    Write-Host "Current version: " -NoNewline -ForegroundColor Yellow
    Write-Host $currentVersion -ForegroundColor White
    Write-Host ""
    Write-Host "Usage: " -NoNewline -ForegroundColor Yellow
    Write-Host "pixi run release <major|minor|patch|nightly|X.Y.Z>" -ForegroundColor White
    Write-Host ""
    Write-Host "Example: " -NoNewline -ForegroundColor Yellow
    Write-Host "pixi run release patch" -ForegroundColor White
    exit 0
}

if ($Version -eq 'nightly') {
    & (Join-Path $PSScriptRoot 'release-nightly.ps1')
    exit $LASTEXITCODE
}

# Resolve major/minor/patch into a concrete version (or accept literal X.Y.Z)
try {
    $Version = Resolve-ReleaseVersion -Argument $Version -CurrentVersion $currentVersion
} catch {
    Write-Host "Error: $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}

$tagName = "v$Version"

# Check if we are on main branch
$currentBranch = git rev-parse --abbrev-ref HEAD
if ($currentBranch -ne "main") {
    Write-Host "Error: Must be on main branch to release (currently on '$currentBranch')" -ForegroundColor Red
    exit 1
}

# Check for uncommitted changes (prebuilt/ is excluded since the release overwrites it)
$status = git status --porcelain -- ':!prebuilt/'
if ($status) {
    Write-Host "Error: Working directory has uncommitted changes" -ForegroundColor Red
    Write-Host $status -ForegroundColor Gray
    Write-Host "Please commit or stash changes before releasing" -ForegroundColor Yellow
    exit 1
}

# Check if tag already exists
$existingTag = git tag -l $tagName
if ($existingTag) {
    Write-Host "Error: Tag '$tagName' already exists" -ForegroundColor Red
    exit 1
}

Write-Host "Current version: $currentVersion" -ForegroundColor Gray
Write-Host "New version:     $Version" -ForegroundColor Green
Write-Host ""

# Step 1: Update version
Write-Host "Updating version to $Version..." -ForegroundColor Cyan
Set-Version $Version

# Step 2: Build and update prebuilt DLL
Write-Host "Building release..." -ForegroundColor Cyan
Push-Location $projectDir
cmake --build build --config Release
if ($LASTEXITCODE -ne 0) {
    Write-Host "Build failed!" -ForegroundColor Red
    Pop-Location
    exit 1
}

$prebuiltDir = Join-Path $projectDir "prebuilt"
if (-not (Test-Path $prebuiltDir)) {
    New-Item -ItemType Directory -Path $prebuiltDir -Force | Out-Null
}
Copy-Item "build/bin/Release/HeadTracking.dll" $prebuiltDir -Force
Write-Host "  Updated prebuilt DLL" -ForegroundColor Gray
Pop-Location

# Step 3: Generate CHANGELOG
Write-Host "Generating CHANGELOG from commits..." -ForegroundColor Cyan
$changelogPath = Join-Path $projectDir "CHANGELOG.md"
$hasExistingTags = git tag -l 2>$null
if (-not $hasExistingTags) {
    # First release - write a basic changelog entry
    $date = Get-Date -Format 'yyyy-MM-dd'
    $firstEntry = "# Changelog`n`n## [$Version] - $date`n`nFirst release.`n"
    Set-Content $changelogPath $firstEntry
    Write-Host "  First release - wrote initial CHANGELOG entry" -ForegroundColor Gray
} else {
    $changelogArgs = @{
        ChangelogPath = $changelogPath
        Version = $Version
        ArtifactPaths = @(
            "src/"
            "cameraunlock-core"
            "scripts/install.cmd"
            "scripts/uninstall.cmd"
            "prebuilt/"
        )
    }
    New-ChangelogFromCommits @changelogArgs
}

# Step 4: Commit
Write-Host "Committing changes..." -ForegroundColor Cyan
git add $versionHeader
git add "$projectDir/prebuilt"
git add $changelogPath
git commit -m "Release v$Version"
if ($LASTEXITCODE -ne 0) {
    Write-Host "Commit failed!" -ForegroundColor Red
    exit 1
}

# Step 5: Create annotated tag
Write-Host "Creating tag $tagName..." -ForegroundColor Cyan
git tag -a $tagName -m "Release $tagName"

# Step 6: Push
Write-Host "Pushing to GitHub..." -ForegroundColor Cyan
git push origin main
git push origin $tagName

Write-Host ""
Write-Host "Release $tagName initiated!" -ForegroundColor Green
Write-Host ""
Write-Host "The GitHub Actions release workflow will now:" -ForegroundColor Yellow
Write-Host "  - Build the release" -ForegroundColor White
Write-Host "  - Create GitHub release with artifacts" -ForegroundColor White
Write-Host ""
Write-Host "Watch progress at:" -ForegroundColor Yellow
Write-Host "  https://github.com/itsloopyo/fallout-new-vegas-headtracking/actions" -ForegroundColor Cyan
