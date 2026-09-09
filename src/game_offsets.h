#pragma once

#include <cstdint>

#include "build_profile.h"

namespace HeadTracking {
namespace GameOffsets {

// Global singleton pointers (a pointer to the object lives at this address).
inline uintptr_t PlayerBase() { return ActiveProfile().playerBase; }
inline uintptr_t SceneGraphBase() { return ActiveProfile().sceneGraphBase; }

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
