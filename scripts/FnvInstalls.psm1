#!/usr/bin/env pwsh
#Requires -Version 5.1
# Install enumeration for the dev-facing pixi tasks.
#
# Find-GamePath returns the highest-priority hit and stops, which is right for
# the launcher (it installs into one copy and means it). A dev task wants the
# opposite: owning New Vegas on Steam and GOG at once is ordinary, and a deploy
# that picks one leaves the other running whatever build was last dropped into
# it. The symptom is a fix that "did not work" because the copy that got
# launched was never updated.

Set-StrictMode -Version Latest

$Script:GameId = 'fallout-new-vegas'
$Script:GameDisplayName = 'Fallout: New Vegas'

Import-Module (Join-Path $PSScriptRoot '..\cameraunlock-core\powershell\GamePathDetection.psm1')

<#
.SYNOPSIS
    Every install of Fallout: New Vegas on this machine, as directories holding
    FalloutNV.exe.
.DESCRIPTION
    Steam, GOG and Epic from games.json, plus every Game Pass copy. The mod
    loads through a dsound.dll proxy next to the exe, which works on all of
    them, so there is no store this deploys differently to and none it skips.

    Game Pass is not in games.json because that file is where the launcher
    looks for deploy targets and the Game Pass build has no camera offsets yet;
    it is found here instead, by reading the Xbox app's own install roots.
.OUTPUTS
    System.String[]
#>
function Get-FnvInstalls {
    [CmdletBinding()]
    [OutputType([string[]])]
    param()

    $paths = @(Find-AllGamePaths -GameId $Script:GameId) + @(Get-FnvGamePassInstalls)
    if ($paths.Count -eq 0) {
        $config = Get-GameConfig -GameId $Script:GameId
        Write-GameNotFoundError `
            -GameName $Script:GameDisplayName `
            -EnvVar $config.EnvVar `
            -SteamFolder $config.SteamFolder
        throw "Game not found: $($Script:GameDisplayName)"
    }

    if ($paths.Count -eq 1) {
        Write-Host "Game found at: $($paths[0])" -ForegroundColor Green
    } else {
        Write-Host "Found $($paths.Count) installations of $($Script:GameDisplayName):" -ForegroundColor Cyan
        $paths | ForEach-Object { Write-Host "  $_" -ForegroundColor Cyan }
    }
    return $paths
}

<#
.SYNOPSIS
    Every root the Xbox app installs games into on this machine.
.DESCRIPTION
    The Xbox app drops a `.GamingRoot` file at the root of each drive it has
    been told to install into: eight bytes of header, then the folder name as
    UTF-16LE. Read rather than assumed, since recording that name is the file's
    whole purpose.
.OUTPUTS
    System.String[] - e.g. @('C:\XboxGames', 'D:\XboxGames')
#>
function Get-XboxInstallRoots {
    [CmdletBinding()]
    [OutputType([string[]])]
    param()

    $roots = [System.Collections.Generic.List[string]]::new()
    foreach ($drive in [System.IO.DriveInfo]::GetDrives()) {
        if (-not $drive.IsReady) { continue }
        $marker = Join-Path $drive.Name '.GamingRoot'
        if (-not (Test-Path -LiteralPath $marker -PathType Leaf)) { continue }
        $bytes = [System.IO.File]::ReadAllBytes($marker)
        if ($bytes.Length -le 8) { continue }
        $folder = [System.Text.Encoding]::Unicode.GetString($bytes, 8, $bytes.Length - 8).TrimEnd([char]0)
        if (-not $folder) { continue }
        $root = Join-Path $drive.Name $folder
        if (Test-Path -LiteralPath $root -PathType Container) { $roots.Add($root) }
    }
    return $roots.ToArray()
}

<#
.SYNOPSIS
    Game Pass copies of Fallout: New Vegas on this machine.
.DESCRIPTION
    One path per language the package shipped, each being the directory that
    holds that language's FalloutNV.exe. All of them, not just English: which
    one the player launches follows their Xbox language setting, and the proxy
    has to be next to the exe that actually runs.
.OUTPUTS
    System.String[]
#>
function Get-FnvGamePassInstalls {
    [CmdletBinding()]
    [OutputType([string[]])]
    param()

    $found = [System.Collections.Generic.List[string]]::new()
    foreach ($root in Get-XboxInstallRoots) {
        $titles = @(Get-ChildItem -LiteralPath $root -Directory -Filter 'Fallout*New Vegas*' -ErrorAction SilentlyContinue)
        foreach ($title in $titles) {
            $content = Join-Path $title.FullName 'Content'
            if (-not (Test-Path -LiteralPath $content -PathType Container)) { continue }
            foreach ($lang in @(Get-ChildItem -LiteralPath $content -Directory -ErrorAction SilentlyContinue)) {
                if (Test-Path -LiteralPath (Join-Path $lang.FullName 'FalloutNV.exe') -PathType Leaf) {
                    $found.Add($lang.FullName)
                }
            }
        }
    }
    return $found.ToArray()
}

Export-ModuleMember -Function @(
    'Get-FnvInstalls',
    'Get-XboxInstallRoots',
    'Get-FnvGamePassInstalls'
)
