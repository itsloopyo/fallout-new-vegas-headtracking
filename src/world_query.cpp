#include "world_query.h"
#include "build_profile.h"

#include <cmath>
#include <cstddef>

namespace HeadTracking {
namespace {

struct alignas(16) RayQuery {
    float start[4]{};
    float end[4]{};
    uint32_t shapeFilter = 0;
    uint32_t collisionFilter = 0;
    uint32_t reserved28[6]{};
    float fraction = 1.0f;
    uint32_t shapeKey = UINT32_MAX;
    uint32_t reserved48[2]{};
    uint32_t rootShapeKey = UINT32_MAX;
    uint32_t reserved54[23]{};
};
static_assert(sizeof(RayQuery) == 0xB0);
static_assert(offsetof(RayQuery, fraction) == 0x40);

}

WorldHit TraceWorld(const cameraunlock::math::Vec3& start,
                    const cameraunlock::math::Vec3& direction,
                    float distance, bool cameraCollision) {
    const auto& profile = ActiveProfile();
    auto* world = *reinterpret_cast<void**>(profile.tesWorld);
    auto* player = *reinterpret_cast<uint8_t**>(profile.playerBase);
    if (!world || !player) return {};
    auto* process = *reinterpret_cast<uint8_t**>(player + 0x68);
    if (!process) return {};
    auto* controller = *reinterpret_cast<uint8_t**>(process + 0x138);
    if (!controller) return {};
    auto* phantom = *reinterpret_cast<uint8_t**>(controller + 0x594);
    if (!phantom) return {};
    auto* object = *reinterpret_cast<uint8_t**>(phantom + 8);
    if (!object) return {};

    RayQuery query;
    const uint32_t group = *reinterpret_cast<uint32_t*>(object + 0x2C) & 0xFFFF0000;
    query.collisionFilter = group | (cameraCollision ? 35u : 6u);
    constexpr float worldToHavok = 0.14287498593330383f;
    const auto end = start + direction * distance;
    query.start[0] = start.x * worldToHavok;
    query.start[1] = start.y * worldToHavok;
    query.start[2] = start.z * worldToHavok;
    query.end[0] = end.x * worldToHavok;
    query.end[1] = end.y * worldToHavok;
    query.end[2] = end.z * worldToHavok;
    using CastRay = void* (__thiscall*)(void*, RayQuery*, bool);
    reinterpret_cast<CastRay>(profile.castRay)(world, &query, true);
    if (!std::isfinite(query.fraction) || query.fraction < 0.0f || query.fraction > 1.0f) return {};
    return {true, query.fraction < 1.0f, distance * query.fraction};
}

}
