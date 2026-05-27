@echo off
:: ============================================
:: Fallout: NV Head Tracking - Uninstall
:: ============================================
:: Skeleton from cameraunlock-core/scripts/templates/uninstall.cmd.
:: xNVSE is a SHARED modding framework: other Fallout: NV mods may
:: depend on it, so we never remove it (even with /force). The flag
:: still parses cleanly for launcher consistency.
:: ============================================

:: --- CONFIG BLOCK ---
set "GAME_ID=fallout-new-vegas"
set "MOD_DISPLAY_NAME=Fallout: NV Head Tracking"
set "MOD_DLLS=HeadTracking.dll"
set "MOD_INI=HeadTracking.ini"
set "MOD_INTERNAL_NAME=FalloutNVHeadTracking"
set "STATE_FILE=.headtracking-state.json"
set "FRAMEWORK_TYPE=xNVSE"
set "LEGACY_DLLS="
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
set "FORCE_FLAG="
set "_GIVEN_PATH="
:parse_args
if "%~1"=="" goto :args_done
set "_ARG=%~1"
if /i "!_ARG!"=="/y"      ( set "YES_FLAG=1"   & shift & goto :parse_args )
if /i "!_ARG!"=="-y"      ( set "YES_FLAG=1"   & shift & goto :parse_args )
if /i "!_ARG!"=="--yes"   ( set "YES_FLAG=1"   & shift & goto :parse_args )
if /i "!_ARG!"=="/force"  ( set "FORCE_FLAG=1" & shift & goto :parse_args )
if /i "!_ARG!"=="--force" ( set "FORCE_FLAG=1" & shift & goto :parse_args )
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
echo === %MOD_DISPLAY_NAME% - Uninstall ===
echo.

:: -------- Resolve game path via shared shim --------
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
    echo Pass a path explicitly: uninstall.cmd "C:\path\to\game"
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
    echo Please close the game before uninstalling.
    echo.
    exit /b 1
)

:: -------- Compute DEPLOY_DIR --------
set "DEPLOY_DIR=%GAME_PATH%\Data\NVSE\Plugins"

:: -------- Remove mod files --------
call :remove_mod_files_plain

:: xNVSE is shared - never remove it. /force only changes the message.
if "!FORCE_FLAG!"=="1" (
    echo.
    echo xNVSE is a shared modding framework - leaving intact even with /force ^(other mods may depend on it^).
) else (
    echo.
    echo xNVSE was left intact ^(other mods may depend on it^).
)

:: -------- Remove state file --------
if exist "%GAME_PATH%\%STATE_FILE%" (
    del "%GAME_PATH%\%STATE_FILE%"
    echo   Removed: state file
)

echo.
echo === Uninstall Complete ===
echo.
exit /b 0

:: ============================================
:: Remove mod DLLs + INI + legacy DLLs from DEPLOY_DIR.
:: ============================================
:remove_mod_files_plain
echo Removing mod files...
set "REMOVED=0"
for %%f in (%MOD_DLLS%) do (
    if exist "!DEPLOY_DIR!\%%f" (
        del "!DEPLOY_DIR!\%%f"
        echo   Removed: %%f
        set /a REMOVED+=1
    )
)
if defined MOD_INI (
    if exist "!DEPLOY_DIR!\%MOD_INI%" (
        del "!DEPLOY_DIR!\%MOD_INI%"
        echo   Removed: %MOD_INI%
        set /a REMOVED+=1
    )
)
if defined LEGACY_DLLS (
    for %%f in (%LEGACY_DLLS%) do (
        if exist "!DEPLOY_DIR!\%%f" (
            del "!DEPLOY_DIR!\%%f"
            echo   Removed: %%f ^(legacy^)
            set /a REMOVED+=1
        )
    )
)
if "!REMOVED!"=="0" echo   No mod files found
exit /b 0
