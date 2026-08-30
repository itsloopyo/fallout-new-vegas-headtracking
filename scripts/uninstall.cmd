@echo off
:: ============================================
:: Fallout: NV Head Tracking - Uninstall
:: ============================================
:: Thin wrapper - uninstall body lives in cameraunlock-core/scripts/uninstall-body.cmd,
:: staged into the release ZIP's shared/ by Copy-SharedBundle. To change
:: uninstall behaviour edit the body, not this wrapper. Everything below the
:: CONFIG BLOCK is copied verbatim from
:: cameraunlock-core/scripts/templates/uninstall-wrapper.cmd.
:: ============================================

:: --- CONFIG BLOCK ---
set "GAME_ID=fallout-new-vegas"
set "MOD_DISPLAY_NAME=Fallout: NV Head Tracking"
set "MOD_DLLS=HeadTracking.dll HeadTracking.log HeadTracking.prev.log"
set "MOD_INTERNAL_NAME=FalloutNVHeadTracking"
set "STATE_FILE=.headtracking-state.json"
:: BepInEx | MelonLoader | MonoCecil | ASILoader | REFramework | UE4SS | xNVSE
:: | None
set "FRAMEWORK_TYPE=xNVSE"
:: DLL names shipped by older versions of this mod, removed too so an upgrade
:: does not leave a second copy for the loader to bind.
set "LEGACY_DLLS="
:: Files install.cmd seeded write-if-absent. MUST list the same names as
:: install.cmd's MOD_SEED_FILES, or an uninstall leaves the mod's config behind.
set "MOD_SEED_FILES=HeadTracking.ini"
:: BepInEx: subfolder under BepInEx\plugins\ the DLLs were deployed into.
set "PLUGIN_SUBFOLDER="
:: Config and log files the mod writes at runtime, removed from wherever the
:: DLLs were deployed.
set "MOD_LEFTOVERS="
:: Files to remove from the game root. Only needed by a mod deployed BELOW the
:: root (see ASI_SUBDIR) that still resolves its config and log from the exe's
:: own directory.
set "ROOT_EXTRAS="

:: --- Loader-specific config (leave the ones that don't apply blank) ---
:: MonoCecil: used to find + restore the original Assembly-CSharp.dll.
set "MANAGED_SUBFOLDER="
set "ASSEMBLY_DLL="
:: MonoCecil: marker the patcher injects; guards against capturing/restoring a
:: patched Assembly-CSharp.dll as the pristine .original backup.
set "PATCH_MARKER="
:: MonoCecil: extra files to also remove from MANAGED_SUBFOLDER (config/log
:: files left behind by the mod itself).
set "MANAGED_EXTRAS="
:: ASILoader: filename the ASI DLL was renamed to. Defaults to winmm.dll.
set "ASI_LOADER_NAME="
:: ASILoader: subdirectory below the exe directory the payload went into. MUST
:: match install.cmd's value, or uninstall looks in the wrong folder.
set "ASI_SUBDIR="
:: UE4SS: path under GAME_PATH holding the shipping exe. MUST match
:: install.cmd's value.
set "UE4_BINARIES_RELDIR="
:: --- END CONFIG BLOCK ---

:: Pin delayed expansion off before `%*` is expanded on the `call` below.
:: Under `cmd /V:ON`, or with DelayedExpansion=1 in
:: HKCU\Software\Microsoft\Command Processor, cmd.exe eats a `!` out of the
:: expanded line, and a real game path like C:\Games\Oh! My Game reaches the
:: body already mangled. The body pins expansion off at its own outer scope
:: too, but that is one `call` too late to save the argument it was handed.
setlocal disabledelayedexpansion

set "WRAPPER_DIR=%~dp0"
set "_BODY=%WRAPPER_DIR%shared\uninstall-body.cmd"
if not exist "%_BODY%" set "_BODY=%WRAPPER_DIR%..\cameraunlock-core\scripts\uninstall-body.cmd"
if not exist "%_BODY%" (
    echo ERROR: uninstall-body.cmd not found in shared\ or ..\cameraunlock-core\scripts\.
    echo If this is a release ZIP, re-download it from GitHub ^(corrupt installer^).
    echo If this is the dev tree, run: git submodule update --init --recursive
    exit /b 1
)
call "%_BODY%" %*
exit /b %errorlevel%
