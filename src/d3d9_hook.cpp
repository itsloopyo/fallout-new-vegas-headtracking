// D3D9 frame presentation hook
// Crosshair handling is in d3d9_crosshair.cpp
// Culling hook is in d3d9_culling.cpp

#include "d3d9_hook.h"
#include "proxy_entry.h"
#include "d3d9_internal.h"
#include "camera_controller.h"
#include "game_offsets.h"
#include "debug_log.h"
#include "window_centering.h"

#include <MinHook.h>
#include <cameraunlock/logging/file_log.h>

#include <cstdio>
#include <cstring>
#include <cmath>

#pragma comment(lib, "d3d9.lib")

namespace HeadTracking {

// Static member definitions
void* D3D9Hook::s_originalPresent = nullptr;
CameraController* D3D9Hook::s_cameraController = nullptr;
bool D3D9Hook::s_fatalErrorFlag = false;

constexpr int RESET_VTABLE_INDEX = 16;
constexpr int PRESENT_VTABLE_INDEX = 17;
using PresentFn = HRESULT (STDMETHODCALLTYPE*)(IDirect3DDevice9*, const RECT*, const RECT*, HWND, const RGNDATA*);
using ResetFn = HRESULT (STDMETHODCALLTYPE*)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);

static void* s_originalReset = nullptr;

// Cached backbuffer surface for HookedPresent's "is this the swapchain backbuffer?"
// gate. GetBackBuffer is a COM call that AddRefs every frame; storing the pointer
// once (the swap chain keeps the surface alive across frames) lets us replace two
// per-frame COM calls + Releases with one GetRenderTarget + a pointer compare.
static IDirect3DSurface9* s_cachedBackBuffer = nullptr;
static IDirect3DDevice9* s_backBufferDevice = nullptr;

// Reset fails with D3DERR_INVALIDCALL while anything outside the game holds a
// swap chain back buffer or a state block, which is what a fullscreen alt-tab
// runs into. Both are recreated on the next reticle draw.
static HRESULT STDMETHODCALLTYPE HookedReset(IDirect3DDevice9* device, D3DPRESENT_PARAMETERS* params) {
    if (D3D9Internal::g_cachedStateBlock) {
        D3D9Internal::g_cachedStateBlock->Release();
        D3D9Internal::g_cachedStateBlock = nullptr;
        D3D9Internal::g_stateBlockDevice = nullptr;
    }
    if (s_cachedBackBuffer) {
        s_cachedBackBuffer->Release();
        s_cachedBackBuffer = nullptr;
        s_backBufferDevice = nullptr;
    }
    const HRESULT result = reinterpret_cast<ResetFn>(s_originalReset)(device, params);
    if (FAILED(result)) {
        cameraunlock::logging::Line("Device Reset returned 0x%08lX", result);
    }
    return result;
}

namespace D3D9Internal {

void LogPlayerNodes() {
#if HEADTRACKING_DEBUG_LOGGING
    auto* player = RuntimePlayer();
    if (!player) return;
    auto* root = *reinterpret_cast<uint8_t**>(player + ActiveLayout().playerFirstPersonRoot);
    if (RuntimeObject(root, ActiveLayout().nodeLocalPosition + 12, ActiveRuntime().nodeVtables, "first-person NiNode"))
        HT_LOG_D3D("Player=%p first-person root=%p", player, root);
#endif
}

bool CreateHook(void* target, void* replacement, void** original) {
    MH_STATUS status = MH_CreateHook(target, replacement, original);
    if (status != MH_OK) {
        cameraunlock::logging::Line("ERROR: MH_CreateHook(%p): %s", target, MH_StatusToString(status));
        return false;
    }
    return true;
}

}  // namespace D3D9Internal

D3D9Hook& D3D9Hook::Instance() {
    static D3D9Hook instance;
    return instance;
}

D3D9Hook::D3D9Hook()
    : m_initialized(false)
    , m_enabled(true)
    , m_fatalError(false) {
    memset(m_lastError, 0, sizeof(m_lastError));
}

void D3D9Hook::SignalFatalError(const char* context) {
    cameraunlock::logging::Line("FATAL: memory access failed in %s; head tracking disabled", context);
    s_fatalErrorFlag = true;
    Instance().m_fatalError = true;
    Instance().m_enabled = false;
    HT_LOG_D3D("=== FATAL ERROR: SEH exception in %s ===", context);
    HT_LOG_D3D("=== Head tracking DISABLED - hook will no longer apply camera rotation ===");
    (void)context;
}

bool D3D9Hook::Initialize() {
    if (m_initialized) {
        return true;
    }

    RuntimePlayer();
    RuntimeCamera();
    if (RuntimeValidationFailed()) {
        strcpy_s(m_lastError, "Live camera/player identity disagrees with discovery");
        return false;
    }

    HT_LOG_D3D("D3D9Hook::Initialize - Starting (Present hook)");

    WNDCLASSEXA wc = {};
    wc.cbSize = sizeof(WNDCLASSEXA);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = GetModuleHandleA(nullptr);
    wc.lpszClassName = "HeadTrackingD3D9TempWindow";

    if (!RegisterClassExA(&wc)) {
        HT_LOG_D3D("RegisterClassExA failed (may already exist)");
    }

    HWND tempWindow = CreateWindowExA(
        0, wc.lpszClassName, "Temp",
        WS_OVERLAPPEDWINDOW,
        0, 0, 100, 100,
        nullptr, nullptr, wc.hInstance, nullptr);

    if (!tempWindow) {
        snprintf(m_lastError, sizeof(m_lastError), "Failed to create temp window");
        HT_LOG_D3D("ERROR: %s", m_lastError);
        return false;
    }

    HT_LOG_D3D("Created temp window: %p", tempWindow);

    IDirect3D9* d3d9 = Direct3DCreate9(D3D_SDK_VERSION);
    if (!d3d9) {
        DestroyWindow(tempWindow);
        UnregisterClassA(wc.lpszClassName, wc.hInstance);
        snprintf(m_lastError, sizeof(m_lastError), "Failed to create D3D9");
        HT_LOG_D3D("ERROR: %s", m_lastError);
        return false;
    }

    HT_LOG_D3D("Created D3D9 interface: %p", d3d9);

    D3DPRESENT_PARAMETERS pp = {};
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.BackBufferFormat = D3DFMT_UNKNOWN;
    pp.hDeviceWindow = tempWindow;

    IDirect3DDevice9* tempDevice = nullptr;
    HRESULT hr = d3d9->CreateDevice(
        D3DADAPTER_DEFAULT,
        D3DDEVTYPE_HAL,
        tempWindow,
        D3DCREATE_SOFTWARE_VERTEXPROCESSING,
        &pp,
        &tempDevice);

    if (FAILED(hr) || !tempDevice) {
        d3d9->Release();
        DestroyWindow(tempWindow);
        UnregisterClassA(wc.lpszClassName, wc.hInstance);
        snprintf(m_lastError, sizeof(m_lastError), "Failed to create D3D device: 0x%08X", hr);
        HT_LOG_D3D("ERROR: %s", m_lastError);
        return false;
    }

    HT_LOG_D3D("Created temp D3D device: %p", tempDevice);

    void** vtable = *reinterpret_cast<void***>(tempDevice);
    HT_LOG_D3D("Device vtable at: %p", vtable);

    void* presentAddr = vtable[PRESENT_VTABLE_INDEX];
    HT_LOG_D3D("Present at vtable[%d] = %p", PRESENT_VTABLE_INDEX, presentAddr);

    MH_STATUS status = MH_Initialize();
    bool created = status == MH_OK || status == MH_ERROR_ALREADY_INITIALIZED;
    if (!created) {
        snprintf(m_lastError, sizeof(m_lastError), "MH_Initialize: %s", MH_StatusToString(status));
    }
    if (created) {
        created = D3D9Internal::CreateHook(presentAddr, reinterpret_cast<void*>(&HookedPresent), &s_originalPresent) &&
                  D3D9Internal::CreateHook(vtable[RESET_VTABLE_INDEX], reinterpret_cast<void*>(&HookedReset),
                                           &s_originalReset) &&
                  D3D9Internal::InstallCullingHook() && D3D9Internal::InstallWeaponViewHook() &&
                  D3D9Internal::InstallSkyViewHook() && D3D9Internal::InstallRigHook();
        if (!created) {
            snprintf(m_lastError, sizeof(m_lastError), "MH_CreateHook failed (see the line above)");
        }
    }
    tempDevice->Release();
    d3d9->Release();
    DestroyWindow(tempWindow);
    UnregisterClassA(wc.lpszClassName, wc.hInstance);
    if (created) {
        status = MH_EnableHook(MH_ALL_HOOKS);
        created = status == MH_OK;
        if (!created) {
            snprintf(m_lastError, sizeof(m_lastError), "MH_EnableHook: %s", MH_StatusToString(status));
        }
    }
    if (!created) {
        // Removes every hook created so far, so the next game load can retry
        // from a clean slate instead of failing on MH_ERROR_ALREADY_CREATED.
        MH_Uninitialize();
        return false;
    }

    m_initialized = true;
    HT_LOG_D3D("D3D9Hook (Present + Culling) initialized successfully!");

    return true;
}

void D3D9Hook::ResetUICache() {
    D3D9Internal::ResetLeanClamp();
}

void D3D9Hook::SetCameraController(CameraController* controller) {
    s_cameraController = controller;
    HT_LOG_D3D("CameraController set: %p", controller);
}

void D3D9Hook::SetEnabled(bool enabled) {
    m_enabled = enabled;
    HT_LOG_D3D("D3D9Hook enabled: %d", enabled);
}

constexpr float kTeleportDistSq = 500.0f * 500.0f;  // game units squared

static void DetectTeleport() {
    auto* camera = RuntimeCamera();
    if (!camera) return;

    float* camPos = reinterpret_cast<float*>(camera + GameOffsets::CameraWorldPosition());
    static float lastCamPos[3] = {0, 0, 0};
    static bool hasLastPos = false;

    if (hasLastPos) {
        float dx = camPos[0] - lastCamPos[0];
        float dy = camPos[1] - lastCamPos[1];
        float dz = camPos[2] - lastCamPos[2];
        float distSq = dx*dx + dy*dy + dz*dz;
        if (distSq > kTeleportDistSq) {
            HT_LOG_D3D("=== TELEPORT DETECTED! dist=%.1f ===", sqrtf(distSq));
            D3D9Internal::ResetLeanClamp();
            D3D9Internal::g_crosshairDisabled = false;
        }
    }

    lastCamPos[0] = camPos[0];
    lastCamPos[1] = camPos[1];
    lastCamPos[2] = camPos[2];
    hasLastPos = true;
}

HRESULT STDMETHODCALLTYPE D3D9Hook::HookedPresent(IDirect3DDevice9* device, const RECT* source,
    const RECT* destination, HWND window, const RGNDATA* dirtyRegion) {
    if (IsFatalErrorSet() || RuntimeValidationFailed()) {
        // A failure part way through a frame can leave this frame's head pose
        // in the camera; hand it back clean before going quiet.
        D3D9Internal::RestoreCamera();
        const auto original = reinterpret_cast<PresentFn>(s_originalPresent);
        return original(device, source, destination, window, dirtyRegion);
    }


    CenterWindowOnce(device);

    const bool isPaused = D3D9Internal::IsGamePaused();
    const bool isAiming = D3D9Internal::IsPlayerAiming();
    const bool controllerActive = s_cameraController && s_cameraController->IsActive();
    D3D9Hook& hook = Instance();

    if (hook.m_enabled && s_cameraController) {
        DetectTeleport();

        if (controllerActive) {
            if (isPaused || isAiming) {
                if (D3D9Internal::g_crosshairDisabled) {
                    D3D9Internal::SetCrosshairTileVisible(!isAiming && !isPaused);
                    D3D9Internal::g_crosshairDisabled = false;
                    HT_LOG_D3D("Crosshair: showing stock (paused=%d, ADS=%d)", isPaused, isAiming);
                }
            } else {
                if (!D3D9Internal::g_crosshairDisabled) {
                    D3D9Internal::SetCrosshairTileVisible(false);
                    D3D9Internal::g_crosshairDisabled = true;
                    HT_LOG_D3D("Crosshair: hiding stock, drawing custom");
                }

                D3DVIEWPORT9 vp;
                if (SUCCEEDED(device->GetViewport(&vp)) && vp.Width > 800 && vp.Height > 600) {
                    // Cache the backbuffer pointer the first time we see this device;
                    // GetBackBuffer is one of the more expensive D3D9 COM calls and
                    // the swap chain holds the surface alive across frames, so a
                    // single AddRef-and-stash is enough. Subsequent frames only need
                    // GetRenderTarget + a pointer compare.
                    if (s_backBufferDevice != device) {
                        if (s_cachedBackBuffer) {
                            s_cachedBackBuffer->Release();
                            s_cachedBackBuffer = nullptr;
                        }
                        if (SUCCEEDED(device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &s_cachedBackBuffer))) {
                            s_backBufferDevice = device;
                        }
                    }

                    if (s_cachedBackBuffer) {
                        IDirect3DSurface9* renderTarget = nullptr;
                        if (SUCCEEDED(device->GetRenderTarget(0, &renderTarget)) && renderTarget) {
                            bool isBackBuffer = (renderTarget == s_cachedBackBuffer);
                            renderTarget->Release();
                            if (isBackBuffer) {
                                const HRESULT begin = device->BeginScene();
                                if (SUCCEEDED(begin)) {
                                    D3D9Internal::DrawAimCrosshair(device, vp);
                                    const HRESULT end = device->EndScene();
                                    if (FAILED(end)) {
                                        cameraunlock::logging::Line("ERROR: reticle EndScene failed: 0x%08lX", end);
                                        SignalFatalError("reticle EndScene");
                                    }
                                } else if (begin != D3DERR_DEVICELOST) {
                                    cameraunlock::logging::Line("ERROR: reticle BeginScene failed: 0x%08lX", begin);
                                    SignalFatalError("reticle BeginScene");
                                }
                            }
                        }
                    }
                }
            }
        } else if (D3D9Internal::g_crosshairDisabled) {
            D3D9Internal::SetCrosshairTileVisible(!isAiming && !isPaused);
            D3D9Internal::g_crosshairDisabled = false;
        }
    }

    // EndScene also runs between world passes; only Present ends the displayed frame.
    D3D9Internal::RestoreCamera();
    HeadTracking::Proxy::OnFrame();

    const auto original = reinterpret_cast<PresentFn>(s_originalPresent);
    return original(device, source, destination, window, dirtyRegion);
}

}  // namespace HeadTracking
