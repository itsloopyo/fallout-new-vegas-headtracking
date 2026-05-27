# Deploy HeadTracking NVSE plugin to Fallout: New Vegas
# Usage: scripts/deploy.ps1 [Debug|Release]

param(
    [string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"

# Import shared game detection module
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectRoot = Split-Path -Parent $scriptDir
$modulePath = Join-Path $projectRoot "cameraunlock-core\powershell\GamePathDetection.psm1"
Import-Module $modulePath -Force

$gameId = 'fallout-new-vegas'
$config = Get-GameConfig -GameId $gameId

function Write-ColorOutput {
    param(
        [string]$Message,
        [string]$Color = "White"
    )
    Write-Host $Message -ForegroundColor $Color
}

function Install-NVSE {
    param([string]$GamePath)

    Write-ColorOutput "xNVSE not found - installing automatically..." "Yellow"
    Write-ColorOutput ""

    # Fetch latest release from GitHub API
    Write-ColorOutput "  Fetching latest xNVSE release..." "Gray"
    $releaseUrl = "https://api.github.com/repos/xNVSE/NVSE/releases/latest"

    try {
        $release = Invoke-RestMethod -Uri $releaseUrl -Headers @{ "User-Agent" = "HeadTracking" }
    } catch {
        Write-ColorOutput "ERROR: Failed to fetch xNVSE release info: $($_.Exception.Message)" "Red"
        return $false
    }

    $nvseVersion = $release.tag_name -replace '^v', ''

    # Prefer 7z (smaller, flat structure) - requires 7-Zip installed
    $asset = $release.assets | Where-Object { $_.name -match "^nvse_.*\.7z$" } | Select-Object -First 1
    $use7z = $true

    if (-not $asset) {
        Write-ColorOutput "ERROR: Could not find xNVSE .7z release asset" "Red"
        Write-ColorOutput "Available assets:" "Yellow"
        $release.assets | ForEach-Object { Write-ColorOutput "  - $($_.name)" "Gray" }
        return $false
    }

    # Verify 7-Zip is available
    $7zPath = "C:\Program Files\7-Zip\7z.exe"
    if (-not (Test-Path $7zPath)) {
        Write-ColorOutput "ERROR: 7-Zip not found at $7zPath" "Red"
        Write-ColorOutput "Please install 7-Zip from https://www.7-zip.org/" "Yellow"
        return $false
    }

    Write-ColorOutput "  Downloading: $($asset.name)" "Gray"

    $tempArchive = Join-Path $env:TEMP "xNVSE.7z"
    try {
        $ProgressPreference = "SilentlyContinue"
        Invoke-WebRequest -Uri $asset.browser_download_url -OutFile $tempArchive -UseBasicParsing
    } catch {
        Write-ColorOutput "ERROR: Failed to download xNVSE: $($_.Exception.Message)" "Red"
        return $false
    }

    Write-ColorOutput "  Extracting to game directory..." "Gray"

    # Extract to temp first, then copy (avoids permission issues with Program Files)
    $tempExtract = Join-Path $env:TEMP "xNVSE_extract"
    if (Test-Path $tempExtract) { Remove-Item $tempExtract -Recurse -Force }
    New-Item -ItemType Directory -Path $tempExtract -Force | Out-Null

    $extractResult = & $7zPath x $tempArchive -o"$tempExtract" -y 2>&1
    if ($LASTEXITCODE -ne 0) {
        Write-ColorOutput "ERROR: Failed to extract xNVSE: $extractResult" "Red"
        return $false
    }

    # Copy all extracted files to game directory
    Get-ChildItem -Path $tempExtract -File | ForEach-Object {
        Copy-Item -Path $_.FullName -Destination (Join-Path $GamePath $_.Name) -Force
        Write-ColorOutput "    $($_.Name)" "Gray"
    }

    # Copy Data folder if exists
    $dataFolder = Join-Path $tempExtract "Data"
    if (Test-Path $dataFolder) {
        Copy-Item -Path $dataFolder -Destination $GamePath -Recurse -Force
        Write-ColorOutput "    Data\" "Gray"
    }

    # Clean up temp files
    Remove-Item $tempArchive -Force -ErrorAction SilentlyContinue
    Remove-Item $tempExtract -Recurse -Force -ErrorAction SilentlyContinue

    # Verify installation
    $loaderPath = Join-Path $GamePath "nvse_loader.exe"
    if (-not (Test-Path $loaderPath)) {
        Write-ColorOutput "ERROR: nvse_loader.exe not found after extraction" "Red"
        return $false
    }

    Write-ColorOutput ""
    Write-ColorOutput "xNVSE v$nvseVersion installed successfully!" "Green"
    Write-ColorOutput ""

    return $true
}

function Validate-NVSE {
    param([string]$GamePath)

    $nvsePath = Join-Path $GamePath "nvse_loader.exe"
    if (-not (Test-Path $nvsePath)) {
        return Install-NVSE -GamePath $GamePath
    }
    return $true
}

function Validate-JIP {
    param([string]$GamePath)

    $jipPath = Join-Path $GamePath "Data\NVSE\Plugins\jip_nvse.dll"
    if (-not (Test-Path $jipPath)) {
        Write-ColorOutput "WARNING: JIP LN NVSE not found (optional but recommended)." "Yellow"
        Write-ColorOutput "Install from: https://www.nexusmods.com/newvegas/mods/58277" "Cyan"
    }
    return $true
}

function Validate-NVTF {
    param([string]$GamePath)

    $pluginsDir = Join-Path $GamePath "Data\NVSE\Plugins"
    $nvtfPath = Join-Path $pluginsDir "nvtf.dll"
    if (-not (Test-Path $nvtfPath)) {
        Write-ColorOutput ""
        Write-ColorOutput "WARNING: NVTF (New Vegas Tick Fix) not found!" "Yellow"
        Write-ColorOutput "Without NVTF, camera movement will stutter due to engine timing issues." "Yellow"
        Write-ColorOutput ""
        Write-ColorOutput "Download from: https://www.nexusmods.com/newvegas/mods/66537" "Cyan"
        Write-ColorOutput "  1. Download 'NVTF' (main file) and 'NVTF - INI' files" "Gray"
        Write-ColorOutput "  2. Extract both to:" "Gray"
        Write-ColorOutput "     $pluginsDir" "White"
        Write-ColorOutput ""
    }
    return $true
}

# Main deployment logic
Write-ColorOutput "HeadTracking NVSE Plugin Deployment" "Cyan"
Write-ColorOutput "Configuration: $Configuration" "Gray"
Write-ColorOutput ""

# Find game path
$gamePath = Find-GamePath -GameId $gameId
if (-not $gamePath) {
    Write-GameNotFoundError -GameName 'Fallout: New Vegas' -EnvVar $config.EnvVar -SteamFolder $config.SteamFolder
    exit 1
}

Write-ColorOutput "Game found at: $gamePath" "Green"

# Validate prerequisites
if (-not (Validate-NVSE $gamePath)) {
    exit 1
}
Validate-JIP $gamePath | Out-Null
Validate-NVTF $gamePath | Out-Null

# Source paths - get project root from script location
# $PSScriptRoot is the 'scripts' folder, so go up one level
$projectRoot = Split-Path -Parent $PSScriptRoot
if (-not $projectRoot -or -not (Test-Path (Join-Path $projectRoot "pixi.toml"))) {
    $projectRoot = (Get-Location).Path
}

$buildDir = Join-Path $projectRoot "build\bin\$Configuration"
$dllSource = Join-Path $buildDir "HeadTracking.dll"
$iniSource = Join-Path $projectRoot "config\HeadTracking.ini"

# Check if DLL exists
if (-not (Test-Path $dllSource)) {
    # Try alternative paths
    $altPaths = @(
        (Join-Path $projectRoot "build\$Configuration\HeadTracking.dll"),
        (Join-Path $projectRoot "build\bin\HeadTracking.dll")
    )
    foreach ($alt in $altPaths) {
        if (Test-Path $alt) {
            $dllSource = $alt
            break
        }
    }
}

if (-not (Test-Path $dllSource)) {
    Write-ColorOutput "ERROR: HeadTracking.dll not found at $dllSource" "Red"
    Write-ColorOutput "Run 'pixi run build' first to compile the plugin." "Yellow"
    exit 1
}

if (-not (Test-Path $iniSource)) {
    Write-ColorOutput "ERROR: HeadTracking.ini not found at $iniSource" "Red"
    exit 1
}

# Destination directory
$pluginsDir = Join-Path $gamePath "Data\NVSE\Plugins"
New-Item -ItemType Directory -Force -Path $pluginsDir | Out-Null

# Copy files
$dllDest = Join-Path $pluginsDir "HeadTracking.dll"
$iniDest = Join-Path $pluginsDir "HeadTracking.ini"

Write-ColorOutput "Copying HeadTracking.dll -> $dllDest" "Gray"
Copy-Item -Path $dllSource -Destination $dllDest -Force

Write-ColorOutput "Copying HeadTracking.ini -> $iniDest" "Gray"
Copy-Item -Path $iniSource -Destination $iniDest -Force

Write-ColorOutput ""
Write-ColorOutput "Deployment successful!" "Green"
Write-ColorOutput "  Plugin directory: $pluginsDir" "Cyan"
Write-ColorOutput "  HeadTracking.dll - OK" "Green"
Write-ColorOutput "  HeadTracking.ini - OK" "Green"
Write-ColorOutput ""
Write-ColorOutput "Launch the game with nvse_loader.exe to use head tracking." "Yellow"
