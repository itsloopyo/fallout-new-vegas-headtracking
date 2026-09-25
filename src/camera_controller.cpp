#include <Windows.h>

#include "camera_controller.h"
#include "d3d9_hook.h"
#include "plugin.h"
#include "game_offsets.h"
#include "debug_log.h"

#include <cameraunlock/math/angle_utils.h>
#include <cameraunlock/math/smoothing_utils.h>

#include "nvse_abi/GameAPI.h"

#include <algorithm>
#include <cmath>

namespace HeadTracking {

CameraController::CameraController()
    : m_enabled(true)
    , m_initialized(false)
    , m_hasTrackingData(false)
    , m_rawYaw(0.0)
    , m_rawPitch(0.0)
    , m_rawRoll(0.0)
    , m_smoothedYaw(0.0)
    , m_smoothedPitch(0.0)
    , m_smoothedRoll(0.0)
    , m_localSmoothing(cameraunlock::math::kDefaultLocalSmoothing)
    , m_remoteSmoothing(cameraunlock::math::kDefaultRemoteSmoothing)
    , m_remoteConnection(false)
    , m_worldSpaceYaw(true) {
}

CameraController::~CameraController() {
}

void CameraController::Initialize() {
    if (m_initialized) {
        return;
    }

    // Reset all state
    m_rawYaw = 0.0;
    m_rawPitch = 0.0;
    m_rawRoll = 0.0;
    m_smoothedYaw = 0.0;
    m_smoothedPitch = 0.0;
    m_smoothedRoll = 0.0;
    m_hasTrackingData = false;

    m_initialized = true;

    if (g_ConsolePrint) {
        g_ConsolePrint("HeadTracking: Camera controller initialized");
    }
}

void CameraController::Update(const TrackingData& data, float deltaTime) {
#if HEADTRACKING_DEBUG_LOGGING
    static int updateCount = 0;
    updateCount++;
    bool shouldLog = (updateCount % 60 == 0);
#endif

    if (!m_enabled || !m_initialized) {
        HT_LOG_CAMERA("Update: disabled or not initialized (enabled=%d init=%d)", m_enabled, m_initialized);
        return;
    }

    // Validate incoming data
    if (!data.valid) {
        HT_LOG_CAMERA("Update: invalid data");
        return;
    }

    const bool firstSample = !m_hasTrackingData;
    m_hasTrackingData = true;

    // The tracker owns the centre, so its pose is taken as absolute.
    double rawYawOffset = data.yaw;
    double rawPitchOffset = data.pitch;
    double rawRollOffset = data.roll;

#if HEADTRACKING_DEBUG_LOGGING
    if (shouldLog) {
        HT_LOG_CAMERA("Update: data yaw=%.2f pitch=%.2f", data.yaw, data.pitch);
    }
#endif

    // Normalize yaw to -180 to +180 range
    rawYawOffset = cameraunlock::math::NormalizeAngle(rawYawOffset);

    // Store raw values
    m_rawYaw = rawYawOffset;
    m_rawPitch = rawPitchOffset;
    m_rawRoll = rawRollOffset;

    // Local trackers are already stable and get whatever the user asked for,
    // down to none at all; a device sending over the network is the one that
    // needs the jitter rejection. Nothing is applied on top of either value.
    double effectiveSmoothing = cameraunlock::math::GetEffectiveSmoothing(
        m_localSmoothing, m_remoteSmoothing, m_remoteConnection);

    if (firstSample || effectiveSmoothing == 0.0) {
        m_smoothedYaw = rawYawOffset;
        m_smoothedPitch = rawPitchOffset;
        m_smoothedRoll = rawRollOffset;
    } else {
        m_smoothedYaw = cameraunlock::math::SmoothAngle(static_cast<float>(m_smoothedYaw), static_cast<float>(rawYawOffset),
            static_cast<float>(effectiveSmoothing), deltaTime);
        m_smoothedPitch = cameraunlock::math::Smooth(m_smoothedPitch, rawPitchOffset, effectiveSmoothing, static_cast<double>(deltaTime));
        m_smoothedRoll = cameraunlock::math::Smooth(m_smoothedRoll, rawRollOffset, effectiveSmoothing, static_cast<double>(deltaTime));
    }

#if HEADTRACKING_DEBUG_LOGGING
    if (shouldLog) {
        HT_LOG_CAMERA("  Final: yaw=%.2f pitch=%.2f (smoothing=%.3f)",
                    m_smoothedYaw, m_smoothedPitch, effectiveSmoothing);
    }
#endif

}

void CameraController::SetEnabled(bool enabled) {
    if (m_enabled == enabled) {
        return;
    }

    m_enabled = enabled;

    if (!enabled) {
        ResetTracking();
        // Reset smoothed values when disabling
        m_smoothedYaw = 0.0;
        m_smoothedPitch = 0.0;
        m_smoothedRoll = 0.0;
    }

    if (g_ConsolePrint) {
        g_ConsolePrint("HeadTracking: %s", enabled ? "Enabled" : "Disabled");
    }
}

void CameraController::SetLocalSmoothing(double smoothing) {
    m_localSmoothing = smoothing;
}

void CameraController::SetRemoteSmoothing(double smoothing) {
    m_remoteSmoothing = smoothing;
}

void CameraController::SetWorldSpaceYaw(bool worldSpace) {
    m_worldSpaceYaw = worldSpace;
}

}  // namespace HeadTracking

namespace HeadTracking {
void CameraController::ResetTracking() {
    m_hasTrackingData = false;
    m_smoothedYaw = m_smoothedPitch = m_smoothedRoll = 0.0;
    m_posX = m_posY = m_posZ = 0.0f;
}
}
