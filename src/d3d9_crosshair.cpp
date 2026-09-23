// D3D9 Crosshair handling - UI tile manipulation and custom crosshair drawing
// Part of the D3D9 hook subsystem

#include "d3d9_internal.h"
#include "d3d9_hook.h"
#include "debug_log.h"
#include "build_profile.h"
#include "nvse_abi/GameAPI.h"
#include <cameraunlock/logging/file_log.h>

#include <cstring>
#include <cmath>

namespace HeadTracking {
namespace D3D9Internal {

// Global state definitions (declared extern in d3d9_internal.h)
float g_mainCameraTanFovX = 0.0f;
float g_mainCameraTanFovY = 0.0f;
IDirect3DStateBlock9* g_cachedStateBlock = nullptr;
IDirect3DDevice9* g_stateBlockDevice = nullptr;
bool g_crosshairDisabled = false;
bool g_reticleEnabled = true;

// Tile value IDs (from xNVSE GameTiles.h)
constexpr uint32_t kTileValue_visible = 0x0FA3;

// Tile::SetFloatValue function pointer (address from xNVSE)
typedef void (__thiscall *TileSetFloatValueFn)(void* tile, uint32_t valueID, float value, bool propagate);
bool IsGamePaused() {
    void* interfaceMgr = *reinterpret_cast<void**>(ActiveProfile().interfaceManager);
    if (!interfaceMgr) return false;

    __try {
        uint8_t* base = reinterpret_cast<uint8_t*>(interfaceMgr);
        uint32_t flags0C = *reinterpret_cast<uint32_t*>(base + 0x0C);

#if HEADTRACKING_DEBUG_LOGGING
        static int logCount = 0;
        if (logCount < 20) {
            HT_LOG_D3D("IsGamePaused: flags0C=0x%08X", flags0C);
            logCount++;
        }
#endif

        return (flags0C & 2) != 0;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        HT_LOG_D3D("FATAL: SEH exception in IsGamePaused");
        D3D9Hook::SignalFatalError("IsGamePaused");
        return false;
    }
}

bool IsPlayerAiming() {
    auto* player = reinterpret_cast<uint8_t*>(PlayerCharacter::GetSingleton());
    if (!player) return false;
    auto* process = *reinterpret_cast<uint8_t**>(player + 0x68);
    return process && process[0x228] != 0;
}

void SetCrosshairTileVisible(bool visible) {
    auto* hud = *reinterpret_cast<uint8_t**>(ActiveProfile().hudMainMenu);
    if (!hud) return;
    void* tile = *reinterpret_cast<void**>(hud + 0x12C);
    if (!tile) return;
    reinterpret_cast<TileSetFloatValueFn>(ActiveProfile().tileSetFloatValue)(
        tile, kTileValue_visible, visible ? 1.0f : 0.0f, true);
}

void DrawAimCrosshair(IDirect3DDevice9* device, const D3DVIEWPORT9& vp) {
#if HEADTRACKING_DEBUG_LOGGING
    static int drawCount = 0;
    drawCount++;
    bool shouldLog = (drawCount <= 20 || drawCount % 500 == 0);
#endif

    float tanHalfFovX = g_mainCameraTanFovX;
    float tanHalfFovY = g_mainCameraTanFovY;

    // Body aim direction in rotated camera frame - pre-computed in culling hook
    // from the actual rotation matrices. This is model-agnostic: any rotation
    // model (sequential Euler, horizon-locked, etc.) produces correct results.
    float bDepth = g_bodyAimInCamera[0];
    float bUp    = g_bodyAimInCamera[1];
    float bRight = g_bodyAimInCamera[2];

    if (!g_aimProjectionValid || !std::isfinite(bDepth) || bDepth <= 0.0f) return;

    float normalizedX = bRight / (bDepth * tanHalfFovX);
    float normalizedY = -bUp / (bDepth * tanHalfFovY);

    if (!std::isfinite(normalizedX) || !std::isfinite(normalizedY) ||
        std::fabs(normalizedX) > 1.0f || std::fabs(normalizedY) > 1.0f) return;

    float screenX = vp.X + vp.Width / 2.0f + normalizedX * (vp.Width / 2.0f);
    float screenY = vp.Y + vp.Height / 2.0f + normalizedY * (vp.Height / 2.0f);

#if HEADTRACKING_DEBUG_LOGGING
    if (shouldLog) {
        HT_LOG_D3D("DrawCrosshair %d: bodyAim=(%.3f,%.3f,%.3f) screen(%.1f,%.1f)",
               drawCount, bDepth, bUp, bRight, screenX, screenY);
    }
#endif

    if (g_stateBlockDevice != device) {
        if (g_cachedStateBlock) {
            g_cachedStateBlock->Release();
            g_cachedStateBlock = nullptr;
        }
        if (FAILED(device->CreateStateBlock(D3DSBT_ALL, &g_cachedStateBlock))) {
            HT_LOG_D3D("DrawCrosshair: Failed to create state block");
            return;
        }
        g_stateBlockDevice = device;
        HT_LOG_D3D("DrawCrosshair: Created cached state block for device %p", device);
    }

    if (FAILED(g_cachedStateBlock->Capture())) {
        HT_LOG_D3D("DrawCrosshair: Failed to capture state");
        return;
    }

    struct Vertex {
        float x, y, z, rhw;
        DWORD color;
    };

    // Build a bracket arm as two triangles forming a thick line from (baseX,baseY) to (tipX,tipY).
    // thickOffset shifts the second edge of the quad (positive = rightward, negative = leftward).
    auto BuildBracketArm = [](Vertex* out, float baseX, float baseY,
                               float tipX, float tipY, float thickOffset, DWORD c) {
        out[0] = { baseX,               baseY, 0.0f, 1.0f, c };
        out[1] = { baseX + thickOffset, baseY, 0.0f, 1.0f, c };
        out[2] = { tipX,                tipY,  0.0f, 1.0f, c };
        out[3] = { baseX + thickOffset, baseY, 0.0f, 1.0f, c };
        out[4] = { tipX + thickOffset,  tipY,  0.0f, 1.0f, c };
        out[5] = { tipX,                tipY,  0.0f, 1.0f, c };
    };

    const DWORD color = 0xFFD4A017;
    const float gap = 4.0f;
    const float bracketLen = 8.0f;
    const float thick = 4.0f;

    float lx = screenX - gap - bracketLen;
    float rx = screenX + gap + bracketLen;

    Vertex allVerts[24];
    BuildBracketArm(&allVerts[0], lx, screenY - bracketLen, screenX - gap, screenY, thick, color);
    BuildBracketArm(&allVerts[6], lx, screenY + bracketLen, screenX - gap, screenY, thick, color);
    BuildBracketArm(&allVerts[12], rx, screenY - bracketLen, screenX + gap, screenY, -thick, color);
    BuildBracketArm(&allVerts[18], rx, screenY + bracketLen, screenX + gap, screenY, -thick, color);

    device->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
    device->SetTexture(0, nullptr);
    device->SetRenderState(D3DRS_LIGHTING, FALSE);
    device->SetRenderState(D3DRS_ZENABLE, FALSE);
    device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    device->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
    device->SetRenderState(D3DRS_FOGENABLE, FALSE);
    device->SetRenderState(D3DRS_STENCILENABLE, FALSE);
    device->SetRenderState(D3DRS_CLIPPING, FALSE);
    device->SetPixelShader(nullptr);
    device->SetVertexShader(nullptr);
    device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 8, allVerts, sizeof(Vertex));

    g_cachedStateBlock->Apply();
}

}  // namespace D3D9Internal
}  // namespace HeadTracking
