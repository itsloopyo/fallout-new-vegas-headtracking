#pragma once

#include <cstdint>

// Single source of truth for the Fallout: New Vegas (1.4.0.525) runtime memory
// layout the plugin reverse-engineers. These addresses and field offsets were
// previously duplicated across camera_controller.cpp, d3d9_hook.cpp and
// d3d9_culling.cpp; centralizing them keeps the RE knowledge in one place so a
// single discovery (or a runtime version bump) updates every reader at once.

namespace HeadTracking {
namespace GameOffsets {

// Global singleton pointers (a pointer to the object lives at this address).
constexpr uintptr_t kPlayerBase     = 0x011DEA3C;  // PlayerCharacter*
constexpr uintptr_t kSceneGraphBase = 0x011DEB7C;  // SceneGraph*

// PlayerCharacter field offsets (rotation stored in radians).
constexpr uintptr_t kPlayerRotX = 0x24;  // pitch (looking up/down)
constexpr uintptr_t kPlayerRotZ = 0x2C;  // yaw   (turning left/right)

// SceneGraph field offsets.
constexpr uintptr_t kSceneGraphCamera = 0xAC;  // NiCamera* (main world camera)

// NiCamera field offsets.
constexpr uintptr_t kCameraWorldTransform = 0x68;  // NiTransform rotation matrix (NiMatrix33)
constexpr uintptr_t kCameraWorldPosition  = 0x8C;  // NiPoint3 world position
// Position relative to the world-transform pointer (0x8C - 0x68); the culling
// hook receives the world transform and reaches the position from there.
constexpr uintptr_t kWorldTransformToPosition = kCameraWorldPosition - kCameraWorldTransform;

}  // namespace GameOffsets
}  // namespace HeadTracking
