// D3D9 Crosshair handling - UI tile manipulation and custom crosshair drawing
// Part of the D3D9 hook subsystem

#include "d3d9_internal.h"
#include "d3d9_hook.h"
#include "debug_log.h"

#include <cstring>
#include <cmath>

namespace HeadTracking {
namespace D3D9Internal {

// FOV detection thresholds
constexpr float kNormalFovMin = 0.95f;
constexpr float kNormalFovMax = 1.1f;
constexpr float kAdsDetectionThreshold = 0.96f;

// Global state definitions (declared extern in d3d9_internal.h)
float g_mainCameraTanFovX = 1.023f;
float g_mainCameraTanFovY = 0.575f;
float g_normalFovX = 0.0f;
bool g_normalFovCaptured = false;
IDirect3DStateBlock9* g_cachedStateBlock = nullptr;
IDirect3DDevice9* g_stateBlockDevice = nullptr;
void* g_crosshairTile = nullptr;
bool g_triedFindCrosshair = false;
bool g_crosshairDisabled = false;
bool g_reticleEnabled = true;

// Tile value IDs (from xNVSE GameTiles.h)
constexpr uint32_t kTileValue_visible = 0x0FA3;

// Tile::SetFloatValue function pointer (address from xNVSE)
typedef void (__thiscall *TileSetFloatValueFn)(void* tile, uint32_t valueID, float value, bool propagate);
static TileSetFloatValueFn s_TileSetFloatValue = reinterpret_cast<TileSetFloatValueFn>(0x00A012D0);

// InterfaceManager singleton address
static void** s_pInterfaceManager = reinterpret_cast<void**>(0x011D8A80);

// BSStringT structure at offset 0x20 in Tile
struct BSStringT {
    char* str;
    uint16_t len;
    uint16_t alloc;
};

// tList::Node structure for child list
struct TileChildNode {
    TileChildNode* next;
    TileChildNode* prev;
    void* child;
};

// tList head structure at offset 0x04 in Tile
struct TileChildList {
    TileChildNode* head;
    TileChildNode* tail;
    uint32_t count;
};

bool IsGamePaused() {
    void* interfaceMgr = *s_pInterfaceManager;
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
    // Capture normal FOV (first stable main camera FOV)
    if (!g_normalFovCaptured && g_mainCameraTanFovX > kNormalFovMin && g_mainCameraTanFovX < kNormalFovMax) {
        g_normalFovX = g_mainCameraTanFovX;
        g_normalFovCaptured = true;
        HT_LOG_D3D("Captured normal FOV: %.4f", g_normalFovX);
    }

    if (!g_normalFovCaptured || g_normalFovX < 0.1f) {
        return false;
    }

    float fovRatio = g_mainCameraTanFovX / g_normalFovX;
    return (fovRatio < kAdsDetectionThreshold);
}

void ResetCrosshairCache() {
    g_crosshairTile = nullptr;
    g_triedFindCrosshair = false;
    HT_LOG_D3D("UI cache reset - crosshair tile cleared");
}

// Get tile name (String at offset 0x20 in Tile)
static const char* GetTileName(void* tile) {
    if (!tile) return "<null>";
    __try {
        BSStringT* nameStr = reinterpret_cast<BSStringT*>(reinterpret_cast<uint8_t*>(tile) + 0x20);
        if (nameStr && nameStr->str && nameStr->len > 0 && nameStr->len < 256) {
            if (nameStr->str[0] >= 32 && nameStr->str[0] <= 126) {
                return nameStr->str;
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        HT_LOG_D3D("SEH exception in GetTileName for tile at %p (invalid pointer)", tile);
        return "<invalid>";
    }
    return "<unnamed>";
}

// Find a tile by name in the hierarchy (recursive search)
static void* FindTileByName(void* tile, const char* targetName, int maxDepth = 5) {
    if (!tile || maxDepth <= 0) {
        return nullptr;
    }

    __try {
        const char* name = GetTileName(tile);
        if (name && strcmp(name, "<unnamed>") != 0 && strstr(name, targetName) != nullptr) {
            return tile;
        }

        TileChildList* childList = reinterpret_cast<TileChildList*>(reinterpret_cast<uint8_t*>(tile) + 0x04);
        if (childList && childList->head && childList->count > 0 && childList->count < 200) {
            TileChildNode* node = childList->head;
            for (uint32_t i = 0; i < childList->count && node; i++) {
                void* childTile = node->child;
                if (childTile) {
                    void* found = FindTileByName(childTile, targetName, maxDepth - 1);
                    if (found) return found;
                }
                node = node->next;
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        HT_LOG_D3D("FATAL: SEH exception in FindTileByName for '%s'", targetName);
        D3D9Hook::SignalFatalError("FindTileByName");
    }
    return nullptr;
}

#if HEADTRACKING_DEBUG_LOGGING
static void LogTileHierarchy(void* tile, int depth = 0, int maxDepth = 3) {
    if (!tile || depth > maxDepth) {
        return;
    }

    __try {
        const char* name = GetTileName(tile);
        char indent[32] = "";
        for (int i = 0; i < depth && i < 15; i++) {
            indent[i*2] = ' ';
            indent[i*2+1] = ' ';
        }
        indent[depth*2] = '\0';

        HT_LOG_D3D("%sTile: %s (addr=%p)", indent, name, tile);

        TileChildList* childList = reinterpret_cast<TileChildList*>(reinterpret_cast<uint8_t*>(tile) + 0x04);
        if (childList && childList->head && childList->count > 0 && childList->count < 200) {
            TileChildNode* node = childList->head;
            for (uint32_t i = 0; i < childList->count && node; i++) {
                void* childTile = node->child;
                if (childTile) {
                    LogTileHierarchy(childTile, depth + 1, maxDepth);
                }
                node = node->next;
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        HT_LOG_D3D("SEH exception in LogTileHierarchy at depth %d (invalid structure)", depth);
    }
}
#endif

void SetCrosshairTileVisible(bool visible) {
    if (!g_triedFindCrosshair) {
        g_triedFindCrosshair = true;

        void* interfaceMgr = *s_pInterfaceManager;
        if (!interfaceMgr) {
            HT_LOG_D3D("SetCrosshairTileVisible: No InterfaceManager");
            return;
        }

        HT_LOG_D3D("InterfaceManager at %p", interfaceMgr);

#if HEADTRACKING_DEBUG_LOGGING
        uint32_t offsets[] = {0x9C, 0xBC, 0xCC};
        const char* offsetNames[] = {"menuRoot(0x9C)", "activeTileAlt(0xBC)", "activeTile(0xCC)"};

        for (int i = 0; i < 3; i++) {
            void* tile = *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(interfaceMgr) + offsets[i]);
            if (tile) {
                HT_LOG_D3D("=== Tile at %s ===", offsetNames[i]);
                LogTileHierarchy(tile, 0, 5);
            }
        }
        HT_LOG_D3D("=== End Tile Hierarchies ===");
#endif

        void* menuRoot = *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(interfaceMgr) + 0x9C);

        const char* possibleNames[] = {"reticle", "Reticle", "crosshair", "Crosshair",
                                        "HUDReticle", "hudreticle", "HUD", "hud"};
        for (const char* name : possibleNames) {
            g_crosshairTile = FindTileByName(menuRoot, name, 8);
            if (g_crosshairTile) {
                HT_LOG_D3D("Found crosshair tile with name containing '%s' at %p", name, g_crosshairTile);
                break;
            }
        }

        if (!g_crosshairTile) {
            HT_LOG_D3D("Could not find crosshair tile in menuRoot, trying other roots...");

            void* activeTileAlt = *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(interfaceMgr) + 0xBC);
            void* activeTile = *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(interfaceMgr) + 0xCC);

            for (const char* name : possibleNames) {
                if (activeTileAlt) {
                    g_crosshairTile = FindTileByName(activeTileAlt, name, 8);
                    if (g_crosshairTile) {
                        HT_LOG_D3D("Found in activeTileAlt with name '%s' at %p", name, g_crosshairTile);
                        break;
                    }
                }
                if (activeTile) {
                    g_crosshairTile = FindTileByName(activeTile, name, 8);
                    if (g_crosshairTile) {
                        HT_LOG_D3D("Found in activeTile with name '%s' at %p", name, g_crosshairTile);
                        break;
                    }
                }
            }
        }

        if (!g_crosshairTile) {
            HT_LOG_D3D("Could not find crosshair tile anywhere!");
        }
    }

    if (g_crosshairTile && s_TileSetFloatValue) {
        __try {
            s_TileSetFloatValue(g_crosshairTile, kTileValue_visible, visible ? 1.0f : 0.0f, true);
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            HT_LOG_D3D("FATAL: SEH exception in SetCrosshairTileVisible");
            D3D9Hook::SignalFatalError("SetCrosshairTileVisible");
        }
    }
}

void DrawAimCrosshair(IDirect3DDevice9* device, const D3DVIEWPORT9& vp) {
#if HEADTRACKING_DEBUG_LOGGING
    static int drawCount = 0;
    drawCount++;
    bool shouldLog = (drawCount <= 20 || drawCount % 500 == 0);
#endif

    float tanHalfFovX = g_mainCameraTanFovX;
    float tanHalfFovY = g_mainCameraTanFovY;

    // Body aim direction in rotated camera frame — pre-computed in culling hook
    // from the actual rotation matrices. This is model-agnostic: any rotation
    // model (sequential Euler, horizon-locked, etc.) produces correct results.
    float bDepth = g_bodyAimInCamera[0];
    float bUp    = g_bodyAimInCamera[1];
    float bRight = g_bodyAimInCamera[2];

    if (bDepth < 0.01f) bDepth = 0.01f;

    float normalizedX = bRight / (bDepth * tanHalfFovX);
    float normalizedY = -bUp / (bDepth * tanHalfFovY);

    float screenX = vp.Width / 2.0f + normalizedX * (vp.Width / 2.0f);
    float screenY = vp.Height / 2.0f + normalizedY * (vp.Height / 2.0f);

#if HEADTRACKING_DEBUG_LOGGING
    if (shouldLog) {
        HT_LOG_D3D("DrawCrosshair %d: bodyAim=(%.3f,%.3f,%.3f) screen(%.1f,%.1f)",
               drawCount, bDepth, bUp, bRight, screenX, screenY);
    }
#endif

    screenX = fmaxf(30.0f, fminf(static_cast<float>(vp.Width) - 30.0f, screenX));
    screenY = fmaxf(30.0f, fminf(static_cast<float>(vp.Height) - 30.0f, screenY));

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

    // Precomputed unit circle for 12-segment crosshair dot (eliminates 24 trig calls/frame)
    static struct CircleTable {
        float cx[13];
        float sy[13];
        CircleTable() {
            for (int i = 0; i <= 12; i++) {
                float angle = static_cast<float>(i) * 2.0f * kPiF / 12.0f;
                cx[i] = cosf(angle);
                sy[i] = sinf(angle);
            }
        }
    } s_circle;

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

    float centerX = vp.Width / 2.0f;
    float centerY = vp.Height / 2.0f;
    const float radius = 3.0f;
    constexpr int segments = 12;
    const DWORD redColor = 0xFFFF0000;

    const DWORD color = 0xFFD4A017;
    const float gap = 4.0f;
    const float bracketLen = 8.0f;
    const float thick = 4.0f;

    float lx = screenX - gap - bracketLen;
    float rx = screenX + gap + bracketLen;

    // Single vertex buffer: 12 circle triangles (36 verts) + 4 bracket arms (24 verts) = 20 triangles
    Vertex allVerts[60];

    // Circle segments (indices 0-35) — uses precomputed trig table
    for (int i = 0; i < segments; i++) {
        allVerts[i * 3 + 0] = { centerX, centerY, 0.0f, 1.0f, redColor };
        allVerts[i * 3 + 1] = { centerX + radius * s_circle.cx[i],   centerY + radius * s_circle.sy[i],   0.0f, 1.0f, redColor };
        allVerts[i * 3 + 2] = { centerX + radius * s_circle.cx[i+1], centerY + radius * s_circle.sy[i+1], 0.0f, 1.0f, redColor };
    }

    // Bracket arms (indices 36-59)
    BuildBracketArm(&allVerts[36], lx, screenY - bracketLen, screenX - gap, screenY,  thick, color);
    BuildBracketArm(&allVerts[42], lx, screenY + bracketLen, screenX - gap, screenY,  thick, color);
    BuildBracketArm(&allVerts[48], rx, screenY - bracketLen, screenX + gap, screenY, -thick, color);
    BuildBracketArm(&allVerts[54], rx, screenY + bracketLen, screenX + gap, screenY, -thick, color);

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

    device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 20, allVerts, sizeof(Vertex));

    g_cachedStateBlock->Apply();
}

}  // namespace D3D9Internal
}  // namespace HeadTracking
