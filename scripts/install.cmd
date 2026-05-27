@echo off
:: ============================================
:: Fallout: NV Head Tracking - Install
:: ============================================
:: Skeleton from cameraunlock-core/scripts/templates/install.cmd, with
:: the BepInEx loader path swapped for xNVSE. Deploys to the xNVSE
:: plugins dir (Data\NVSE\Plugins) and preserves an existing
:: HeadTracking.ini if the user has tweaked it.
:: ============================================

:: --- CONFIG BLOCK ---
set "GAME_ID=fallout-new-vegas"
set "MOD_DISPLAY_NAME=Fallout: NV Head Tracking"
set "MOD_DLLS=HeadTracking.dll"
set "MOD_INI=HeadTracking.ini"
set "MOD_INTERNAL_NAME=FalloutNVHeadTracking"
set "MOD_VERSION=1.0.0"
set "STATE_FILE=.headtracking-state.json"
set "FRAMEWORK_TYPE=xNVSE"
:: xNVSE ships with no upstream license, so we cannot redistribute the binary
:: inside our release ZIP. Instead we pin an exact version + SHA-256 and fetch
:: it from the official xNVSE GitHub release at install time, verifying the
:: hash before trusting it. To bump the pin, run `pixi run update-deps`.
set "XNVSE_VERSION=6.4.7"
set "XNVSE_URL=https://github.com/xNVSE/NVSE/releases/download/6.4.7/xnvse_6_4_7_windows_7_legacy_version.zip"
set "XNVSE_SHA256=339ae6c8f9bdd6c90a4feeaae49f3b45f828849ad8f3fb1ccca533ebe895bdbc"
set "MOD_CONTROLS=Controls (or use Ctrl+Shift+T/Y/G/H/U chord equivalents):&echo   Home      - Recenter head tracking&echo   End       - Toggle head tracking on/off&echo   Page Up   - Cycle tracking mode (normal / rotation only / position only)&echo   Page Down - Toggle aim reticle&echo   Insert    - Toggle yaw mode (world-locked / camera-local)"
:: --- END CONFIG BLOCK ---

call :detect_yes_flag %*
call :main %*
set "_EC=%errorlevel%"
if not defined YES_FLAG ( echo. & pause )
exit /b %_EC%

:: ============================================
:: Pre-scan args at outer scope so YES_FLAG propagates to the post-:main
:: pause check. :main's arg parser sets its own (local) YES_FLAG too, but
:: cmd.exe discards local vars when setlocal pops on `exit /b`, so without
:: this pre-scan the post-:main `if not defined YES_FLAG` always pauses
:: and /y can't make the script headless. Quoted-string form is required
:: here - bracket form `if [%~1]==[/y]` does NOT quote, so a path arg
:: containing whitespace ("C:\...\Gone Home") splits across the brackets
:: and crashes cmd with "[Home]==[/y] was unexpected at this time". The
:: trailing-backslash hazard the bracket form was working around is moot
:: with `%~1`: it strips the launcher's surrounding quotes before the
:: comparison, so a value like `C:\foo\` can't escape the closing `"`.
:: ============================================
:detect_yes_flag
if "%~1"=="" exit /b 0
if /i "%~1"=="/y"    set "YES_FLAG=1"
if /i "%~1"=="-y"    set "YES_FLAG=1"
if /i "%~1"=="--yes" set "YES_FLAG=1"
shift
goto :detect_yes_flag

:main
setlocal enabledelayedexpansion

:: Capture script dir BEFORE the arg parser runs. Inside `call :main`,
:: `shift` rotates %0 too, so %~dp0 read after shifts resolves to the
:: dirname of the first arg (e.g. C:\ for /y) instead of the script.
set "SCRIPT_DIR=%~dp0"

:: -------- Arg parser (canonical, do not modify) --------
set "YES_FLAG="
set "_GIVEN_PATH="
:parse_args
if "%~1"=="" goto :args_done
set "_ARG=%~1"
if /i "!_ARG!"=="/y"    ( set "YES_FLAG=1" & shift & goto :parse_args )
if /i "!_ARG!"=="-y"    ( set "YES_FLAG=1" & shift & goto :parse_args )
if /i "!_ARG!"=="--yes" ( set "YES_FLAG=1" & shift & goto :parse_args )
if "!_ARG:~0,2!"=="--" ( echo ERROR: unknown flag "!_ARG!" & exit /b 2 )
if "!_ARG:~0,1!"=="/"  ( echo ERROR: unknown flag "!_ARG!" & exit /b 2 )
if "!_ARG:~0,1!"=="-"  ( echo ERROR: unknown flag "!_ARG!" & exit /b 2 )
if not defined _GIVEN_PATH (
    if exist "!_ARG!\" ( set "_GIVEN_PATH=!_ARG!" & shift & goto :parse_args )
)
echo ERROR: unrecognised argument "!_ARG!"
exit /b 2
:args_done

echo.
echo === %MOD_DISPLAY_NAME% - Install ===
echo.

:: -------- Resolve game path via shared shim --------
:: Release ZIP layout: scripts\ is the ZIP root, shim is at shared\find-game.ps1.
:: Dev tree layout: scripts\ is <repo>\scripts\, shim is at ..\cameraunlock-core\scripts\find-game.ps1.
set "_SHIM=%SCRIPT_DIR%shared\find-game.ps1"
if not exist "%_SHIM%" set "_SHIM=%SCRIPT_DIR%..\cameraunlock-core\scripts\find-game.ps1"
if not exist "%_SHIM%" (
    echo ERROR: find-game.ps1 not found in shared\ or ..\cameraunlock-core\scripts\.
    echo If this is a release ZIP, re-download it from GitHub ^(corrupt installer^).
    echo If this is the dev tree, make sure the cameraunlock-core submodule is checked out.
    exit /b 1
)
set "_SHIM_OUT=%TEMP%\cul-find-%RANDOM%-%RANDOM%.cmd"
set "_GIVEN_ARG="
if defined _GIVEN_PATH set "_GIVEN_ARG=-GivenPath "!_GIVEN_PATH!""
powershell -NoProfile -ExecutionPolicy Bypass -File "%_SHIM%" -GameId %GAME_ID% -OutFile "!_SHIM_OUT!" !_GIVEN_ARG!
set "_PS_EC=!errorlevel!"
if not "!_PS_EC!"=="0" (
    echo.
    echo ERROR: Could not resolve game install path ^(shim exit code !_PS_EC!^).
    echo Pass a path explicitly: install.cmd "C:\path\to\game"
    echo.
    del "!_SHIM_OUT!" 2>nul
    exit /b 1
)
call "!_SHIM_OUT!"
del "!_SHIM_OUT!" 2>nul

echo Game found: %GAME_PATH%
echo.

:: -------- Game-running check --------
tasklist /fi "imagename eq %GAME_EXE%" 2>nul | findstr /i "%GAME_EXE%" >nul 2>&1
if not errorlevel 1 (
    echo ERROR: %GAME_DISPLAY_NAME% is currently running.
    echo Please close the game before installing.
    echo.
    exit /b 1
)

:: -------- Prior state: preserve installed_by_us=true across re-installs --------
set "WE_INSTALLED=false"
if exist "%GAME_PATH%\%STATE_FILE%" (
    findstr /c:"installed_by_us" "%GAME_PATH%\%STATE_FILE%" 2>nul | findstr /c:"true" >nul 2>&1
    if not errorlevel 1 set "WE_INSTALLED=true"
)

:: -------- Ensure xNVSE --------
if not exist "%GAME_PATH%\nvse_loader.exe" (
    echo xNVSE not found. Installing...
    echo.
    call :install_xnvse
    if errorlevel 1 exit /b 1
    set "WE_INSTALLED=true"
    echo.
) else (
    echo Existing xNVSE detected, skipping loader install, deploying plugin only.
)
echo.

:: -------- Deploy mod files --------
echo Deploying mod files...

set "PLUGINS_PATH=%GAME_PATH%\Data\NVSE\Plugins"
set "DLL_DIR=%SCRIPT_DIR%plugins"

if not exist "%PLUGINS_PATH%" mkdir "%PLUGINS_PATH%"

set "DEPLOY_FAILED=0"
for %%f in (%MOD_DLLS%) do (
    if exist "%DLL_DIR%\%%f" (
        copy /y "%DLL_DIR%\%%f" "%PLUGINS_PATH%\" >nul
        echo   Deployed %%f
    ) else (
        echo   ERROR: %%f not found in plugins folder
        set "DEPLOY_FAILED=1"
    )
)

:: Copy INI only if not already present (preserve user settings).
if defined MOD_INI (
    if exist "%DLL_DIR%\%MOD_INI%" (
        if not exist "%PLUGINS_PATH%\%MOD_INI%" (
            copy /y "%DLL_DIR%\%MOD_INI%" "%PLUGINS_PATH%\" >nul
            echo   Deployed %MOD_INI%
        ) else (
            echo   Skipped %MOD_INI% ^(already exists, preserving user settings^)
        )
    )
)

if "!DEPLOY_FAILED!"=="1" (
    echo.
    echo ========================================
    echo   Deployment Failed!
    echo ========================================
    echo.
    exit /b 1
)

:: -------- Write state file --------
call :write_state_file

echo.
echo ========================================
echo   Deployment Complete!
echo ========================================
echo.
echo %MOD_DISPLAY_NAME% has been deployed to:
echo   %PLUGINS_PATH%
echo.
echo Launch the game via nvse_loader.exe to use the mod!
if defined MOD_CONTROLS (
    echo.
    echo !MOD_CONTROLS!
)
echo.
exit /b 0

:: ============================================
:: Install xNVSE by downloading the pinned release at install time.
:: xNVSE has no upstream license and cannot be redistributed in our ZIP, so
:: install.cmd fetches it from GitHub and verifies it against the pinned
:: SHA-256 from the CONFIG BLOCK. To bump the pin, run `pixi run update-deps`.
:: ============================================
:install_xnvse
set "XNVSE_DL=%TEMP%\xnvse_dl_%RANDOM%%RANDOM%.zip"

echo   Downloading xNVSE %XNVSE_VERSION%...
powershell -NoProfile -ExecutionPolicy Bypass -Command "try { [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12; Invoke-WebRequest -Uri '%XNVSE_URL%' -OutFile '%XNVSE_DL%' -UseBasicParsing -TimeoutSec 120 } catch { Write-Host $_.Exception.Message; exit 1 }"
if errorlevel 1 (
    echo   ERROR: Failed to download xNVSE from:
    echo     %XNVSE_URL%
    echo   Check your internet connection, or install xNVSE manually from
    echo   https://github.com/xNVSE/NVSE/releases and re-run this installer.
    del "%XNVSE_DL%" 2>nul
    exit /b 1
)

:: Verify the download matches the pinned hash before trusting it.
set "XNVSE_DL_SHA="
for /f "delims=" %%H in ('powershell -NoProfile -ExecutionPolicy Bypass -Command "(Get-FileHash -Path '%XNVSE_DL%' -Algorithm SHA256).Hash.ToLower()"') do set "XNVSE_DL_SHA=%%H"
if /i not "!XNVSE_DL_SHA!"=="%XNVSE_SHA256%" (
    echo   ERROR: xNVSE download failed its integrity check.
    echo     expected %XNVSE_SHA256%
    echo     got      !XNVSE_DL_SHA!
    del "%XNVSE_DL%" 2>nul
    exit /b 1
)

echo   Extracting xNVSE...

:: Extract to a temp dir and locate nvse_loader.exe to find the real source
:: root before copying into GAME_PATH.
set "XNVSE_EXTRACT=%TEMP%\xNVSE_extract"
if exist "%XNVSE_EXTRACT%" rmdir /s /q "%XNVSE_EXTRACT%"
mkdir "%XNVSE_EXTRACT%"

"%SystemRoot%\System32\tar.exe" -xf "%XNVSE_DL%" -C "%XNVSE_EXTRACT%"
if errorlevel 1 (
    echo   ERROR: Extraction failed.
    del "%XNVSE_DL%" 2>nul
    rmdir /s /q "%XNVSE_EXTRACT%" 2>nul
    exit /b 1
)
del "%XNVSE_DL%" 2>nul

:: The legacy-version asset wraps the binaries in an inner .zip. Unpack any
:: nested archives in place and delete them so only the binaries remain to copy.
for /r "%XNVSE_EXTRACT%" %%z in (*.zip) do (
    "%SystemRoot%\System32\tar.exe" -xf "%%z" -C "%%~dpz"
    del /q "%%z"
)

set "XNVSE_SRC="
for /r "%XNVSE_EXTRACT%" %%f in (nvse_loader.exe) do (
    if not defined XNVSE_SRC set "XNVSE_SRC=%%~dpf"
)

if not defined XNVSE_SRC (
    echo   ERROR: nvse_loader.exe not found in downloaded archive.
    rmdir /s /q "%XNVSE_EXTRACT%" 2>nul
    exit /b 1
)

xcopy "!XNVSE_SRC!*" "%GAME_PATH%\" /s /y /q >nul
rmdir /s /q "%XNVSE_EXTRACT%"

if not exist "%GAME_PATH%\nvse_loader.exe" (
    echo   ERROR: xNVSE installation failed - nvse_loader.exe not present after copy.
    exit /b 1
)

echo   xNVSE installed successfully!
exit /b 0

:: ============================================
:: Write the canonical state file.
:: Schema version 1. Preserves WE_INSTALLED which may have been
:: already-true from a prior install.
:: ============================================
:write_state_file
> "%GAME_PATH%\%STATE_FILE%" (
    echo {
    echo   "schema_version": 1,
    echo   "framework": {
    echo     "type": "%FRAMEWORK_TYPE%",
    echo     "installed_by_us": !WE_INSTALLED!
    echo   },
    echo   "mod": {
    echo     "id": "%GAME_ID%",
    echo     "name": "%MOD_INTERNAL_NAME%",
    echo     "version": "%MOD_VERSION%"
    echo   }
    echo }
)
exit /b 0
