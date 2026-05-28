# Revert a release - deletes tags and optionally reverts release commit
#
# Usage: pixi run unrelease <version>
# Example: pixi run unrelease 1.0.1

$ErrorActionPreference = "Stop"

function Write-ColorOutput {
    param(
        [string]$Message,
        [string]$Color = "White"
    )
    Write-Host $Message -ForegroundColor $Color
}

# Main script
Write-ColorOutput "HeadTracking Release Revert" "Cyan"
Write-ColorOutput "===========================" "Cyan"
Write-ColorOutput ""

$version = $args[0]

if (-not $version -or $version -notmatch '^\d+\.\d+\.\d+$') {
    Write-ColorOutput "ERROR: version required. Usage: pixi run unrelease X.Y.Z" "Red"
    exit 1
}

$tag = "v$version"
Write-ColorOutput "Reverting release: $tag" "Yellow"
Write-ColorOutput ""

# Check if tag exists locally
$localTag = git tag -l $tag 2>$null
if (-not $localTag) {
    Write-ColorOutput "WARNING: Local tag $tag not found" "Yellow"
} else {
    Write-ColorOutput "Deleting local tag $tag..." "Gray"
    git tag -d $tag
    Write-ColorOutput "Local tag deleted" "Green"
}

# Check if tag exists remotely
$remoteTag = git ls-remote --tags origin $tag 2>$null
if ($remoteTag) {
    Write-ColorOutput "Deleting remote tag $tag..." "Gray"
    git push origin --delete $tag 2>$null
    if ($LASTEXITCODE -eq 0) {
        Write-ColorOutput "Remote tag deleted" "Green"
    } else {
        Write-ColorOutput "WARNING: Could not delete remote tag (may not exist or no permission)" "Yellow"
    }
} else {
    Write-ColorOutput "No remote tag found for $tag" "Gray"
}

# Revert the release commit only when HEAD is actually that commit. The
# message match is the safety net; running `pixi run unrelease` is consent.
Write-ColorOutput ""
$lastCommit = git log -1 --oneline
Write-ColorOutput "Last commit: $lastCommit" "Gray"

if ($lastCommit -match "Release v$version") {
    Write-ColorOutput "Reverting release commit..." "Gray"
    git reset --soft HEAD~1
    git restore --staged src/version.h
    git checkout src/version.h
    Write-ColorOutput "Commit reverted and files restored" "Green"
} else {
    Write-ColorOutput "Last commit is not 'Release v$version' - leaving commits untouched" "Yellow"
    Write-ColorOutput "Only the tag(s) were removed." "Gray"
}

Write-ColorOutput ""
Write-ColorOutput "===========================" "Cyan"
Write-ColorOutput "Release $tag reverted" "Green"
Write-ColorOutput ""
Write-ColorOutput "If you pushed changes, you may need to force-push:" "Yellow"
Write-ColorOutput "  git push --force origin main" "Cyan"
Write-ColorOutput ""
Write-ColorOutput "WARNING: Force-pushing rewrites history. Use with caution!" "Red"
