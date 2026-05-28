// D3D9 Culling hook - frustum expansion and camera rotation application
// Part of the D3D9 hook subsystem

#include "d3d9_internal.h"
#include "d3d9_hook.h"
#include "camera_controller.h"
#include "game_offsets.h"
#include "debug_log.h"

#include <cameraunlock/math/angle_utils.h>

#include <cstring>
#include <cmath>

namespace HeadTracking {
namespace D3D9Internal {

// Address of the culling plane calculation function
static constexpr uintptr_t ADDR_CALC_CULLING_PLANES = 0xA74E10;

// Hook trampoline
static void* s_calcCullingTrampoline = nullptr;

// Expansion factor for culling (widens frustum for culling only)
constexpr float kCullingExpansion = 20.0f;

// FOV capture range for identifying the main camera frustum
constexpr float kFovCaptureMin = 0.9f;
constexpr float kFovCaptureMax = 1.2f;

// Matrix rotation state tracking
static float* s_lastModifiedMatrix = nullptr;
static float s_matrixBeforeRotation[9] = {0};
static float s_matrixAfterRotation[9] = {0};
static bool s_hasRotationState = false;

// Body aim direction in rotated camera frame (for crosshair projection).
// Computed from actual rotation matrices — model-agnostic.
float g_bodyAimInCamera[3] = {1.0f, 0.0f, 0.0f};

// Position offset state.  The engine refreshes the camera position each
// frame via scene graph propagation, so no inter-frame undo is needed.
// The save/check handles re-entry within a single frame, when
// CalcCullingPlanes is called multiple times for the main camera.
static float* s_lastModifiedPosition = nullptr;
static float s_positionBefore[3] = {0};
static float s_positionAfter[3] = {0};
static bool s_hasPositionState = false;

// Conversion: ~70 game units per real-world meter (Gamebryo/FNV scale)
constexpr float kGameUnitsPerMeter = 70.0f;


#if HEADTRACKING_DEBUG_LOGGING
static int s_rotationCallCount = 0;
static DWORD s_lastRotationLogTime = 0;
#endif

// NiMatrix33 stores rotation row-major, representing v_world = M * v_local.
// Columns are local basis axes in world space.  In-game testing of the
// camera world-transform established the order empirically:
//   col 0 = (m[0], m[3], m[6]) = forward
//   col 1 = (m[1], m[4], m[7]) = up
//   col 2 = (m[2], m[5], m[8]) = right
//
// worldSpaceYaw=true (default): yaw is world-frame (around world Z),
// pre-multiply by Rz_world. This mixes rows 0 and 1 of M and leaves row 2
// untouched, so yaw stays horizon-locked at any pitch; convention-independent.
// worldSpaceYaw=false: yaw is camera-local, post-multiply about the camera's
// own up-axis (col 1). At extreme pitch this leans/rolls the view.
// Pitch and roll are always camera-local: post-multiply rotates about the
// column matching that axis. Post-multiplies mix the OTHER two columns.
//   yaw   about up (col 1)      -> mixes forward(col 0) and right(col 2)
//   pitch about right (col 2)   -> mixes forward(col 0) and up(col 1)
//   roll  about forward(col 0)  -> mixes up(col 1) and right(col 2)
static void ApplyRotationToMatrix(float* m, double yawDeg, double pitchDeg, double rollDeg,
                                  bool worldSpaceYaw) {
    if (!m) return;

    float yawRad = static_cast<float>(yawDeg * cameraunlock::math::kDegToRad);
    float pitchRad = static_cast<float>(pitchDeg * cameraunlock::math::kDegToRad);
    float rollRad = static_cast<float>(-rollDeg * cameraunlock::math::kDegToRad);

    float cy = cosf(yawRad), sy = sinf(yawRad);
    if (worldSpaceYaw) {
        // YAW (world-space): new_M = Rz(yaw) * M.  Mixes rows 0 and 1; row 2 unchanged.
        float r0[3] = {m[0], m[1], m[2]};
        float r1[3] = {m[3], m[4], m[5]};
        m[0] = cy*r0[0] + sy*r1[0];  m[1] = cy*r0[1] + sy*r1[1];  m[2] = cy*r0[2] + sy*r1[2];
        m[3] = -sy*r0[0] + cy*r1[0]; m[4] = -sy*r0[1] + cy*r1[1]; m[5] = -sy*r0[2] + cy*r1[2];
    } else {
        // YAW (camera-local): rotate about up (col 1).  Mixes columns 0 and 2.
        // new_col0 = cy*col0 + sy*col2; new_col2 = -sy*col0 + cy*col2.
        // Sign chosen so zero-pitch yaw matches the world-space branch exactly.
        float c0x = m[0], c0y = m[3], c0z = m[6];
        float c2x = m[2], c2y = m[5], c2z = m[8];
        m[0] = cy*c0x + sy*c2x;  m[3] = cy*c0y + sy*c2y;  m[6] = cy*c0z + sy*c2z;
        m[2] = -sy*c0x + cy*c2x; m[5] = -sy*c0y + cy*c2y; m[8] = -sy*c0z + cy*c2z;
    }

    // PITCH: rotate about right (col 2).  Mixes columns 0 and 1.
    // new_col0 = cp*col0 + sp*col1; new_col1 = -sp*col0 + cp*col1
    float cp = cosf(pitchRad), sp = sinf(pitchRad);
    float c0x = m[0], c0y = m[3], c0z = m[6];
    float c1x = m[1], c1y = m[4], c1z = m[7];
    m[0] = cp*c0x + sp*c1x;  m[3] = cp*c0y + sp*c1y;  m[6] = cp*c0z + sp*c1z;
    m[1] = -sp*c0x + cp*c1x; m[4] = -sp*c0y + cp*c1y; m[7] = -sp*c0z + cp*c1z;

    // ROLL: rotate about forward (col 0).  Mixes columns 1 and 2.
    // new_col1 = cr*col1 + sr*col2; new_col2 = -sr*col1 + cr*col2
    if (fabsf(rollRad) > 0.0001f) {
        float cr = cosf(rollRad), sr = sinf(rollRad);
        c1x = m[1]; c1y = m[4]; c1z = m[7];
        float c2x = m[2], c2y = m[5], c2z = m[8];
        m[1] = cr*c1x + sr*c2x;  m[4] = cr*c1y + sr*c2y;  m[7] = cr*c1z + sr*c2z;
        m[2] = -sr*c1x + cr*c2x; m[5] = -sr*c1y + cr*c2y; m[8] = -sr*c1z + cr*c2z;
    }
}

// Check if matrix still contains our rotation
static bool MatrixStillHasOurRotation(float* camMatrix) {
    if (!camMatrix || !s_hasRotationState) {
        return false;
    }
    if (camMatrix != s_lastModifiedMatrix) return false;
    for (int i = 0; i < 9; i++) {
        if (fabsf(camMatrix[i] - s_matrixAfterRotation[i]) > 0.0001f) {
            return false;
        }
    }
    return true;
}

// Check if camera position still has our offset (for re-entry detection).
// Between frames the engine always refreshes the position (scene graph
// propagation), so this only returns true for multiple CalcCullingPlanes
// calls within the same frame.
static bool PositionStillHasOurOffset(float* pos) {
    if (!pos || !s_hasPositionState || pos != s_lastModifiedPosition) return false;
    for (int i = 0; i < 3; i++) {
        if (fabsf(pos[i] - s_positionAfter[i]) > 0.01f) return false;
    }
    return true;
}

// Apply positional head tracking by directly offsetting the NiCamera position.
// Must be called AFTER CalcCullingPlanes returns; calling it before triggers
// a BSP traversal crash.  Uses the pre-rotation basis (the body's orientation,
// not the head's) to transform the tracker-space offset to world space.
static void ApplyCameraPositionOffset(float* camPos, float posX, float posY, float posZ) {
    if (!camPos) return;

    if (PositionStillHasOurOffset(camPos)) {
        memcpy(camPos, s_positionBefore, sizeof(s_positionBefore));
    }

    memcpy(s_positionBefore, camPos, sizeof(s_positionBefore));

    // Columns of the pre-rotation matrix are the body's local basis in world:
    //   col 0 = forward = (m[0], m[3], m[6])
    //   col 1 = up      = (m[1], m[4], m[7])
    //   col 2 = right   = (m[2], m[5], m[8])
    // world_offset = posX*right + posY*up + posZ*forward
    float* m = s_matrixBeforeRotation;
    float worldX = posX * m[2] + posY * m[1] + posZ * m[0];
    float worldY = posX * m[5] + posY * m[4] + posZ * m[3];
    float worldZ = posX * m[8] + posY * m[7] + posZ * m[6];

    camPos[0] += worldX;
    camPos[1] += worldY;
    camPos[2] += worldZ;

    memcpy(s_positionAfter, camPos, sizeof(s_positionAfter));
    s_lastModifiedPosition = camPos;
    s_hasPositionState = true;
}

// Returns true if the engine refreshed the transform since our last modification.
bool ApplyRotationWithBaseline(float* camMatrix, double yawDeg, double pitchDeg, double rollDeg,
                               bool worldSpaceYaw) {
    if (!camMatrix) return true;

#if HEADTRACKING_DEBUG_LOGGING
    s_rotationCallCount++;
    DWORD now = GetTickCount();
    if (now - s_lastRotationLogTime >= 1000) {
        HT_LOG_D3D("=== ROTATION STATS: %d calls in last second, yaw=%.2f pitch=%.2f ===",
               s_rotationCallCount, yawDeg, pitchDeg);
        s_rotationCallCount = 0;
        s_lastRotationLogTime = now;
    }
#endif

    bool engineRefreshed = !MatrixStillHasOurRotation(camMatrix);
    if (!engineRefreshed) {
        memcpy(camMatrix, s_matrixBeforeRotation, sizeof(s_matrixBeforeRotation));
    }

#if HEADTRACKING_DEBUG_LOGGING
    static int detailLogCount = 0;
    if (detailLogCount < 10) {
        HT_LOG_D3D("ApplyRotation #%d: yaw=%.2f pitch=%.2f matrix[0]=%.4f",
               detailLogCount, yawDeg, pitchDeg, camMatrix[0]);
        detailLogCount++;
    }
#endif

    memcpy(s_matrixBeforeRotation, camMatrix, sizeof(s_matrixBeforeRotation));
    ApplyRotationToMatrix(camMatrix, yawDeg, pitchDeg, rollDeg, worldSpaceYaw);
    memcpy(s_matrixAfterRotation, camMatrix, sizeof(s_matrixAfterRotation));
    s_lastModifiedMatrix = camMatrix;
    s_hasRotationState = true;
    return engineRefreshed;
}

// Hook wrapper for culling plane calculation
static void __fastcall HookedCalcCullingPlanes(void* frustumPlanes, void* edx, void* frustum, void* worldTransform) {
    (void)edx;

    if (D3D9Hook::IsFatalErrorSet()) {
        typedef void (__thiscall *OrigCalcCullingPlanesFn)(void* thisPtr, void* frustum, void* worldTrans);
        OrigCalcCullingPlanesFn origFunc = reinterpret_cast<OrigCalcCullingPlanesFn>(s_calcCullingTrampoline);
        origFunc(frustumPlanes, frustum, worldTransform);
        return;
    }

#if HEADTRACKING_DEBUG_LOGGING
    static int callCount = 0;
    callCount++;
    bool shouldLog = (callCount <= 10 || callCount % 1000 == 0);
#endif

    CameraController* controller = D3D9Hook::GetCameraController();
    bool shouldExpand = controller && controller->IsDecoupled() && controller->IsActive();
    bool appliedRotation = false;
    bool isMainCamera = false;

    // CalcCullingPlanes is invoked multiple times per frame (main camera, shadow,
    // reflection, refraction). The scene-graph -> camera -> world-transform walk
    // is stable across frames and only changes on scene loads / camera-mode
    // switches, so cache the resolved transform pointer and rebuild only when
    // either of the two upstream pointers changes (still one deref per call).
    static uint8_t* s_cachedSceneGraph = nullptr;
    static uint8_t* s_cachedCamera = nullptr;
    static void* s_cachedMainWorldTransform = nullptr;

    uint8_t* sceneGraph = *reinterpret_cast<uint8_t**>(GameOffsets::kSceneGraphBase);
    if (sceneGraph) {
        uint8_t* camera = *reinterpret_cast<uint8_t**>(sceneGraph + GameOffsets::kSceneGraphCamera);
        if (camera) {
            if (sceneGraph != s_cachedSceneGraph || camera != s_cachedCamera) {
                s_cachedSceneGraph = sceneGraph;
                s_cachedCamera = camera;
                s_cachedMainWorldTransform = reinterpret_cast<void*>(camera + GameOffsets::kCameraWorldTransform);
            }
            isMainCamera = (worldTransform == s_cachedMainWorldTransform);
        }
    }

    float* camMatrix = reinterpret_cast<float*>(worldTransform);
    float* f = reinterpret_cast<float*>(frustum);

    // IsGamePaused() sets up an SEH frame and dereferences the InterfaceManager
    // singleton; it is only needed for the main-camera rotation branch. Kept as
    // the last short-circuit term so it runs ~once per frame, not once per
    // frustum (shadow/reflection/refraction passes skip it).
    if (D3D9Hook::Instance().IsEnabled() && controller && shouldExpand && worldTransform && isMainCamera && !IsGamePaused()) {
        double yawDeg = controller->GetCurrentYawOffset();
        double pitchDeg = controller->GetCurrentPitchOffset();
        double rollDeg = controller->GetCurrentRollOffset();

        if (IsPlayerAiming()) {
            yawDeg *= 2.0;
            pitchDeg *= 2.0;
        }

        __try {
            ApplyRotationWithBaseline(camMatrix, yawDeg, pitchDeg, rollDeg,
                                      controller->IsWorldSpaceYaw());
            appliedRotation = true;

            // body_world = pre-rotation forward (column 0 of pre-rotation matrix).
            // Project into rotated camera frame via dot products with new columns.
            // [0] = depth (along new forward, col 0)
            // [1] = up    (along new up,      col 1)
            // [2] = right (along new right,   col 2)
            float bf0 = s_matrixBeforeRotation[0];
            float bf1 = s_matrixBeforeRotation[3];
            float bf2 = s_matrixBeforeRotation[6];
            g_bodyAimInCamera[0] = bf0*camMatrix[0] + bf1*camMatrix[3] + bf2*camMatrix[6];
            g_bodyAimInCamera[1] = bf0*camMatrix[1] + bf1*camMatrix[4] + bf2*camMatrix[7];
            g_bodyAimInCamera[2] = bf0*camMatrix[2] + bf1*camMatrix[5] + bf2*camMatrix[8];
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            D3D9Hook::SignalFatalError("ApplyRotationWithBaseline in HookedCalcCullingPlanes");
        }

        // Positional head tracking. MUST be applied before origFunc: the view
        // matrix is built inside CalcCullingPlanes, so a post-call write lands
        // too late and is discarded. The ×20 frustum expansion covers the
        // offset for culling.
        float posX = controller->GetPositionX();   // meters, right
        float posY = controller->GetPositionY();   // meters, up
        float posZ = controller->GetPositionZ();   // meters, forward

        if (posX != 0.0f || posY != 0.0f || posZ != 0.0f) {
            float* camPos = reinterpret_cast<float*>(
                static_cast<uint8_t*>(worldTransform) + GameOffsets::kWorldTransformToPosition);
            ApplyCameraPositionOffset(camPos,
                posX * kGameUnitsPerMeter,
                posY * kGameUnitsPerMeter,
                posZ * kGameUnitsPerMeter);
        }
    }
    float origLeft = 0, origRight = 0, origTop = 0, origBottom = 0;

    if (f) {
        if (f[1] > kFovCaptureMin && f[1] < kFovCaptureMax) {
            g_mainCameraTanFovX = f[1];
            g_mainCameraTanFovY = f[2];
        }
    }

    if (shouldExpand && f) {
        origLeft = f[0];
        origRight = f[1];
        origTop = f[2];
        origBottom = f[3];

        f[0] *= kCullingExpansion;
        f[1] *= kCullingExpansion;
        f[2] *= kCullingExpansion;
        f[3] *= kCullingExpansion;

#if HEADTRACKING_DEBUG_LOGGING
        if (shouldLog) {
            HT_LOG_D3D("CalcCullingPlanes %d: EXPANDED L=%.3f->%.3f R=%.3f->%.3f T=%.3f->%.3f B=%.3f->%.3f",
                   callCount, origLeft, f[0], origRight, f[1], origTop, f[2], origBottom, f[3]);
        }
#endif
    }

    typedef void (__thiscall *OrigCalcCullingPlanesFn)(void* thisPtr, void* frustum, void* worldTrans);
    OrigCalcCullingPlanesFn origFunc = reinterpret_cast<OrigCalcCullingPlanesFn>(s_calcCullingTrampoline);
    origFunc(frustumPlanes, frustum, worldTransform);

    if (shouldExpand && f) {
        f[0] = origLeft;
        f[1] = origRight;
        f[2] = origTop;
        f[3] = origBottom;

#if HEADTRACKING_DEBUG_LOGGING
        if (shouldLog) {
            HT_LOG_D3D("CalcCullingPlanes %d: RESTORED L=%.3f R=%.3f T=%.3f B=%.3f",
                   callCount, f[0], f[1], f[2], f[3]);
        }
#endif
    }

#if HEADTRACKING_DEBUG_LOGGING
    if (shouldLog && appliedRotation) {
        HT_LOG_D3D("CalcCullingPlanes %d: applied head rotation (yaw=%.2f pitch=%.2f roll=%.2f)",
               callCount, controller ? controller->GetCurrentYawOffset() : 0.0,
               controller ? controller->GetCurrentPitchOffset() : 0.0,
               controller ? controller->GetCurrentRollOffset() : 0.0);
    } else if (shouldLog && !isMainCamera) {
        HT_LOG_D3D("CalcCullingPlanes %d: skipped rotation (not main camera)", callCount);
    }
#endif
}

bool InstallCullingHook() {
    HT_LOG_D3D("Installing culling plane hook at 0x%08X", ADDR_CALC_CULLING_PLANES);

    DWORD oldProtect;
    if (!VirtualProtect(reinterpret_cast<void*>(ADDR_CALC_CULLING_PLANES), 16, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        HT_LOG_D3D("ERROR: VirtualProtect failed for culling hook");
        return false;
    }

    constexpr int CULLING_HOOK_SIZE = 10;

    s_calcCullingTrampoline = InstallJmpHook(
        reinterpret_cast<void*>(ADDR_CALC_CULLING_PLANES),
        reinterpret_cast<void*>(&HookedCalcCullingPlanes),
        CULLING_HOOK_SIZE);
    if (!s_calcCullingTrampoline) {
        VirtualProtect(reinterpret_cast<void*>(ADDR_CALC_CULLING_PLANES), 16, oldProtect, &oldProtect);
        HT_LOG_D3D("ERROR: Failed to install culling hook");
        return false;
    }

    VirtualProtect(reinterpret_cast<void*>(ADDR_CALC_CULLING_PLANES), 16, oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(ADDR_CALC_CULLING_PLANES), 16);

    HT_LOG_D3D("Culling hook installed successfully!");
    return true;
}

}  // namespace D3D9Internal
}  // namespace HeadTracking
