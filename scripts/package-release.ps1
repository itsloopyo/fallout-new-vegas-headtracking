#Requires -Version 5.1
# The proxy must land beside FalloutNV.exe. A Data-only mod-manager archive
# cannot load it, so this package produces an installer ZIP only.
#
# Lopari installs from launcher-manifest.json and needs nothing else here.
# install.cmd / uninstall.cmd still ship for users installing by hand from the
# GitHub release: the payload is a DLL that has to land under a system DLL's
# name, which is not something to leave a player to do with Explorer.
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$releaseDir = Join-Path $projectRoot 'release'
$header = Get-Content (Join-Path $projectRoot 'src/version.h') -Raw
$parts = foreach ($part in @('MAJOR', 'MINOR', 'PATCH')) {
    $match = [regex]::Match($header, "VERSION_$part\s*=\s*(\d+)")
    if (-not $match.Success) { throw "Missing VERSION_$part in version.h" }
    $match.Groups[1].Value
}
$version = $parts -join '.'
$stagingDir = Join-Path $releaseDir ('staging-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path (Join-Path $stagingDir 'plugins') -Force | Out-Null
# Named for the export the game resolves, not for the project. install.cmd's
# shim body copies MOD_DLLS by name, and the manifest deploys plugins/dsound.dll
# to the same place, so both routes deliver one identical file.
Copy-Item -LiteralPath (Join-Path $projectRoot 'build/bin/Release/HeadTracking.dll') -Destination (Join-Path $stagingDir 'plugins/dsound.dll')
$manifest = Get-Content (Join-Path $projectRoot 'launcher-manifest.json') -Raw | ConvertFrom-Json
$manifest.mod_info.version = $version
foreach ($file in $manifest.files) {
    if (-not (Test-Path -LiteralPath (Join-Path $stagingDir $file.source) -PathType Leaf)) {
        throw "Manifest source missing: $($file.source)"
    }
    if ([IO.Path]::IsPathRooted($file.target) -or $file.target -match '(^|[/\\])\.\.([/\\]|$)') {
        throw "Invalid manifest target: $($file.target)"
    }
}
$manifest | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $stagingDir 'launcher-manifest.json') -Encoding UTF8
foreach ($doc in @('README.md', 'CHANGELOG.md', 'LICENSE', 'THIRD-PARTY-NOTICES.md')) {
    Copy-Item -LiteralPath (Join-Path $projectRoot $doc) -Destination $stagingDir
}
foreach ($script in @('install.cmd', 'uninstall.cmd')) {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot $script) -Destination $stagingDir
}
# install.cmd is a thin wrapper; shared/ carries the body it calls, find-game.ps1
# and games.json. Without it a hand-installer gets "installer ZIP is corrupt".
Import-Module (Join-Path $projectRoot 'cameraunlock-core/powershell/ReleaseWorkflow.psm1') -Force
Copy-SharedBundle -StagingDir $stagingDir
$zip = Join-Path $releaseDir "FalloutNVHeadTracking-v$version-installer.zip"
Compress-Archive -Path (Join-Path $stagingDir '*') -DestinationPath $zip -Force
$resolvedStaging = (Resolve-Path -LiteralPath $stagingDir).Path
$resolvedRelease = (Resolve-Path -LiteralPath $releaseDir).Path
if (-not $resolvedStaging.StartsWith($resolvedRelease + '\', [StringComparison]::OrdinalIgnoreCase)) {
    throw "Staging path escaped release directory: $resolvedStaging"
}
Remove-Item -LiteralPath $resolvedStaging -Recurse -Force
Write-Output $zip
