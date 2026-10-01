#include <Windows.h>

#include "plugin.h"
#include "proxy_entry.h"

#include <cameraunlock/logging/file_log.h>

BOOL WINAPI DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    switch (reason) {
        case DLL_PROCESS_ATTACH:
            HeadTracking::g_hModule = hModule;
            DisableThreadLibraryCalls(hModule);
            HeadTracking::Proxy::OnProcessAttach(hModule);
            break;

        case DLL_PROCESS_DETACH:
            // Process teardown must not join worker threads under the loader lock.
            cameraunlock::logging::Line("Detaching from the game process");
            cameraunlock::logging::Close();
            break;

        default:
            break;
    }
    return TRUE;
}
