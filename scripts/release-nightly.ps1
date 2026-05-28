[CmdletBinding()]
param([switch]$AllowDirty)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$ProjectRoot = Resolve-Path (Join-Path $PSScriptRoot '..')
Import-Module (Join-Path $ProjectRoot 'cameraunlock-core\powershell\NightlyRelease.psm1') -Force

$versionHeader = Join-Path $ProjectRoot 'src\version.h'
$headerContent = Get-Content $versionHeader -Raw

$major = [regex]::Match($headerContent, 'VERSION_MAJOR\s*=\s*(\d+)').Groups[1].Value
$minor = [regex]::Match($headerContent, 'VERSION_MINOR\s*=\s*(\d+)').Groups[1].Value
$patch = [regex]::Match($headerContent, 'VERSION_PATCH\s*=\s*(\d+)').Groups[1].Value

if (-not $major -or -not $minor -or -not $patch) {
    throw "Failed to extract VERSION_MAJOR/MINOR/PATCH from $versionHeader"
}

$version = "$major.$minor.$patch"

Publish-NightlyBuild `
    -ModId 'fallout-new-vegas' `
    -ModName 'FalloutNVHeadTracking' `
    -Version $version `
    -ProjectRoot $ProjectRoot `
    -AllowDirty:$AllowDirty
