#include "d3d9_internal.h"
#include "d3d9_hook.h"
#include "build_profile.h"
#include "weapon_view.h"
#include <cameraunlock/logging/file_log.h>
#include <cstring>

namespace HeadTracking::D3D9Internal {
WeaponView g_weaponView;
namespace {
void* s_renderAccumulator = nullptr;
using RenderAccumulator = void (__cdecl*)(void*, void*, uint32_t);

void __cdecl RenderWeaponView(void* camera, void* accumulator, uint32_t flags) {
    const auto original = reinterpret_cast<RenderAccumulator>(s_renderAccumulator);
    auto* main = *reinterpret_cast<uint8_t**>(ActiveProfile().gameMain);
    if (!g_weaponView.valid || D3D9Hook::IsFatalErrorSet() || !main || !camera ||
        camera != *reinterpret_cast<void**>(main + 0xA0)) {
        original(camera, accumulator, flags);
        return;
    }
    auto* transform = reinterpret_cast<float*>(static_cast<uint8_t*>(camera) + 0x68);
    const auto* frustum = reinterpret_cast<const float*>(static_cast<uint8_t*>(camera) + 0xDC);
    float saved[12];
    std::memcpy(saved, transform, sizeof(saved));
    g_weaponView.Apply(transform, frustum[1], frustum[2]);
    static ULONGLONG lastLog = 0;
    const auto now = GetTickCount64();
    if (now - lastLog >= 1000) {
        lastLog = now;
        cameraunlock::logging::Line("WeaponView: lens=(%.3f,%.3f) world=(%.3f,%.3f) relative=(%.3f,%.3f,%.3f) offset=(%.3f,%.3f,%.3f)",
            frustum[1],frustum[2],g_weaponView.tanX,g_weaponView.tanY,
            g_weaponView.rotation[0],g_weaponView.rotation[1],g_weaponView.rotation[2],
            g_weaponView.translation[0],g_weaponView.translation[1],g_weaponView.translation[2]);
    }
    original(camera, accumulator, flags);
    std::memcpy(transform, saved, sizeof(saved));
}
}

bool InstallWeaponViewHook() {
    return CreateHook(reinterpret_cast<void*>(ActiveProfile().renderAccumulator),
        reinterpret_cast<void*>(&RenderWeaponView), &s_renderAccumulator);
}
}
