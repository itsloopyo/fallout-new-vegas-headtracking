#pragma once

// Internal header for D3D9 hook subsystem
// Contains shared state and declarations between d3d9_hook.cpp, d3d9_crosshair.cpp, d3d9_culling.cpp

#include <Windows.h>
#include <d3d9.h>
#include <cstdint>

#include <cameraunlock/math/angle_utils.h>

namespace HeadTracking {

// Float-precision constants for D3D9 code that operates in float
constexpr float kDegToRadF = static_cast<float>(cameraunlock::math::kDegToRad);
constexpr float kPiF = static_cast<float>(cameraunlock::math::kPi);

// Forward declarations
class CameraController;
class UdpReceiver;

namespace D3D9Internal {

// Cached frustum values from main camera (captured during culling hook)
// Used by crosshair drawing to calculate screen position
extern float g_mainCameraTanFovX;
extern float g_mainCameraTanFovY;

// Body aim direction in the rotated camera frame, computed in culling hook.
// [0]=depth (forward), [1]=up, [2]=right.
// The crosshair reads this directly - no separate angle-based formula needed.
extern float g_bodyAimInCamera[3];

extern bool g_aimProjectionValid;
void RestoreCamera();
bool GetCameraPositionOffset(const void* camera, float offset[3]);
void ResetLeanClamp();

// Cached D3D9 state block for crosshair drawing
extern IDirect3DStateBlock9* g_cachedStateBlock;
extern IDirect3DDevice9* g_stateBlockDevice;

// Crosshair UI state
extern bool g_crosshairDisabled;

// Check if game is paused (menu open)
bool IsGamePaused();

// Check if player is aiming down sights
bool IsPlayerAiming();

// Reset UI cache (call after loading screen)

// Hide or show the HUD crosshair tile
void SetCrosshairTileVisible(bool visible);

// Draw crosshair at body-forward position (uses pre-computed g_bodyAimInCamera).
// vp is the viewport already queried by the caller (avoids a duplicate D3D call).
void DrawAimCrosshair(IDirect3DDevice9* device, const D3DVIEWPORT9& vp);

// Install the culling plane hook
bool InstallCullingHook();
bool InstallWeaponViewHook();
bool InstallSkyViewHook();

// Apply rotation to a 3x3 matrix with baseline tracking.
// worldSpaceYaw selects horizon-locked (true) vs camera-local (false) yaw.
// Returns true if the engine refreshed the transform since our last modification.
bool ApplyRotationWithBaseline(float* camMatrix, double yawDeg, double pitchDeg, double rollDeg,
                               bool worldSpaceYaw);

// Log player node hierarchy (debug only)
void LogPlayerNodes();

bool CreateHook(void* target, void* replacement, void** original);

}  // namespace D3D9Internal
}  // namespace HeadTracking
