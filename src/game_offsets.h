#pragma once
#include "build_profile.h"
#include "runtime_discovery.h"

namespace HeadTracking::GameOffsets {
inline uintptr_t PlayerBase() { return ActiveProfile().playerBase; }
inline uintptr_t SceneGraphBase() { return ActiveProfile().sceneGraphBase; }
inline uintptr_t SceneGraphCamera() { return ActiveLayout().sceneCamera; }
inline uintptr_t CameraWorldTransform() { return ActiveLayout().cameraTransform; }
inline uintptr_t CameraWorldPosition() { return ActiveLayout().cameraPosition; }
inline uintptr_t WorldTransformToPosition() { return ActiveLayout().cameraPosition - ActiveLayout().cameraTransform; }
}
