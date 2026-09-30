#include "world_query.h"
#include "build_profile.h"
#include "runtime_discovery.h"

#include <cmath>
#include <cstring>

namespace HeadTracking {
WorldHit TraceWorld(const cameraunlock::math::Vec3& start,
                    const cameraunlock::math::Vec3& direction,
                    float distance, bool cameraCollision) {
    const auto& profile = ActiveProfile();
    const auto& layout = ActiveLayout();
    auto* world = *reinterpret_cast<void**>(profile.tesWorld);
    auto* player = RuntimePlayer();
    if (!RuntimeObject(world, 4, 0, "TES world trace receiver") || !player) return {};
    auto* process = *reinterpret_cast<uint8_t**>(player + layout.playerProcess);
    if (!RuntimeObject(process, layout.processController + 4, ActiveRuntime().processVtable, "HighProcess collision controller")) return {};
    auto* controller = *reinterpret_cast<uint8_t**>(process + layout.processController);
    if (!RuntimeObject(controller, layout.controllerPhantom + 4, 0, "character controller")) return {};
    auto* phantom = *reinterpret_cast<uint8_t**>(controller + layout.controllerPhantom);
    if (!RuntimeObject(phantom, layout.phantomObject + 4, 0, "character phantom")) return {};
    auto* object = *reinterpret_cast<uint8_t**>(phantom + layout.phantomObject);
    if (!RuntimeObject(object, layout.objectFilter + 4, 0, "collision filter owner")) return {};

    alignas(16) uint8_t query[512]{};
    const uint32_t group = *reinterpret_cast<uint32_t*>(object + layout.objectFilter) & layout.collisionGroupMask;
    const uint32_t filter = group | (cameraCollision ? layout.layerCamera : layout.layerAim);
    const uint32_t missingKey = UINT32_MAX;
    float fraction = 1.0f;
    std::memcpy(query + layout.rayFilter, &filter, sizeof(filter));
    std::memcpy(query + layout.rayFraction, &fraction, sizeof(fraction));
    std::memcpy(query + layout.rayShapeKey, &missingKey, sizeof(missingKey));
    std::memcpy(query + layout.rayRootShapeKey, &missingKey, sizeof(missingKey));
    const auto end = start + direction * distance;
    const float from[4] = {start.x * layout.worldToHavok, start.y * layout.worldToHavok, start.z * layout.worldToHavok, 0};
    const float to[4] = {end.x * layout.worldToHavok, end.y * layout.worldToHavok, end.z * layout.worldToHavok, 0};
    std::memcpy(query + layout.rayStart, from, sizeof(from));
    std::memcpy(query + layout.rayEnd, to, sizeof(to));
    using CastRay = void* (__thiscall*)(void*, void*, bool);
    reinterpret_cast<CastRay>(profile.castRay)(world, query, true);
    std::memcpy(&fraction, query + layout.rayFraction, sizeof(fraction));
    if (!std::isfinite(fraction) || fraction < 0.0f || fraction > 1.0f) {
        RejectRuntimeContract("world trace returned an invalid fraction");
        return {};
    }
    return {true, fraction < 1.0f, distance * fraction};
}
}
