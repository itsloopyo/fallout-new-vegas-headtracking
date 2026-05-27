# Validate release readiness for HeadTracking NVSE plugin
# Checks version consistency, changelog, and build status

$ErrorActionPreference = "Stop"

function Write-ColorOutput {
    param(
        [string]$Message,
        [string]$Color = "White"
    )
    Write-Host $Message -ForegroundColor $Color
}

function Get-VersionFromHeader {
    $versionHeader = "src/version.h"
    if (-not (Test-Path $versionHeader)) {
        Write-ColorOutput "ERROR: version.h not found" "Red"
        exit 1
    }

    $content = Get-Content $versionHeader -Raw

    $major = [regex]::Match($content, 'VERSION_MAJOR\s*=\s*(\d+)').Groups[1].Value
    $minor = [regex]::Match($content, 'VERSION_MINOR\s*=\s*(\d+)').Groups[1].Value
    $patch = [regex]::Match($content, 'VERSION_PATCH\s*=\s*(\d+)').Groups[1].Value

    if (-not $major -or -not $minor -or -not $patch) {
        Write-ColorOutput "ERROR: Could not parse version from version.h" "Red"
        exit 1
    }

    return "$major.$minor.$patch"
}

Write-ColorOutput "HeadTracking Release Validation" "Cyan"
Write-ColorOutput "===============================" "Cyan"
Write-ColorOutput ""

$allPassed = $true

# Check 1: Get version from header
$version = Get-VersionFromHeader
Write-ColorOutput "Version from version.h: $version" "Green"

# Check 2: Verify CHANGELOG entry exists
Write-ColorOutput ""
Write-ColorOutput "Checking CHANGELOG.md..." "Gray"

if (-not (Test-Path "CHANGELOG.md")) {
    Write-ColorOutput "WARNING: CHANGELOG.md not found" "Yellow"
    $allPassed = $false
} else {
    $changelog = Get-Content "CHANGELOG.md" -Raw
    $escapedVersion = [regex]::Escape($version)

    if ($changelog -notmatch "\[?$escapedVersion\]?") {
        Write-ColorOutput "WARNING: CHANGELOG.md missing entry for version $version" "Yellow"
        Write-ColorOutput "Add a changelog entry with heading: ## [$version] - $(Get-Date -Format 'yyyy-MM-dd')" "Gray"
        $allPassed = $false
    } else {
        Write-ColorOutput "CHANGELOG.md contains entry for v$version" "Green"
    }
}

# Check 3: Verify README exists
Write-ColorOutput ""
Write-ColorOutput "Checking README.md..." "Gray"

if (-not (Test-Path "README.md")) {
    Write-ColorOutput "ERROR: README.md not found" "Red"
    $allPassed = $false
} else {
    Write-ColorOutput "README.md exists" "Green"
}

# Check 4: Verify config file exists
Write-ColorOutput ""
Write-ColorOutput "Checking config/HeadTracking.ini..." "Gray"

if (-not (Test-Path "config/HeadTracking.ini")) {
    Write-ColorOutput "ERROR: config/HeadTracking.ini not found" "Red"
    $allPassed = $false
} else {
    Write-ColorOutput "config/HeadTracking.ini exists" "Green"
}

# Check 5: Verify DLL was built
Write-ColorOutput ""
Write-ColorOutput "Checking build output..." "Gray"

$dllPath = "build/bin/Release/HeadTracking.dll"
if (-not (Test-Path $dllPath)) {
    Write-ColorOutput "WARNING: HeadTracking.dll not found (run 'pixi run build' first)" "Yellow"
    $allPassed = $false
} else {
    $dllInfo = Get-Item $dllPath
    Write-ColorOutput "HeadTracking.dll exists ($($dllInfo.Length) bytes)" "Green"
}

# Check 6: Verify no uncommitted changes
Write-ColorOutput ""
Write-ColorOutput "Checking git status..." "Gray"

$gitStatus = git status --porcelain 2>$null
if ($gitStatus) {
    Write-ColorOutput "WARNING: Uncommitted changes detected" "Yellow"
    Write-ColorOutput "Consider committing changes before release" "Gray"
    # Not a hard failure, just a warning
}
else {
    Write-ColorOutput "Working directory is clean" "Green"
}

# Summary
Write-ColorOutput ""
Write-ColorOutput "===============================" "Cyan"

if ($allPassed) {
    Write-ColorOutput "All validation checks passed!" "Green"
    Write-ColorOutput "Ready for release v$version" "Green"
    exit 0
} else {
    Write-ColorOutput "Some validation checks failed or have warnings." "Yellow"
    Write-ColorOutput "Please review the issues above before releasing." "Gray"
    exit 1
}
