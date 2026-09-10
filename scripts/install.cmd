@echo off
:: ============================================
:: Fallout: NV Head Tracking - Install
:: ============================================
:: Thin wrapper - install body lives in cameraunlock-core/scripts/install-body-xnvse.cmd,
:: staged into the release ZIP's shared/ by Copy-SharedBundle. To change
:: install behaviour edit the body, not this wrapper. Everything below the
:: CONFIG BLOCK is copied verbatim from
:: cameraunlock-core/scripts/templates/install-wrapper-xnvse.cmd.
:: ============================================

:: --- CONFIG BLOCK ---
set "GAME_ID=fallout-new-vegas"
set "MOD_DISPLAY_NAME=Fallout: NV Head Tracking"
set "MOD_DLLS=HeadTracking.dll"
set "MOD_INTERNAL_NAME=FalloutNVHeadTracking"
set "MOD_VERSION=0.0.0"
set "STATE_FILE=.headtracking-state.json"
set "FRAMEWORK_TYPE=xNVSE"
:: xNVSE pin. Bump with `pixi run update-deps`, review the diff, commit.
:: The asset must be a .zip - the body extracts it with tar.exe.
set "XNVSE_VERSION=6.4.7"
set "XNVSE_URL=https://github.com/xNVSE/NVSE/releases/download/6.4.7/xnvse_6_4_7_windows_7_legacy_version.zip"
set "XNVSE_SHA256=339ae6c8f9bdd6c90a4feeaae49f3b45f828849ad8f3fb1ccca533ebe895bdbc"
:: Files copied only when they are not already there, so an upgrade keeps
:: whatever the user tuned. Listing an .ini in MOD_DLLS instead puts it through
:: the unconditional copy and resets every key on every update.
set "MOD_SEED_FILES=HeadTracking.ini"
:: Post-install help text. `&echo ` starts each further line.
set "MOD_CONTROLS=Controls (or use Ctrl+Shift+Y/G/H/U chord equivalents):&echo   End       - Toggle head tracking on/off&echo   Page Up   - Cycle tracking mode (normal / rotation only / position only)&echo   Page Down - Toggle aim reticle&echo   Insert    - Toggle yaw mode (world-locked / camera-local)"
:: --- END CONFIG BLOCK ---

:: Pin delayed expansion off before `%*` is expanded on the `call` below.
:: Under `cmd /V:ON`, or with DelayedExpansion=1 in
:: HKCU\Software\Microsoft\Command Processor, cmd.exe eats a `!` out of the
:: expanded line, and a real game path like C:\Games\Oh! My Game reaches the
:: body already mangled. The body pins expansion off at its own outer scope
:: too, but that is one `call` too late to save the argument it was handed.
setlocal disabledelayedexpansion

set "WRAPPER_DIR=%~dp0"
set "_BODY=%WRAPPER_DIR%shared\install-body-xnvse.cmd"
if not exist "%_BODY%" set "_BODY=%WRAPPER_DIR%..\cameraunlock-core\scripts\install-body-xnvse.cmd"
if not exist "%_BODY%" (
    echo ERROR: install-body-xnvse.cmd not found in shared\ or ..\cameraunlock-core\scripts\.
    echo If this is a release ZIP, re-download it from GitHub ^(corrupt installer^).
    echo If this is the dev tree, run: git submodule update --init --recursive
    exit /b 1
)
call "%_BODY%" %*
exit /b %errorlevel%
