#pragma once

#include <cameraunlock/math/vec3.h>

namespace HeadTracking {

struct WorldHit {
    bool queried = false;
    bool blocked = false;
    float distance = 0.0f;
};

WorldHit TraceWorld(const cameraunlock::math::Vec3& start,
                    const cameraunlock::math::Vec3& direction,
                    float distance, bool cameraCollision);

}
