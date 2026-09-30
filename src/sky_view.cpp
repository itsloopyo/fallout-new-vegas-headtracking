#include "d3d9_internal.h"
#include "d3d9_hook.h"
#include "build_profile.h"
#include "runtime_discovery.h"
#include <cameraunlock/logging/file_log.h>
#include <cstring>

namespace HeadTracking::D3D9Internal {
namespace {
void* s_setupSkyGeometry = nullptr;
using SetupSkyGeometry = void (__thiscall*)(void*, void*);

void __fastcall SetupSkyView(void* shader, void*, void* properties) {
    const auto original = reinterpret_cast<SetupSkyGeometry>(s_setupSkyGeometry);
    const auto& profile = ActiveProfile();
    const auto* accumulator = *reinterpret_cast<uint8_t**>(profile.currentAccumulator);
    if (!RuntimeObject(shader, 4, ActiveRuntime().skyVtable, "SkyShader") ||
        !RuntimeObject(accumulator, ActiveLayout().accumulatorCamera + 4, ActiveRuntime().accumulatorVtable, "sky accumulator")) {
        original(shader, properties);
        return;
    }
    const auto* camera = *reinterpret_cast<void* const*>(accumulator + ActiveLayout().accumulatorCamera);
    float offset[3] = {};
    const bool tracked = !D3D9Hook::IsFatalErrorSet() && GetCameraPositionOffset(camera, offset);
    if (!tracked) {
        original(shader, properties);
        return;
    }
    static ULONGLONG lastLog = 0;
    const auto now = GetTickCount64();
    if (now - lastLog >= 1000) {
        lastLog = now;
        cameraunlock::logging::Line("SkyView: camera=%p tracked=%d lean=(%.3f,%.3f,%.3f)",
            camera, tracked, offset[0], offset[1], offset[2]);
    }
    const auto* pass = *reinterpret_cast<uint8_t**>(profile.currentRenderPass);
    if (!RuntimeObject(pass, 4, 0, "sky render pass")) { original(shader, properties); return; }
    auto* geometry = *reinterpret_cast<uint8_t* const*>(pass);
    if (!RuntimeObject(geometry, ActiveLayout().cameraPosition + 12, ActiveRuntime().geometryVtables, "sky geometry")) {
        original(shader, properties);
        return;
    }
    auto* position = reinterpret_cast<float*>(geometry + ActiveLayout().cameraPosition);
    float saved[3];
    std::memcpy(saved, position, sizeof(saved));
    // Sky geometry is centred on the clean eye. Follow the clamped render lean
    // only while the shader copies its world transform into GPU constants.
    for (int i = 0; i < 3; ++i) position[i] += offset[i];
    original(shader, properties);
    std::memcpy(position, saved, sizeof(saved));
}
}

bool InstallSkyViewHook() {
    return CreateHook(reinterpret_cast<void*>(ActiveProfile().setupSkyGeometry),
        reinterpret_cast<void*>(&SetupSkyView), &s_setupSkyGeometry);
}
}
