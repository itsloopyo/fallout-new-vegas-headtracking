// FNV Head Tracking NVSE Plugin
// Main entry point for NVSE plugin system

#include <Windows.h>

#include <cstdint>

#define RUNTIME

#include "nvse_abi/PluginAPI.h"
#include "nvse_abi/GameAPI.h"

#include "plugin.h"
#include "version.h"
#include "debug_log.h"
#include "proxy_entry.h"
#include "build_profile.h"

#include <cameraunlock/logging/file_log.h>

#include <string>
#include <vector>

using namespace HeadTracking;

// Global plugin handle
static PluginHandle g_pluginHandle = 0;

namespace culog = cameraunlock::logging;

// HeadTracking.log next to the plugin DLL, matching where the INI lives.
static void OpenModLog(HMODULE hModule) {
    // GetModuleFileNameW truncates silently at the buffer size and reports it
    // by returning exactly that size. A deep Steam library path is not far off
    // MAX_PATH, and the old fixed buffer bare-returned on it: no log, and no
    // signal anywhere that logging had been skipped. Grow until the call fits.
    std::vector<wchar_t> buf(MAX_PATH);
    DWORD written = 0;
    for (;;) {
        written = GetModuleFileNameW(hModule, buf.data(), static_cast<DWORD>(buf.size()));
        if (written == 0 || written < buf.size()) {
            break;
        }
        if (buf.size() >= 32768) {
            written = 0;
            break;
        }
        buf.resize(buf.size() * 2);
    }
    if (written == 0) {
        OutputDebugStringW(L"FNV Head Tracking: could not resolve the plugin DLL path; "
                           L"no log file will be written\n");
        return;
    }
    std::wstring stem(buf.data(), written);
    size_t dot = stem.rfind(L'.');
    if (dot != std::wstring::npos) {
        stem.resize(dot);
    }
    const std::wstring logPath = stem + L".log";
    // Keep one generation. The log truncates per run so a bug report carries
    // only the current session, but a crash-then-relaunch would otherwise
    // destroy the log that recorded the crash.
    MoveFileExW(logPath.c_str(), (stem + L".prev.log").c_str(), MOVEFILE_REPLACE_EXISTING);
    culog::Open(logPath);
}

// Message handler for game lifecycle events
static void MessageHandler(NVSEMessagingInterface::Message* msg) {
    if (!msg) {
        return;
    }

    switch (msg->type) {
        case NVSEMessagingInterface::kMessage_PostLoadGame:
        case NVSEMessagingInterface::kMessage_NewGame:
            HT_LOG_MAIN("MessageHandler: Game Loaded!");
            HeadTrackingPlugin::Instance().OnGameLoaded();
            break;

        case NVSEMessagingInterface::kMessage_ExitGame:
        case NVSEMessagingInterface::kMessage_ExitToMainMenu:
            HT_LOG_MAIN("MessageHandler: Game Exit");
            HeadTrackingPlugin::Instance().OnGameExit();
            break;

        case NVSEMessagingInterface::kMessage_MainGameLoop:
#if HEADTRACKING_DEBUG_LOGGING
            { static int loopCount = 0; loopCount++;
              HT_LOG_PERIODIC(Main, "HeadTracking_debug.log", 60, loopCount,
                  "MessageHandler: MainGameLoop frame %d", loopCount); }
#endif
            HeadTrackingPlugin::Instance().Update();
            break;

        default:
            break;
    }
}

// NVSE Plugin Query - called first to get plugin info
extern "C" __declspec(dllexport) bool NVSEPlugin_Query(const NVSEInterface* nvse, PluginInfo* info) {
    if (!nvse || !info) {
        return false;
    }

    // Fill in plugin info
    info->infoVersion = PluginInfo::kInfoVersion;
    info->name = PLUGIN_NAME;
    info->version = PLUGIN_VERSION;

    HMODULE proxy = GetModuleHandleW(L"dsound.dll");
    if (proxy && proxy != g_hModule && GetProcAddress(proxy, "NVSEPlugin_Query")) {
        culog::Line("NVSE compatibility copy dormant: root proxy owns head tracking");
        return false;
    }

    // Check NVSE version
    if (nvse->nvseVersion < NVSE_VERSION_REQUIRED) {
        culog::Line("NVSEPlugin_Query: NVSE %08X is older than the required %08X - not loading",
                  nvse->nvseVersion, NVSE_VERSION_REQUIRED);
        return false;
    }

    // Check if running in editor (GECK)
    if (nvse->isEditor) {
        culog::Line("NVSEPlugin_Query: running in the GECK editor - not loading");
        return false;  // Don't load in GECK editor
    }

    culog::Line("NVSEPlugin_Query: accepted (NVSE %08X)", nvse->nvseVersion);
    return true;
}

// NVSE Plugin Load - called after query succeeds to initialize plugin
extern "C" __declspec(dllexport) bool NVSEPlugin_Load(const NVSEInterface* nvse) {
    HT_LOG_MAIN("NVSEPlugin_Load called");
    culog::Line("NVSEPlugin_Load called");

    if (!nvse) {
        HT_LOG_MAIN("ERROR: nvse is null");
        culog::Line("ERROR: NVSE interface is null - plugin cannot load");
        return false;
    }

    if (!ResolveRunningBuild()) {
        LogBuildIdentification();
        return false;
    }

    g_pluginHandle = nvse->GetPluginHandle();
    HT_LOG_MAIN("Got plugin handle");

    // Initialize plugin
    if (!HeadTrackingPlugin::Instance().Initialize(nvse)) {
        HT_LOG_MAIN("ERROR: Plugin Initialize failed");
        culog::Line("ERROR: plugin initialization failed - head tracking is inactive");
        return false;
    }
    HT_LOG_MAIN("Plugin initialized");

    // Get messaging interface
    NVSEMessagingInterface* messagingInterface = static_cast<NVSEMessagingInterface*>(
        nvse->QueryInterface(kInterface_Messaging));

    if (!messagingInterface) {
        HT_LOG_MAIN("ERROR: Failed to get messaging interface");
        culog::Line("ERROR: NVSE messaging interface unavailable - head tracking is inactive");
        return false;
    }
    HT_LOG_MAIN("Got messaging interface");

    // Register for game lifecycle messages from NVSE
    if (!messagingInterface->RegisterListener(g_pluginHandle, "NVSE", MessageHandler)) {
        HT_LOG_MAIN("WARNING: Failed to register NVSE message listener");
        culog::Line("WARNING: failed to register the NVSE message listener - "
                  "game load/exit events will not reach the mod");
    } else {
        HT_LOG_MAIN("Registered NVSE message listener");
    }

    // Also register for all plugins (nullptr = all senders, for MainGameLoop)
    if (!messagingInterface->RegisterListener(g_pluginHandle, nullptr, MessageHandler)) {
        HT_LOG_MAIN("WARNING: Failed to register global message listener");
        culog::Line("WARNING: failed to register the global message listener - "
                  "the per-frame update will not run");
    } else {
        HT_LOG_MAIN("Registered global message listener");
    }

    HT_LOG_MAIN("Plugin load complete!");
    culog::Line("Plugin load complete");

    return true;
}

// DLL entry point
BOOL WINAPI DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved) {
    (void)lpReserved;  // Unused

    switch (reason) {
        case DLL_PROCESS_ATTACH:
            g_hModule = hModule;
            DisableThreadLibraryCalls(hModule);
            HeadTracking::Proxy::OnProcessAttach(hModule);
            if (HeadTracking::Proxy::LoadedAsProxy(hModule)) {
                // The proxy opens its own log next to the exe under the mod's
                // name; opening a second one here would name it dsound.log.
                break;
            }
            OpenModLog(hModule);
            culog::Line("FNV Head Tracking v%d.%d.%d attached to the game process",
                      VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH);
            break;

        case DLL_PROCESS_DETACH:
            HeadTrackingPlugin::Instance().Shutdown();
            culog::Line("Detaching from the game process");
            culog::Close();
            break;

        default:
            break;
    }

    return TRUE;
}
