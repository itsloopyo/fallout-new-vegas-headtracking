#Requires -Version 5.1
# The proxy must land beside FalloutNV.exe. A Data-only mod-manager archive
# cannot load it, so this package produces an installer ZIP only.
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
Copy-Item -LiteralPath (Join-Path $projectRoot 'build/bin/Release/HeadTracking.dll') -Destination (Join-Path $stagingDir 'plugins')
Copy-Item -LiteralPath (Join-Path $projectRoot 'config/HeadTracking.ini') -Destination (Join-Path $stagingDir 'plugins')
$manifest = Get-Content (Join-Path $projectRoot 'launcher-manifest.json') -Raw | ConvertFrom-Json
$manifest.mod_info.version = $version
$seed = [Convert]::ToBase64String([IO.File]::ReadAllBytes((Join-Path $projectRoot 'config/HeadTracking.ini')))
$manifest.loader.seed[0].content_b64 = $seed
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
$zip = Join-Path $releaseDir "FalloutNVHeadTracking-v$version-installer.zip"
Compress-Archive -Path (Join-Path $stagingDir '*') -DestinationPath $zip -Force
$resolvedStaging = (Resolve-Path -LiteralPath $stagingDir).Path
$resolvedRelease = (Resolve-Path -LiteralPath $releaseDir).Path
if (-not $resolvedStaging.StartsWith($resolvedRelease + '\', [StringComparison]::OrdinalIgnoreCase)) {
    throw "Staging path escaped release directory: $resolvedStaging"
}
Remove-Item -LiteralPath $resolvedStaging -Recurse -Force
Write-Output $zip
