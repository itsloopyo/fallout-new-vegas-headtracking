// Proxy-DLL entry point.
//
// Deployed as dsound.dll next to FalloutNV.exe, this is how the mod gets into
// the process without a script extender. The game statically imports exactly
// one function from dsound (ordinal 11, DirectSoundCreate8), which is forwarded
// to the system copy; loading us is the whole point, the export is the toll.
//
// dsound was picked over the cheaper-looking d3d9 slot because d3d9.dll is what
// ENB, FPS limiters and DXVK all replace, so a mod that takes it is a mod that
// fights them. dinput8 and binkw32 are the usual ASI-loader slots for this game.
// dsound is one export, is always present in the system directory, and nothing
// else in the New Vegas ecosystem wants it.

#include <Windows.h>

#include <string>
#include <vector>

#include "build_profile.h"
#include "plugin.h"
#include "d3d9_hook.h"
#include "proxy_entry.h"
#include "version.h"
#include "startup.h"

#include <cameraunlock/logging/file_log.h>

namespace culog = cameraunlock::logging;

namespace {

HMODULE g_realDsound = nullptr;
FARPROC g_realDirectSoundCreate8 = nullptr;

// Set only in FalloutNV.exe, on a build we have addresses for. Everything the
// proxy does past loading is gated on this.
volatile LONG g_driverArmed = 0;

std::wstring HostImagePath() {
    std::vector<wchar_t> buf(MAX_PATH);
    for (;;) {
        DWORD written = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
        if (written == 0) {
            return std::wstring();
        }
        if (written < buf.size()) {
            return std::wstring(buf.data(), written);
        }
        if (buf.size() >= 32768) {
            return std::wstring();
        }
        buf.resize(buf.size() * 2);
    }
}

// The Xbox app launches gamelaunchhelper -> FalloutNVLauncher.exe ->
// FalloutNV.exe, and the launcher imports dsound out of the same directory, so
// it loads this DLL too. Without this check the mod would initialise inside the
// launcher, where there is no game to track.
bool HostIsTheGame(const std::wstring& hostPath) {
    size_t slash = hostPath.find_last_of(L'\\');
    std::wstring leaf = (slash == std::wstring::npos) ? hostPath : hostPath.substr(slash + 1);
    return _wcsicmp(leaf.c_str(), L"FalloutNV.exe") == 0;
}

void OpenProxyLog(const std::wstring& hostPath) {
    size_t slash = hostPath.find_last_of(L'\\');
    if (slash == std::wstring::npos) {
        return;
    }
    const std::wstring logPath = hostPath.substr(0, slash + 1) + L"HeadTracking.log";
    MoveFileExW(logPath.c_str(),
                (hostPath.substr(0, slash + 1) + L"HeadTracking.prev.log").c_str(),
                MOVEFILE_REPLACE_EXISTING);
    culog::Open(logPath);
}

// Forwarding target has to be resolved by absolute path. Our own file is named
// dsound.dll and sits in the application directory, which the loader searches
// first, so LoadLibrary(L"dsound.dll") would find us and recurse.
void LoadRealDsound() {
    wchar_t path[MAX_PATH] = {0};
    UINT n = GetSystemDirectoryW(path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        return;
    }
    wcscat_s(path, MAX_PATH, L"\\dsound.dll");
    g_realDsound = LoadLibraryW(path);
    if (g_realDsound) {
        g_realDirectSoundCreate8 = GetProcAddress(g_realDsound, "DirectSoundCreate8");
    }
}

// The launcher and other game instances share the title. Only this process's
// game window and executable camera code establish that startup is ready.
bool WaitForGameStartup(const HeadTracking::BuildProfile& profile, DWORD timeoutMs) {
    const ULONGLONG deadline = GetTickCount64() + timeoutMs;
    for (;;) {
        HWND wnd = HeadTracking::FindGameWindow(GetCurrentProcessId());
        if (wnd && HeadTracking::IsCodeExecutable(profile.calcCullingPlanes) &&
            HeadTracking::IsCodeExecutable(profile.renderAccumulator) &&
            HeadTracking::IsCodeExecutable(profile.setupSkyGeometry) &&
            HeadTracking::IsCodeExecutable(profile.setCameraFov) &&
            HeadTracking::IsCodeExecutable(profile.updateCameraProjection)) {
            culog::Line("Startup ready: process=%lu window=%p camera code executable",
                        GetCurrentProcessId(), wnd);
            return true;
        }
        if (GetTickCount64() >= deadline) {
            culog::Line("ERROR: startup timed out: process=%lu window=%p cullingExecutable=%d weaponExecutable=%d skyExecutable=%d fovExecutable=%d projectionExecutable=%d",
                        GetCurrentProcessId(), wnd,
                        HeadTracking::IsCodeExecutable(profile.calcCullingPlanes),
                        HeadTracking::IsCodeExecutable(profile.renderAccumulator),
                        HeadTracking::IsCodeExecutable(profile.setupSkyGeometry),
                        HeadTracking::IsCodeExecutable(profile.setCameraFov),
                        HeadTracking::IsCodeExecutable(profile.updateCameraProjection));
            return false;
        }
        Sleep(250);
    }
}

DWORD WINAPI InitThread(LPVOID) {
    culog::Line("FNV Head Tracking v%d.%d.%d loaded through the dsound proxy",
                HeadTracking::VERSION_MAJOR, HeadTracking::VERSION_MINOR,
                HeadTracking::VERSION_PATCH);

    const auto* profile = HeadTracking::ResolveRunningBuild();
    HeadTracking::LogBuildIdentification();
    if (!profile) {
        // Dormant on purpose: no hooks installed, no memory written, game runs
        // vanilla. Hooking on addresses derived from a different build crashes
        // the player's game within seconds of a save loading.
        return 0;
    }

    if (!WaitForGameStartup(*profile, 120000)) {
        return 0;
    }

    if (!HeadTracking::HeadTrackingPlugin::Instance().Initialize()) {
        culog::Line("ERROR: plugin initialization failed - head tracking is inactive");
        return 0;
    }

    if (!HeadTracking::D3D9Hook::Instance().Initialize()) {
        culog::Line("ERROR: render hooks failed: %s", HeadTracking::D3D9Hook::Instance().GetErrorMessage());
        return 0;
    }
    InterlockedExchange(&g_driverArmed, 1);
    culog::Line("Render hooks ready - lifecycle and pose updates run on the render thread");
    return 0;
}

}  // namespace

namespace HeadTracking {
namespace Proxy {

bool DriverArmed() {
    return InterlockedCompareExchange(&g_driverArmed, 0, 0) != 0;
}

void OnFrame() {
    if (!DriverArmed()) {
        return;
    }
    const auto& profile = ActiveProfile();
    auto* player = *reinterpret_cast<uint8_t**>(profile.playerBase);
    auto* ui = *reinterpret_cast<uint8_t**>(profile.interfaceManager);
    const bool loaded = player && ui && *reinterpret_cast<void**>(player + 0x40);
    static bool wasLoaded = false;
    if (loaded != wasLoaded) {
        wasLoaded = loaded;
        if (loaded) {
            culog::Line("Gameplay active");
            HeadTrackingPlugin::Instance().OnGameLoaded();
        } else {
            culog::Line("Gameplay paused - tracking idle");
            HeadTrackingPlugin::Instance().OnGameExit();
        }
    }
    D3D9Hook::Instance().SetEnabled(loaded);
    HeadTrackingPlugin::Instance().Update();

}

}  // namespace Proxy
}  // namespace HeadTracking

extern "C" __declspec(dllexport) HRESULT WINAPI
DirectSoundCreate8(const GUID* device, void** ds8, void* outer) {
    if (!g_realDirectSoundCreate8) {
        return E_FAIL;
    }
    using Fn = HRESULT(WINAPI*)(const GUID*, void**, void*);
    return reinterpret_cast<Fn>(g_realDirectSoundCreate8)(device, ds8, outer);
}

namespace HeadTracking {
namespace Proxy {

// One binary serves both deployments, so it decides which entry point it is by
// the name it was loaded under: dsound.dll next to the exe is the proxy,
// anything else is the script-extender plugin and this does nothing.
bool LoadedAsProxy(HMODULE self) {
    wchar_t path[MAX_PATH] = {0};
    if (GetModuleFileNameW(self, path, MAX_PATH) == 0) {
        return false;
    }
    const wchar_t* slash = wcsrchr(path, L'\\');
    const wchar_t* leaf = slash ? slash + 1 : path;
    return _wcsicmp(leaf, L"dsound.dll") == 0;
}

void OnProcessAttach(HMODULE self) {
    if (!LoadedAsProxy(self)) {
        return;
    }

    // Unconditional within the proxy deployment: the launcher process needs
    // its audio too, and an export that only works in the game would break the
    // process that is not the game.
    LoadRealDsound();

    const std::wstring hostPath = HostImagePath();
    if (!HostIsTheGame(hostPath)) {
        return;
    }

    OpenProxyLog(hostPath);
    CloseHandle(CreateThread(nullptr, 0, InitThread, nullptr, 0, nullptr));
}

}  // namespace Proxy
}  // namespace HeadTracking
