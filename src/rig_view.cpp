#include "d3d9_internal.h"
#include "d3d9_hook.h"
#include "build_profile.h"
#include "runtime_discovery.h"
#include "rig_lean.h"

#include <cstdint>
#include <intrin.h>
#include <cmath>

namespace HeadTracking::D3D9Internal {
RigLean g_rigLean;
namespace {
void* s_updateFirstPerson = nullptr;

// The player update sets the first-person skeleton root's local translate
// to the player's position and then calls this, which
// updates the skeleton. The root's parent carries no transform, so its local
// translate is in world axes and a world offset adds straight on.
void __fastcall HookedUpdateFirstPerson(uint8_t* player, void*) {
    if (reinterpret_cast<uintptr_t>(_ReturnAddress()) != ActiveRuntime().rigTranslationCaller) {
        reinterpret_cast<void(__thiscall*)(void*)>(s_updateFirstPerson)(player);
        return;
    }
    const cameraunlock::math::Vec3 rig = g_rigLean.TakeRig(IsFirstPerson());
    auto* root = player == RuntimePlayer() && player ? *reinterpret_cast<uint8_t**>(player + ActiveLayout().playerFirstPersonRoot) : nullptr;
    if (!D3D9Hook::IsFatalErrorSet() && (rig.x != 0.0f || rig.y != 0.0f || rig.z != 0.0f) &&
        RuntimeObject(root, ActiveLayout().nodeLocalPosition + 12, ActiveRuntime().nodeVtables, "first-person NiNode")) {
        auto* parent = *reinterpret_cast<uint8_t**>(root + ActiveLayout().nodeParent);
        bool untransformed = !parent;
        if (parent && RuntimeObject(parent, ActiveLayout().nodeWorldScale + 4, ActiveRuntime().nodeVtables, "first-person parent")) {
            const auto* matrix = reinterpret_cast<const float*>(parent + ActiveLayout().cameraTransform);
            const float scale = *reinterpret_cast<const float*>(parent + ActiveLayout().nodeWorldScale);
            untransformed = std::isfinite(scale) && std::fabs(scale - 1) < 0.0001f;
            for (int n = 0; n < 9; ++n)
                untransformed &= std::isfinite(matrix[n]) && std::fabs(matrix[n] - (n % 4 == 0 ? 1.0f : 0.0f)) < 0.0001f;
        }
        if (!untransformed) {
            RejectRuntimeContract("first-person rig parent does not preserve world-axis offsets");
            reinterpret_cast<void(__thiscall*)(void*)>(s_updateFirstPerson)(player);
            return;
        }
        auto* local = reinterpret_cast<float*>(root + ActiveLayout().nodeLocalPosition);
        local[0] += rig.x;
        local[1] += rig.y;
        local[2] += rig.z;
    }
    reinterpret_cast<void(__thiscall*)(void*)>(s_updateFirstPerson)(player);
}
}  // namespace

bool IsFirstPerson() {
    auto* player = RuntimePlayer();
    return player && player[ActiveLayout().playerThirdPerson] == 0;
}

bool InstallRigHook() {
    return CreateHook(reinterpret_cast<void*>(ActiveProfile().updateFirstPerson),
                      reinterpret_cast<void*>(&HookedUpdateFirstPerson), &s_updateFirstPerson);
}
}  // namespace HeadTracking::D3D9Internal
