// FNV Head Tracking NVSE Plugin
// Main entry point for NVSE plugin system

#include <Windows.h>

#include <cstdint>

#define RUNTIME

#include "nvse/PluginAPI.h"
#include "nvse/GameAPI.h"

#include "plugin.h"
#include "version.h"
#include "debug_log.h"

using namespace HeadTracking;

// Global plugin handle
static PluginHandle g_pluginHandle = 0;

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

    // Check NVSE version
    if (nvse->nvseVersion < NVSE_VERSION_REQUIRED) {
        return false;
    }

    // Check if running in editor (GECK)
    if (nvse->isEditor) {
        return false;  // Don't load in GECK editor
    }

    return true;
}

// NVSE Plugin Load - called after query succeeds to initialize plugin
extern "C" __declspec(dllexport) bool NVSEPlugin_Load(const NVSEInterface* nvse) {
    HT_LOG_MAIN("NVSEPlugin_Load called");

    if (!nvse) {
        HT_LOG_MAIN("ERROR: nvse is null");
        return false;
    }

    g_pluginHandle = nvse->GetPluginHandle();
    HT_LOG_MAIN("Got plugin handle");

    // Initialize plugin
    if (!HeadTrackingPlugin::Instance().Initialize(nvse)) {
        HT_LOG_MAIN("ERROR: Plugin Initialize failed");
        return false;
    }
    HT_LOG_MAIN("Plugin initialized");

    // Get messaging interface
    NVSEMessagingInterface* messagingInterface = static_cast<NVSEMessagingInterface*>(
        nvse->QueryInterface(kInterface_Messaging));

    if (!messagingInterface) {
        HT_LOG_MAIN("ERROR: Failed to get messaging interface");
        return false;
    }
    HT_LOG_MAIN("Got messaging interface");

    // Register for game lifecycle messages from NVSE
    if (!messagingInterface->RegisterListener(g_pluginHandle, "NVSE", MessageHandler)) {
        HT_LOG_MAIN("WARNING: Failed to register NVSE message listener");
    } else {
        HT_LOG_MAIN("Registered NVSE message listener");
    }

    // Also register for all plugins (nullptr = all senders, for MainGameLoop)
    if (!messagingInterface->RegisterListener(g_pluginHandle, nullptr, MessageHandler)) {
        HT_LOG_MAIN("WARNING: Failed to register global message listener");
    } else {
        HT_LOG_MAIN("Registered global message listener");
    }

    HT_LOG_MAIN("Plugin load complete!");

    return true;
}

// DLL entry point
BOOL WINAPI DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved) {
    (void)lpReserved;  // Unused

    switch (reason) {
        case DLL_PROCESS_ATTACH:
            g_hModule = hModule;
            DisableThreadLibraryCalls(hModule);
            break;

        case DLL_PROCESS_DETACH:
            HeadTrackingPlugin::Instance().Shutdown();
            break;

        default:
            break;
    }

    return TRUE;
}
