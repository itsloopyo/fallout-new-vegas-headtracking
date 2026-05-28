#include <Windows.h>

#include "camera_controller.h"
#include "d3d9_hook.h"
#include "plugin.h"
#include "game_offsets.h"
#include "debug_log.h"

#include <cameraunlock/math/angle_utils.h>
#include <cameraunlock/math/deadzone_utils.h>
#include <cameraunlock/math/smoothing_utils.h>

#include "nvse/GameAPI.h"

#include <algorithm>
#include <cmath>

namespace HeadTracking {

constexpr float kMaxPitchRad = 1.5f;  // ~85.9 degrees, prevents gimbal lock

CameraController::CameraController()
    : m_enabled(true)
    , m_initialized(false)
    , m_hasValidCenter(false)
    , m_centerYaw(0.0)
    , m_centerPitch(0.0)
    , m_centerRoll(0.0)
    , m_rawYaw(0.0)
    , m_rawPitch(0.0)
    , m_rawRoll(0.0)
    , m_smoothedYaw(0.0)
    , m_smoothedPitch(0.0)
    , m_smoothedRoll(0.0)
    , m_smoothingFactor(0.0)
    , m_sensitivity()
    , m_deadzone()
    , m_cameraMode(CameraMode::Coupled)
    , m_worldSpaceYaw(true) {
}

CameraController::~CameraController() {
}

void CameraController::Initialize() {
    if (m_initialized) {
        return;
    }

    // Reset all state
    m_centerYaw = 0.0;
    m_centerPitch = 0.0;
    m_centerRoll = 0.0;
    m_rawYaw = 0.0;
    m_rawPitch = 0.0;
    m_rawRoll = 0.0;
    m_smoothedYaw = 0.0;
    m_smoothedPitch = 0.0;
    m_smoothedRoll = 0.0;
    m_hasValidCenter = false;

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

    // Auto-center on first valid data if no center set
    if (!m_hasValidCenter) {
        HT_LOG_CAMERA("Update: No valid center, recentering to yaw=%.2f pitch=%.2f", data.yaw, data.pitch);
        Recenter(data);
        // Don't return - continue processing so smoothed values get set this frame
    }

    // Calculate raw offset from center position
    double rawYawOffset = data.yaw - m_centerYaw;
    double rawPitchOffset = data.pitch - m_centerPitch;
    double rawRollOffset = data.roll - m_centerRoll;

#if HEADTRACKING_DEBUG_LOGGING
    if (shouldLog) {
        HT_LOG_CAMERA("Update: data yaw=%.2f pitch=%.2f, center yaw=%.2f pitch=%.2f, rawOffset yaw=%.2f pitch=%.2f",
                    data.yaw, data.pitch, m_centerYaw, m_centerPitch, rawYawOffset, rawPitchOffset);
    }
#endif

    // Normalize yaw to -180 to +180 range
    rawYawOffset = cameraunlock::math::NormalizeAngle(rawYawOffset);

    // Apply deadzone
#if HEADTRACKING_DEBUG_LOGGING
    double preDeadzoneYaw = rawYawOffset;
    double preDeadzonePitch = rawPitchOffset;
#endif
    rawYawOffset = cameraunlock::math::ApplyDeadzone(rawYawOffset, m_deadzone.yaw);
    rawPitchOffset = cameraunlock::math::ApplyDeadzone(rawPitchOffset, m_deadzone.pitch);
    rawRollOffset = cameraunlock::math::ApplyDeadzone(rawRollOffset, m_deadzone.roll);

#if HEADTRACKING_DEBUG_LOGGING
    if (shouldLog) {
        HT_LOG_CAMERA("  After deadzone (dz=%.1f): yaw %.2f->%.2f, pitch %.2f->%.2f",
                    m_deadzone.yaw, preDeadzoneYaw, rawYawOffset, preDeadzonePitch, rawPitchOffset);
    }
#endif

    // Apply sensitivity
    rawYawOffset *= m_sensitivity.yaw;
    rawPitchOffset *= m_sensitivity.pitch;
    rawRollOffset *= m_sensitivity.roll;

    // Store raw values
    m_rawYaw = rawYawOffset;
    m_rawPitch = rawPitchOffset;
    m_rawRoll = rawRollOffset;

    // Baseline smoothing floor (kRemoteConnectionBaseline, 0.15) is always
    // applied — below it, high-refresh displays show jitter on wireless trackers.
    double effectiveSmoothing = cameraunlock::math::GetEffectiveSmoothing(m_smoothingFactor);

    m_smoothedYaw = cameraunlock::math::Smooth(m_smoothedYaw, rawYawOffset, effectiveSmoothing, static_cast<double>(deltaTime));
    m_smoothedPitch = cameraunlock::math::Smooth(m_smoothedPitch, rawPitchOffset, effectiveSmoothing, static_cast<double>(deltaTime));
    m_smoothedRoll = cameraunlock::math::Smooth(m_smoothedRoll, rawRollOffset, effectiveSmoothing, static_cast<double>(deltaTime));

#if HEADTRACKING_DEBUG_LOGGING
    if (shouldLog) {
        HT_LOG_CAMERA("  Final: yaw=%.2f pitch=%.2f (smoothing=%.3f)",
                    m_smoothedYaw, m_smoothedPitch, effectiveSmoothing);
    }
#endif

    // Apply rotation to game camera
    ApplyCameraRotation(m_smoothedYaw, m_smoothedPitch, m_smoothedRoll);
}

void CameraController::Recenter(const TrackingData& currentData) {
    if (!currentData.valid) {
        if (g_ConsolePrint) {
            g_ConsolePrint("HeadTracking: Cannot recenter - no valid tracking data");
        }
        return;
    }

    m_centerYaw = currentData.yaw;
    m_centerPitch = currentData.pitch;
    m_centerRoll = currentData.roll;

    // Reset smoothed values to prevent snap
    m_smoothedYaw = 0.0;
    m_smoothedPitch = 0.0;
    m_smoothedRoll = 0.0;

    m_hasValidCenter = true;

    if (g_ConsolePrint) {
        g_ConsolePrint("HeadTracking: Recentered (yaw=%.1f, pitch=%.1f, roll=%.1f)",
                       m_centerYaw, m_centerPitch, m_centerRoll);
    }
}

void CameraController::RecenterYawPitchOnly(const TrackingData& currentData) {
    if (!currentData.valid) {
        return;
    }

    // Update yaw and pitch centers, preserve roll for camera tilt during ADS
    m_centerYaw = currentData.yaw;
    m_centerPitch = currentData.pitch;
    m_smoothedYaw = 0.0;
    m_smoothedPitch = 0.0;
    // m_smoothedRoll preserved - camera will still tilt
}

void CameraController::SetEnabled(bool enabled) {
    if (m_enabled == enabled) {
        return;
    }

    m_enabled = enabled;

    if (!enabled) {
        // Reset smoothed values when disabling
        m_smoothedYaw = 0.0;
        m_smoothedPitch = 0.0;
        m_smoothedRoll = 0.0;
    }

    if (g_ConsolePrint) {
        g_ConsolePrint("HeadTracking: %s", enabled ? "Enabled" : "Disabled");
    }
}

void CameraController::SetSmoothing(double smoothing) {
    m_smoothingFactor = cameraunlock::math::Clamp(smoothing, 0.0, 0.99);
}

void CameraController::SetSensitivity(const SensitivitySettings& sensitivity) {
    m_sensitivity = sensitivity;
    m_sensitivity.yaw = cameraunlock::math::Clamp(m_sensitivity.yaw, 0.1, 5.0);
    m_sensitivity.pitch = cameraunlock::math::Clamp(m_sensitivity.pitch, 0.1, 5.0);
    m_sensitivity.roll = cameraunlock::math::Clamp(m_sensitivity.roll, 0.1, 5.0);
}

void CameraController::SetDeadzone(const DeadzoneSettings& deadzone) {
    m_deadzone = deadzone;
    m_deadzone.yaw = cameraunlock::math::Clamp(m_deadzone.yaw, 0.0, 30.0);
    m_deadzone.pitch = cameraunlock::math::Clamp(m_deadzone.pitch, 0.0, 30.0);
    m_deadzone.roll = cameraunlock::math::Clamp(m_deadzone.roll, 0.0, 30.0);
}

void CameraController::SetCameraMode(CameraMode mode) {
    if (m_cameraMode == mode) {
        return;
    }

    m_cameraMode = mode;

    if (g_ConsolePrint) {
        g_ConsolePrint("HeadTracking: Camera mode set to %s", CameraModeName(mode));
    }
}

void CameraController::SetWorldSpaceYaw(bool worldSpace) {
    m_worldSpaceYaw = worldSpace;
}

void CameraController::ToggleYawMode() {
    m_worldSpaceYaw = !m_worldSpaceYaw;

    if (g_ConsolePrint) {
        g_ConsolePrint("HeadTracking: Yaw mode: %s",
                       m_worldSpaceYaw ? "world-space (horizon-locked)" : "camera-local");
    }
}

void CameraController::ApplyCameraRotation(double yawOffset, double pitchOffset, double rollOffset) {
    static double lastYaw = 0.0;
    static double lastPitch = 0.0;

    // In decoupled mode: Store offsets for D3D9 EndScene hook to apply
    // DON'T modify player rotation - that would couple camera to movement
    if (m_cameraMode == CameraMode::Decoupled) {
        // Store offsets - the D3D9 EndScene hook will read these and apply to camera
        m_smoothedYaw = yawOffset;
        m_smoothedPitch = pitchOffset;

        HT_LOG_CAMERA("Decoupled mode - stored yaw=%.2f pitch=%.2f for D3D hook",
                    yawOffset, pitchOffset);

        // DO NOT modify player rotation - return here
        // The D3D9 EndScene hook will apply our offsets to the camera
        return;
    }

    // Get player
    uint8_t* player = *reinterpret_cast<uint8_t**>(GameOffsets::kPlayerBase);
    if (!player) {
        HT_LOG_CAMERA("ApplyCameraRotation: No player");
        return;
    }

    float* pRotX = reinterpret_cast<float*>(player + GameOffsets::kPlayerRotX);  // Pitch
    float* pRotZ = reinterpret_cast<float*>(player + GameOffsets::kPlayerRotZ);  // Yaw

    __try {
        if (m_cameraMode == CameraMode::BodyTracking) {
            // Body Tracking Mode v5 - HEAD BONE APPROACH (like FNVR)
            // Don't modify player.rotZ at all - leave movement direction alone
            // Instead, try to find and rotate the "Bip01 Head" bone directly
            // This separates visual head look from movement direction

            // For now, just apply pitch tracking (doesn't affect movement)
            // and leave yaw to mouse/stick (coupled to movement)

            float pitchRad = static_cast<float>(-pitchOffset * cameraunlock::math::kDegToRad);
            *pRotX += pitchRad - static_cast<float>(-lastPitch * cameraunlock::math::kDegToRad);

            // Clamp pitch
            if (*pRotX > kMaxPitchRad) *pRotX = kMaxPitchRad;
            if (*pRotX < -kMaxPitchRad) *pRotX = -kMaxPitchRad;

            lastPitch = pitchOffset;

            // Yaw is intentionally not applied in body tracking mode
            // Horizontal look follows body/movement direction
            // Decoupled mode handles free look via D3D9 hook instead

            HT_LOG_CAMERA("BodyTracking v5 - pitch=%.2f (yaw disabled, needs head bone)",
                        pitchOffset);
        } else {
            // Coupled mode: modify player rotation directly
            // This affects camera, crosshair, and movement direction

            // Calculate delta from last frame (we need to apply incremental changes)
            double deltaYaw = yawOffset - lastYaw;
            double deltaPitch = pitchOffset - lastPitch;
            lastYaw = yawOffset;
            lastPitch = pitchOffset;

            // Convert to radians (invert pitch for correct up/down)
            float yawRad = static_cast<float>(deltaYaw * cameraunlock::math::kDegToRad);
            float pitchRad = static_cast<float>(-deltaPitch * cameraunlock::math::kDegToRad);

            // Add our delta rotation to player rotation
            *pRotZ += yawRad;
            *pRotX += pitchRad;

            // Clamp pitch to prevent gimbal issues
            if (*pRotX > kMaxPitchRad) *pRotX = kMaxPitchRad;
            if (*pRotX < -kMaxPitchRad) *pRotX = -kMaxPitchRad;

            HT_LOG_CAMERA("Coupled mode - Applied delta yaw=%.3f pitch=%.3f", deltaYaw, deltaPitch);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        HT_LOG_CAMERA("FATAL: SEH exception in ApplyCameraRotation writing to player at %p", player);
        D3D9Hook::SignalFatalError("CameraController::ApplyCameraRotation");
    }

    (void)rollOffset;  // Roll not supported
}

}  // namespace HeadTracking
