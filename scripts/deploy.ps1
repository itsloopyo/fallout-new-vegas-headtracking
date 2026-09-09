# Deploy the HeadTracking proxy to every install of Fallout: New Vegas
# Usage: scripts/deploy.ps1 [Debug|Release]
#
# The mod loads as dsound.dll next to FalloutNV.exe. The game imports exactly
# one function from dsound (ordinal 11), which the DLL forwards to the system
# copy; being loaded is the point. That works on Steam, GOG, Epic and Game Pass
# alike, so there is one deployment shape and no store gets special treatment.
#
# There is no script extender in this path. Nothing is written to
# Data\NVSE\Plugins and nothing downloads xNVSE.

param(
    [string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectRoot = Split-Path -Parent $scriptDir
Import-Module (Join-Path $scriptDir "FnvInstalls.psm1") -Force

function Write-ColorOutput {
    param(
        [string]$Message,
        [string]$Color = "White"
    )
    Write-Host $Message -ForegroundColor $Color
}

Write-ColorOutput "HeadTracking Deployment (dsound proxy)" "Cyan"
Write-ColorOutput "Configuration: $Configuration" "Gray"
Write-ColorOutput ""

$gamePaths = Get-FnvInstalls

$buildDir = Join-Path $projectRoot "build\bin\$Configuration"
$dllSource = Join-Path $buildDir "HeadTracking.dll"
$iniSource = Join-Path $projectRoot "config\HeadTracking.ini"

if (-not (Test-Path $dllSource)) {
    Write-ColorOutput "ERROR: HeadTracking.dll not found at $dllSource" "Red"
    Write-ColorOutput "Run 'pixi run build-release' first to compile it." "Yellow"
    exit 1
}
if (-not (Test-Path $iniSource)) {
    Write-ColorOutput "ERROR: HeadTracking.ini not found at $iniSource" "Red"
    exit 1
}

foreach ($gamePath in $gamePaths) {
    Write-ColorOutput ""
    Write-ColorOutput "Deploying to: $gamePath" "Cyan"

    $dllDest = Join-Path $gamePath "dsound.dll"
    $iniDest = Join-Path $gamePath "HeadTracking.ini"

    Copy-Item -Path $dllSource -Destination $dllDest -Force
    Write-ColorOutput "  dsound.dll       (the mod)" "Gray"

    # Seeded only when absent, so a redeploy keeps whatever the user tuned.
    if (Test-Path $iniDest) {
        Write-ColorOutput "  HeadTracking.ini (kept, already present)" "Gray"
    } else {
        Copy-Item -Path $iniSource -Destination $iniDest -Force
        Write-ColorOutput "  HeadTracking.ini (seeded)" "Gray"
    }

    # An older build of this mod deployed as an xNVSE plugin. Left in place it
    # would load a second copy of the mod alongside the proxy whenever the user
    # launched through nvse_loader.exe: two module instances, two singletons,
    # and two binds on UDP 4242.
    $legacyPlugin = Join-Path $gamePath "Data\NVSE\Plugins\HeadTracking.dll"
    if (Test-Path $legacyPlugin) {
        Remove-Item $legacyPlugin -Force
        Write-ColorOutput "  removed the old NVSE plugin copy (superseded by the proxy)" "Yellow"
    }
}

Write-ColorOutput ""
Write-ColorOutput "Deployment successful ($($gamePaths.Count) install(s))." "Green"
Write-ColorOutput "Launch the game normally - Steam, the Xbox app, or the exe." "Yellow"
Write-ColorOutput "nvse_loader.exe is no longer used by this mod." "Gray"
