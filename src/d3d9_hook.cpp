// D3D9 Hook core - EndScene/BeginScene hook infrastructure
// Crosshair handling is in d3d9_crosshair.cpp
// Culling hook is in d3d9_culling.cpp

#include "d3d9_hook.h"
#include "d3d9_internal.h"
#include "camera_controller.h"
#include "udp_receiver.h"
#include "tracking_data.h"
#include "game_offsets.h"
#include "debug_log.h"
#include "window_centering.h"

#include <cstring>
#include <cmath>

#pragma comment(lib, "d3d9.lib")

namespace HeadTracking {

// Static member definitions
void* D3D9Hook::s_originalEndScene = nullptr;
CameraController* D3D9Hook::s_cameraController = nullptr;
UdpReceiver* D3D9Hook::s_udpReceiver = nullptr;
bool D3D9Hook::s_fatalErrorFlag = false;

// D3D9 vtable indices
constexpr int BEGINSCENE_VTABLE_INDEX = 41;
constexpr int ENDSCENE_VTABLE_INDEX = 42;
// Function pointer types
typedef HRESULT(STDMETHODCALLTYPE* BeginSceneFn)(IDirect3DDevice9* device);
typedef HRESULT(STDMETHODCALLTYPE* EndSceneFn)(IDirect3DDevice9* device);

// BeginScene hook for frame timing
static void* s_originalBeginScene = nullptr;

// High-precision frame tracking
static LARGE_INTEGER s_lastFrameTime = {0};
static LARGE_INTEGER s_perfFreq = {0};
static double s_perfFreqRecipMs = 0.0;
static bool s_perfInitialized = false;

// Forward declarations
HRESULT STDMETHODCALLTYPE HookedBeginScene(IDirect3DDevice9* device);

// NiTArray structure (from xNVSE)
struct NiTArray {
    void* vtable;
    void** m_data;
    uint16_t m_capacity;
    uint16_t m_firstFree;
    uint16_t m_numObjs;
    uint16_t m_growSize;
};

#if HEADTRACKING_DEBUG_LOGGING
static const char* GetNiNodeName(uint8_t* node) {
    if (!node) return "<null>";

    __try {
        const char* name = *reinterpret_cast<const char**>(node + 0x08);
        if (name && name[0] != '\0') {
            if (name[0] >= 32 && name[0] <= 126) {
                return name;
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        HT_LOG_D3D("FATAL: SEH exception in GetNiNodeName for node at %p", node);
        D3D9Hook::SignalFatalError("GetNiNodeName");
        return "<error:fatal>";
    }
    return "<unnamed>";
}

static void LogNiNodeHierarchy(uint8_t* node, int depth, int maxDepth = 4) {
    if (!node || depth > maxDepth) {
        return;
    }

    __try {
        const char* name = GetNiNodeName(node);

        char indent[32] = "";
        for (int i = 0; i < depth && i < 15; i++) {
            indent[i*2] = ' ';
            indent[i*2+1] = ' ';
        }
        indent[depth*2] = '\0';

        HT_LOG_D3D("%sNode: %s (addr=%p)", indent, name, node);

        NiTArray* children = reinterpret_cast<NiTArray*>(node + 0x9C);

        HT_LOG_D3D("%s  Children array: vtbl=%p data=%p numObjs=%d", indent,
               children->vtable, children->m_data, children->m_numObjs);

        if (children && children->m_data && children->m_numObjs > 0 && children->m_numObjs < 100) {
            for (uint16_t i = 0; i < children->m_numObjs; i++) {
                uint8_t* child = reinterpret_cast<uint8_t*>(children->m_data[i]);
                if (child) {
                    LogNiNodeHierarchy(child, depth + 1, maxDepth);
                }
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        HT_LOG_D3D("FATAL: SEH exception traversing node at %p", node);
        D3D9Hook::SignalFatalError("LogNiNodeHierarchy");
    }
}
#endif

namespace D3D9Internal {

void LogPlayerNodes() {
#if HEADTRACKING_DEBUG_LOGGING
    static bool logged = false;
    if (logged) return;

    uint8_t* player = *reinterpret_cast<uint8_t**>(GameOffsets::kPlayerBase);
    if (!player) {
        HT_LOG_D3D("LogPlayerNodes: No player");
        return;
    }

    __try {
        HT_LOG_D3D("=== Player Node Hierarchy ===");
        HT_LOG_D3D("Player at: %p", player);

        uint8_t* renderState = *reinterpret_cast<uint8_t**>(player + 0x064);
        if (renderState) {
            uint8_t* niNode = *reinterpret_cast<uint8_t**>(renderState + 0x10);
            HT_LOG_D3D("RenderState NiNode at: %p", niNode);
            if (niNode) {
                HT_LOG_D3D("--- RenderState->niNode hierarchy ---");
                LogNiNodeHierarchy(niNode, 0, 5);
            }
        }

        uint8_t* playerNode = *reinterpret_cast<uint8_t**>(player + 0x694);
        if (playerNode) {
            HT_LOG_D3D("PlayerNode (1st person) at: %p", playerNode);
            HT_LOG_D3D("--- playerNode hierarchy ---");
            LogNiNodeHierarchy(playerNode, 0, 5);
        }

        logged = true;
        HT_LOG_D3D("=== End Player Node Hierarchy ===");

    } __except(EXCEPTION_EXECUTE_HANDLER) {
        HT_LOG_D3D("FATAL: SEH exception in LogPlayerNodes");
        D3D9Hook::SignalFatalError("LogPlayerNodes");
    }
#endif
}

void* InstallJmpHook(void* targetAddr, void* hookFn, int hookSize) {
    void* trampoline = VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!trampoline) {
        HT_LOG_D3D("InstallJmpHook: Failed to allocate trampoline");
        return nullptr;
    }

    // Copy original bytes into trampoline, then append JMP back to (targetAddr + hookSize)
    uint8_t* trampolineBytes = static_cast<uint8_t*>(trampoline);
    memcpy(trampolineBytes, targetAddr, hookSize);
    trampolineBytes[hookSize] = 0xE9;
    uintptr_t jumpBackTarget = reinterpret_cast<uintptr_t>(targetAddr) + hookSize;
    int32_t jumpBackRel = static_cast<int32_t>(jumpBackTarget - reinterpret_cast<uintptr_t>(trampolineBytes + hookSize + 5));
    memcpy(trampolineBytes + hookSize + 1, &jumpBackRel, 4);

    // Write JMP from targetAddr to hookFn, NOP-pad the rest
    uint8_t* target = static_cast<uint8_t*>(targetAddr);
    target[0] = 0xE9;
    uintptr_t hookTarget = reinterpret_cast<uintptr_t>(hookFn);
    int32_t hookRel = static_cast<int32_t>(hookTarget - (reinterpret_cast<uintptr_t>(targetAddr) + 5));
    memcpy(target + 1, &hookRel, 4);
    for (int i = 5; i < hookSize; i++) {
        target[i] = 0x90;
    }

    HT_LOG_D3D("InstallJmpHook: target=%p hook=%p trampoline=%p hookSize=%d", targetAddr, hookFn, trampoline, hookSize);
    return trampoline;
}

}  // namespace D3D9Internal

D3D9Hook& D3D9Hook::Instance() {
    static D3D9Hook instance;
    return instance;
}

D3D9Hook::D3D9Hook()
    : m_initialized(false)
    , m_hooked(false)
    , m_enabled(true)
    , m_fatalError(false)
    , m_deviceVTable(nullptr) {
    memset(m_lastError, 0, sizeof(m_lastError));
}

void D3D9Hook::SignalFatalError(const char* context) {
    s_fatalErrorFlag = true;
    Instance().m_fatalError = true;
    Instance().m_enabled = false;
    HT_LOG_D3D("=== FATAL ERROR: SEH exception in %s ===", context);
    HT_LOG_D3D("=== Head tracking DISABLED - hook will no longer apply camera rotation ===");
    (void)context;
}

D3D9Hook::~D3D9Hook() {
    Shutdown();
}

bool D3D9Hook::Initialize() {
    if (m_initialized) {
        return true;
    }

    HT_LOG_D3D("D3D9Hook::Initialize - Starting (EndScene hook)");

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

    void* endSceneAddr = vtable[ENDSCENE_VTABLE_INDEX];
    HT_LOG_D3D("EndScene at vtable[%d] = %p", ENDSCENE_VTABLE_INDEX, endSceneAddr);

    s_originalEndScene = endSceneAddr;

    DWORD oldProtect;
    if (!VirtualProtect(endSceneAddr, 16, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        tempDevice->Release();
        d3d9->Release();
        DestroyWindow(tempWindow);
        UnregisterClassA(wc.lpszClassName, wc.hInstance);
        snprintf(m_lastError, sizeof(m_lastError), "VirtualProtect failed");
        HT_LOG_D3D("ERROR: %s", m_lastError);
        return false;
    }

    constexpr int SCENE_HOOK_SIZE = 7;

    s_originalEndScene = D3D9Internal::InstallJmpHook(endSceneAddr, reinterpret_cast<void*>(&HookedEndScene), SCENE_HOOK_SIZE);
    if (!s_originalEndScene) {
        VirtualProtect(endSceneAddr, 16, oldProtect, &oldProtect);
        tempDevice->Release();
        d3d9->Release();
        DestroyWindow(tempWindow);
        UnregisterClassA(wc.lpszClassName, wc.hInstance);
        HT_LOG_D3D("ERROR: Failed to install EndScene hook");
        return false;
    }

    VirtualProtect(endSceneAddr, 16, oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), endSceneAddr, 16);
    HT_LOG_D3D("EndScene hook installed at %p", endSceneAddr);

    // Hook BeginScene for frame timing
    void* beginSceneAddr = vtable[BEGINSCENE_VTABLE_INDEX];
    HT_LOG_D3D("BeginScene at vtable[%d] = %p", BEGINSCENE_VTABLE_INDEX, beginSceneAddr);

    if (!VirtualProtect(beginSceneAddr, 16, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        HT_LOG_D3D("WARNING: Could not hook BeginScene - VirtualProtect failed");
    } else {
        s_originalBeginScene = D3D9Internal::InstallJmpHook(beginSceneAddr, reinterpret_cast<void*>(&HookedBeginScene), SCENE_HOOK_SIZE);
        VirtualProtect(beginSceneAddr, 16, oldProtect, &oldProtect);
        FlushInstructionCache(GetCurrentProcess(), beginSceneAddr, 16);
        if (s_originalBeginScene) {
            HT_LOG_D3D("BeginScene hook installed at %p", beginSceneAddr);
        }
    }

    m_hooked = true;
    m_deviceVTable = vtable;

    tempDevice->Release();
    d3d9->Release();
    DestroyWindow(tempWindow);
    UnregisterClassA(wc.lpszClassName, wc.hInstance);

    if (!D3D9Internal::InstallCullingHook()) {
        HT_LOG_D3D("WARNING: Culling hook failed - objects may pop in at screen edges");
    }

    m_initialized = true;
    HT_LOG_D3D("D3D9Hook (EndScene + BeginScene + Culling) initialized successfully!");

    return true;
}

// Cached backbuffer surface for HookedEndScene's "is this the swapchain backbuffer?"
// gate. GetBackBuffer is a COM call that AddRefs every frame; storing the pointer
// once (the swap chain keeps the surface alive across frames) lets us replace two
// per-frame COM calls + Releases with one GetRenderTarget + a pointer compare.
// Invalidated on Shutdown and when the device changes.
static IDirect3DSurface9* s_cachedBackBuffer = nullptr;
static IDirect3DDevice9* s_backBufferDevice = nullptr;

void D3D9Hook::Shutdown() {
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

    if (m_hooked) {
        HT_LOG_D3D("D3D9Hook shutdown (hook remains in place for stability)");
    }
    m_initialized = false;
    m_hooked = false;
}

void D3D9Hook::ResetUICache() {
    D3D9Internal::ResetCrosshairCache();
    HT_LOG_D3D("UI cache reset - crosshair tile cleared (camera baseline preserved)");
}

void D3D9Hook::SetCameraController(CameraController* controller) {
    s_cameraController = controller;
    HT_LOG_D3D("CameraController set: %p", controller);
}

void D3D9Hook::SetUdpReceiver(UdpReceiver* receiver) {
    s_udpReceiver = receiver;
    HT_LOG_D3D("UdpReceiver set: %p", receiver);
}

void D3D9Hook::SetEnabled(bool enabled) {
    m_enabled = enabled;
    HT_LOG_D3D("D3D9Hook enabled: %d", enabled);
}

HRESULT STDMETHODCALLTYPE HookedBeginScene(IDirect3DDevice9* device) {
    if (!s_perfInitialized) {
        QueryPerformanceFrequency(&s_perfFreq);
        s_perfFreqRecipMs = 1000.0 / static_cast<double>(s_perfFreq.QuadPart);
        QueryPerformanceCounter(&s_lastFrameTime);
        s_perfInitialized = true;
    }

    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    double elapsedMs = static_cast<double>(now.QuadPart - s_lastFrameTime.QuadPart) * s_perfFreqRecipMs;

    if (elapsedMs >= 6.0) {
        s_lastFrameTime = now;
    }

    BeginSceneFn original = reinterpret_cast<BeginSceneFn>(s_originalBeginScene);
    return original(device);
}

constexpr float kTeleportDistSq = 500.0f * 500.0f;  // game units squared

static void DetectTeleport() {
    uint8_t** ppSceneGraph = reinterpret_cast<uint8_t**>(GameOffsets::kSceneGraphBase);
    if (!ppSceneGraph || !*ppSceneGraph) {
        return;
    }
    uint8_t* sceneGraph = *ppSceneGraph;
    uint8_t* camera = *reinterpret_cast<uint8_t**>(sceneGraph + GameOffsets::kSceneGraphCamera);
    if (!camera) {
        return;
    }

    float* camPos = reinterpret_cast<float*>(camera + GameOffsets::kCameraWorldPosition);
    static float lastCamPos[3] = {0, 0, 0};
    static bool hasLastPos = false;

    if (hasLastPos) {
        float dx = camPos[0] - lastCamPos[0];
        float dy = camPos[1] - lastCamPos[1];
        float dz = camPos[2] - lastCamPos[2];
        float distSq = dx*dx + dy*dy + dz*dz;
        if (distSq > kTeleportDistSq) {
            HT_LOG_D3D("=== TELEPORT DETECTED! dist=%.1f ===", sqrtf(distSq));
            D3D9Internal::ResetCrosshairCache();
            D3D9Internal::g_crosshairDisabled = false;
        }
    }

    lastCamPos[0] = camPos[0];
    lastCamPos[1] = camPos[1];
    lastCamPos[2] = camPos[2];
    hasLastPos = true;
}

HRESULT STDMETHODCALLTYPE D3D9Hook::HookedEndScene(IDirect3DDevice9* device) {
    if (IsFatalErrorSet()) {
        EndSceneFn original = reinterpret_cast<EndSceneFn>(s_originalEndScene);
        return original(device);
    }

    CenterWindowOnce(device);

    static bool wasPaused = false;
    static bool wasAiming = false;
#if HEADTRACKING_DEBUG_LOGGING
    static int callCount = 0;
    callCount++;
    bool shouldLog = (callCount <= 5 || callCount % 300 == 0);
#endif

    bool isPaused = D3D9Internal::IsGamePaused();
    bool isAiming = D3D9Internal::IsPlayerAiming();

#if HEADTRACKING_DEBUG_LOGGING
    if (isAiming != wasAiming) {
        float fovRatio = (D3D9Internal::g_normalFovX > 0.1f) ? (D3D9Internal::g_mainCameraTanFovX / D3D9Internal::g_normalFovX) : 0.0f;
        HT_LOG_D3D("ADS state changed: wasAiming=%d isAiming=%d (FOV ratio=%.2f, current=%.3f, normal=%.3f)",
               wasAiming, isAiming, fovRatio, D3D9Internal::g_mainCameraTanFovX, D3D9Internal::g_normalFovX);
    }
#endif

    // Hoist the two trivial getters; they're called again in the main draw block
    // below, so reading them into locals halves the member-access traffic.
    bool controllerActive = s_cameraController && s_cameraController->IsDecoupled() && s_cameraController->IsActive();

    if (isAiming && controllerActive) {
        double yawOffset = s_cameraController->GetCurrentYawOffset();
        double pitchOffset = s_cameraController->GetCurrentPitchOffset();

        uint8_t* player = *reinterpret_cast<uint8_t**>(GameOffsets::kPlayerBase);
        if (player) {
            float* pRotZ = reinterpret_cast<float*>(player + GameOffsets::kPlayerRotZ);
            float yawRad = static_cast<float>(yawOffset * kDegToRadF);
            *pRotZ += yawRad;

            float* pRotX = reinterpret_cast<float*>(player + GameOffsets::kPlayerRotX);
            float pitchRad = static_cast<float>(-pitchOffset * kDegToRadF);
            *pRotX += pitchRad;
        }

        if (s_udpReceiver && s_udpReceiver->IsConnected()) {
            const TrackingData& data = s_udpReceiver->GetLatestData();
            if (data.valid) {
                s_cameraController->RecenterYawPitchOnly(data);
            }
        }

        if (!wasAiming) {
            HT_LOG_D3D("ADS TRIGGERED - body follows head (yaw + pitch), camera keeps roll");
        }
    }
    wasAiming = isAiming;

    if (isPaused != wasPaused) {
        HT_LOG_D3D("Pause state changed: wasPaused=%d isPaused=%d", wasPaused, isPaused);
    }

    if (wasPaused && !isPaused) {
        HT_LOG_D3D("UNPAUSE detected! controller=%p udpReceiver=%p", s_cameraController, s_udpReceiver);
        if (s_cameraController && s_udpReceiver) {
            bool connected = s_udpReceiver->IsConnected();
            HT_LOG_D3D("  udpReceiver connected=%d", connected);
            if (connected) {
                const TrackingData& currentData = s_udpReceiver->GetLatestData();
                HT_LOG_D3D("  trackingData valid=%d yaw=%.2f pitch=%.2f", currentData.valid, currentData.yaw, currentData.pitch);
                if (currentData.valid) {
                    s_cameraController->Recenter(currentData);
                    HT_LOG_D3D("  Recentered head tracking on unpause!");
                }
            }
        }
    }
    wasPaused = isPaused;

#if HEADTRACKING_DEBUG_LOGGING
    if (callCount == 100) {
        D3D9Internal::LogPlayerNodes();
    }
#endif

    D3D9Hook& hook = Instance();

#if HEADTRACKING_DEBUG_LOGGING
    if (shouldLog) {
        HT_LOG_D3D("EndScene call %d, enabled=%d, controller=%p, isPaused=%d, isAiming=%d",
               callCount, hook.m_enabled, s_cameraController, isPaused, isAiming);
        if (s_cameraController) {
            HT_LOG_D3D("  IsDecoupled=%d, IsActive=%d, yaw=%.2f, pitch=%.2f, crosshairDisabled=%d",
                   s_cameraController->IsDecoupled(), s_cameraController->IsActive(),
                   s_cameraController->GetCurrentYawOffset(), s_cameraController->GetCurrentPitchOffset(),
                   D3D9Internal::g_crosshairDisabled);
        }
    }
#endif

    if (hook.m_enabled && s_cameraController) {
        DetectTeleport();

        if (controllerActive) {
            if (isPaused || isAiming) {
                if (D3D9Internal::g_crosshairDisabled) {
                    D3D9Internal::SetCrosshairTileVisible(true);
                    D3D9Internal::g_crosshairDisabled = false;
                    HT_LOG_D3D("Crosshair: showing stock (paused=%d, ADS=%d)", isPaused, isAiming);
                }
            } else if (D3D9Internal::g_reticleEnabled) {
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
                                D3D9Internal::DrawAimCrosshair(device, vp);
                            }
                        }
                    }
                }
            } else {
                // Reticle disabled by user — restore stock crosshair
                if (D3D9Internal::g_crosshairDisabled) {
                    D3D9Internal::SetCrosshairTileVisible(true);
                    D3D9Internal::g_crosshairDisabled = false;
                    HT_LOG_D3D("Crosshair: reticle user-disabled, restoring stock");
                }
            }
        } else if (D3D9Internal::g_crosshairDisabled) {
            D3D9Internal::SetCrosshairTileVisible(true);
            D3D9Internal::g_crosshairDisabled = false;
        }
    }

    EndSceneFn original = reinterpret_cast<EndSceneFn>(s_originalEndScene);
    return original(device);
}

}  // namespace HeadTracking
