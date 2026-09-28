#include "d3d9_internal.h"
#include "d3d9_hook.h"
#include "build_profile.h"
#include "rig_lean.h"

#include <cstdint>

namespace HeadTracking::D3D9Internal {
RigLean g_rigLean;
namespace {
void* s_updateFirstPerson = nullptr;

// The player update sets the first-person skeleton root's local translate
// (player+0x694, +0x58) to the player's position and then calls this, which
// updates the skeleton. The root's parent carries no transform, so its local
// translate is in world axes and a world offset adds straight on.
void __fastcall HookedUpdateFirstPerson(uint8_t* player, void*) {
    const cameraunlock::math::Vec3 rig = g_rigLean.TakeRig();
    auto* root = player ? *reinterpret_cast<uint8_t**>(player + 0x694) : nullptr;
    if (root && !D3D9Hook::IsFatalErrorSet() && (rig.x != 0.0f || rig.y != 0.0f || rig.z != 0.0f)) {
        auto* local = reinterpret_cast<float*>(root + 0x58);
        local[0] += rig.x;
        local[1] += rig.y;
        local[2] += rig.z;
    }
    reinterpret_cast<void(__thiscall*)(void*)>(s_updateFirstPerson)(player);
}
}  // namespace

bool IsFirstPerson() {
    auto* player = *reinterpret_cast<uint8_t**>(ActiveProfile().playerBase);
    return player && player[0x64A] == 0;
}

bool InstallRigHook() {
    return CreateHook(reinterpret_cast<void*>(ActiveProfile().updateFirstPerson),
                      reinterpret_cast<void*>(&HookedUpdateFirstPerson), &s_updateFirstPerson);
}
}  // namespace HeadTracking::D3D9Internal
