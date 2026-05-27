#!/usr/bin/env pwsh
#Requires -Version 5.1
# Refresh the pinned xNVSE (New Vegas Script Extender, community fork) version
# that install.cmd downloads at install time. Manual: the dev runs this when
# they want a fresh upstream bump, reviews the diff, and commits. CI never runs
# it; the build does not depend on it.
#
# xNVSE ships with NO upstream license, so we cannot redistribute its binary in
# our release ZIP. This mod therefore uses the doctrine's documented vendoring
# exception: instead of committing the loader zip, we pin an exact version +
# SHA-256 in scripts/install.cmd and fetch it from GitHub at install time. This
# script downloads the latest legacy-version asset only to read its URL + hash,
# rewrites the pin in install.cmd, refreshes vendor/xnvse/README.md, and deletes
# the downloaded binary. No loader binary is ever committed.
#
# We call Invoke-FetchLatestLoader (the lower-level helper) directly rather than
# Refresh-VendoredLoader, because the latter insists on committing the zip +
# LICENSE - exactly what the no-license exception forbids.
#
# xNVSE publishes two assets per release: nvse_*.7z (modern) and
# xnvse_*_windows_7_legacy_version.zip. We pin the legacy .zip because it's what
# install.cmd's tar can extract; the modern .7z would need a 7-Zip dependency.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$ProgressPreference    = 'SilentlyContinue'

$scriptDir  = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectDir = Split-Path -Parent $scriptDir

$module = Join-Path $projectDir 'cameraunlock-core/powershell/ModLoaderSetup.psm1'
if (-not (Test-Path $module)) {
    throw "ModLoaderSetup.psm1 not found at $module. Run 'pixi run sync' to update the cameraunlock-core submodule."
}
Import-Module $module -Force

$installCmd = Join-Path $scriptDir 'install.cmd'
$readme     = Join-Path $projectDir 'vendor/xnvse/README.md'

# Fetch the latest legacy-version asset to a throwaway temp file purely to read
# its download URL and SHA-256. We never keep the binary.
$tmp = Join-Path $env:TEMP ("xnvse-pin-" + [IO.Path]::GetRandomFileName() + ".zip")
try {
    Write-Host "  Fetching latest xNVSE legacy-version asset to read its URL + hash..." -ForegroundColor Cyan
    $meta = Invoke-FetchLatestLoader `
        -OutputPath $tmp `
        -Owner 'xNVSE' -Repo 'NVSE' `
        -AssetPattern '_windows_7_legacy_version\.zip$'
} finally {
    if (Test-Path $tmp) { Remove-Item $tmp -Force }
}

$version = $meta.Tag
$url     = $meta.AssetUrl
$sha     = $meta.Sha256

Write-Host "    version=$version" -ForegroundColor DarkGray
Write-Host "    sha256=$sha"      -ForegroundColor DarkGray

# Rewrite the pin in install.cmd's CONFIG BLOCK. The three lines are uniquely
# formatted so a line-anchored replace is safe.
$cmd = Get-Content $installCmd -Raw
$cmd = $cmd -replace '(?m)^set "XNVSE_VERSION=.*"$', ('set "XNVSE_VERSION=' + $version + '"')
$cmd = $cmd -replace '(?m)^set "XNVSE_URL=.*"$',     ('set "XNVSE_URL=' + $url + '"')
$cmd = $cmd -replace '(?m)^set "XNVSE_SHA256=.*"$',  ('set "XNVSE_SHA256=' + $sha + '"')
# install.cmd must stay CRLF or it silently fails on Windows.
$cmd = $cmd -replace "`r`n", "`n" -replace "`n", "`r`n"
[IO.File]::WriteAllText($installCmd, $cmd)

# Refresh the vendor pin record.
$fetchedAt = (Get-Date).ToString('o')
$readmeBody = @"
# xnvse (pinned, NOT vendored)

xNVSE (New Vegas Script Extender, community fork) is the script-extender loader
Fallout: NV head tracking depends on. It ships with **no upstream license**, so
we do not redistribute its binary - committing or bundling it would be
redistribution we have no license for.

Instead this mod uses the doctrine's vendoring exception: ``scripts/install.cmd``
pins an exact version + SHA-256 and downloads xNVSE from the official GitHub
release at install time, verifying the hash before trusting it. Bump the pin
with ``pixi run update-deps`` (manual; commit the result). The release ZIP
carries no loader binary.

## Pinned snapshot

- Asset pattern: ``_windows_7_legacy_version.zip``
- Version: ``$version``
- Download URL: $url
- SHA-256: ``$sha``
- Refreshed at: $fetchedAt

The download URL and SHA-256 above are mirrored into the CONFIG BLOCK of
``scripts/install.cmd`` (``XNVSE_URL`` / ``XNVSE_SHA256``); this file is the
human-readable record, install.cmd is what runs.
"@
$readmeBody = $readmeBody -replace "`r`n", "`n"
[IO.File]::WriteAllText($readme, $readmeBody)

Write-Host ""
Write-Host "Pin refreshed in scripts/install.cmd and vendor/xnvse/README.md. Review and commit." -ForegroundColor Green
