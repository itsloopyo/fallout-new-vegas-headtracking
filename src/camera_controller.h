#pragma once

#include <Windows.h>

#include "tracking_data.h"

#include <cstdint>

namespace HeadTracking {

// Camera mode determines how head tracking is applied
enum class CameraMode {
    // Coupled: Head tracking modifies player rotation
    // Camera, crosshair, and movement direction all follow head
    Coupled,

    // Decoupled (Free Look): Head tracking only affects view direction
    // Crosshair and movement direction stay fixed to body orientation
    // Requires D3D hook to modify view matrix independently
    Decoupled,

    // BodyTracking: Camera follows head, but movement uses separate body direction
    // Body direction updates when player actively moves + turns with mouse
    // Looking around doesn't change movement direction
    BodyTracking
};

inline const char* CameraModeName(CameraMode mode) {
    switch (mode) {
        case CameraMode::Coupled:      return "Coupled";
        case CameraMode::Decoupled:    return "Decoupled (Free Look)";
        case CameraMode::BodyTracking: return "Body Tracking";
    }
    return "Unknown";
}

// Sensitivity settings (multipliers)
struct SensitivitySettings {
    double yaw;
    double pitch;
    double roll;

    SensitivitySettings()
        : yaw(1.0)
        , pitch(1.0)
        , roll(1.0) {
    }
};

// Deadzone settings (degrees) - ignore small movements
struct DeadzoneSettings {
    double yaw;
    double pitch;
    double roll;

    DeadzoneSettings()
        : yaw(0.5)
        , pitch(0.5)
        , roll(0.5) {
    }
};

class CameraController {
public:
    CameraController();
    ~CameraController();

    // Disable copying
    CameraController(const CameraController&) = delete;
    CameraController& operator=(const CameraController&) = delete;
    CameraController(CameraController&&) = delete;
    CameraController& operator=(CameraController&&) = delete;

    // Initialize the camera controller
    void Initialize();

    // Main update function - called every frame with tracking data.
    // deltaTime is in seconds.
    void Update(const TrackingData& data, float deltaTime);

    // Recenter tracking - sets current head position as neutral
    void Recenter(const TrackingData& currentData);

    // Recenter yaw and pitch only - preserves roll for ADS camera tilt
    void RecenterYawPitchOnly(const TrackingData& currentData);

    // Enable/disable head tracking
    void SetEnabled(bool enabled);
    bool IsEnabled() const { return m_enabled; }

    // Smoothing factor (0.0 = no smoothing, 1.0 = maximum smoothing)
    void SetSmoothing(double smoothing);

    // Sensitivity settings
    void SetSensitivity(const SensitivitySettings& sensitivity);

    // Deadzone settings
    void SetDeadzone(const DeadzoneSettings& deadzone);

    // Camera mode (coupled vs decoupled/free-look)
    void SetCameraMode(CameraMode mode);
    CameraMode GetCameraMode() const { return m_cameraMode; }

    // Yaw mode: true = world-space (horizon-locked, default), false = camera-local.
    // Read by the D3D9 culling hook when composing the view rotation.
    void SetWorldSpaceYaw(bool worldSpace);
    bool IsWorldSpaceYaw() const { return m_worldSpaceYaw; }
    void ToggleYawMode();

    // Get current applied offsets (used by D3D hook in decoupled mode)
    double GetCurrentYawOffset() const { return m_smoothedYaw; }
    double GetCurrentPitchOffset() const { return m_smoothedPitch; }
    double GetCurrentRollOffset() const { return m_smoothedRoll; }

    // Position offset (meters) - set by plugin, applied by D3D9 hook
    void SetPositionOffset(float x, float y, float z) { m_posX = x; m_posY = y; m_posZ = z; }
    float GetPositionX() const { return m_posX; }
    float GetPositionY() const { return m_posY; }
    float GetPositionZ() const { return m_posZ; }

    // Check if currently applying rotation
    bool IsActive() const { return m_enabled && m_hasValidCenter; }

    // Check if in decoupled (free-look) mode
    bool IsDecoupled() const { return m_cameraMode == CameraMode::Decoupled; }

private:
    // Apply the calculated rotation offset to the game camera
    void ApplyCameraRotation(double yawOffset, double pitchOffset, double rollOffset);

    // State
    bool m_enabled;
    bool m_initialized;
    bool m_hasValidCenter;

    // Center position (neutral head position)
    double m_centerYaw;
    double m_centerPitch;
    double m_centerRoll;

    // Current raw offsets (before smoothing)
    double m_rawYaw;
    double m_rawPitch;
    double m_rawRoll;

    // Smoothed offsets (applied to camera)
    double m_smoothedYaw;
    double m_smoothedPitch;
    double m_smoothedRoll;

    // Settings
    double m_smoothingFactor;
    SensitivitySettings m_sensitivity;
    DeadzoneSettings m_deadzone;

    // Camera mode (coupled or decoupled)
    CameraMode m_cameraMode;

    // Yaw mode: true = world-space (horizon-locked), false = camera-local
    bool m_worldSpaceYaw;

    // Position offset (meters) - computed by plugin, consumed by D3D9 hook
    float m_posX = 0.0f;
    float m_posY = 0.0f;
    float m_posZ = 0.0f;
};

}  // namespace HeadTracking
