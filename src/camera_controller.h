#pragma once

#include <Windows.h>

#include "tracking_data.h"

#include <cstdint>

namespace HeadTracking {

class CameraController {
public:
    CameraController();
    ~CameraController();

    void ResetTracking();
    void SetRotationEnabled(bool enabled) { m_rotationEnabled = enabled; }

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

    // Enable/disable head tracking
    void SetEnabled(bool enabled);
    bool IsEnabled() const { return m_enabled; }

    // Smoothing factors (0.0 = no smoothing, 1.0 = maximum smoothing). Which
    // one is used depends on where the tracking arrives from, set below.
    void SetLocalSmoothing(double smoothing);
    void SetRemoteSmoothing(double smoothing);

    // True when the tracker is a remote device on the network rather than an
    // app on this machine. Fed from the receiver every frame, so switching
    // trackers mid-session switches the smoothing parameter with it.
    void SetIsRemoteConnection(bool isRemote) { m_remoteConnection = isRemote; }

    // Yaw mode: true = world-space (horizon-locked, default), false = camera-local.
    // Read by the D3D9 culling hook when composing the view rotation.
    void SetWorldSpaceYaw(bool worldSpace);
    bool IsWorldSpaceYaw() const { return m_worldSpaceYaw; }

    // Get current applied offsets (used by D3D hook in decoupled mode)
    double GetCurrentYawOffset() const { return m_rotationEnabled ? m_smoothedYaw : 0.0; }
    double GetCurrentPitchOffset() const { return m_rotationEnabled ? m_smoothedPitch : 0.0; }
    double GetCurrentRollOffset() const { return m_rotationEnabled ? m_smoothedRoll : 0.0; }

    // Position offset (meters) - set by plugin, applied by D3D9 hook
    void SetPositionOffset(float x, float y, float z) { m_posX = x; m_posY = y; m_posZ = z; }
    float GetPositionX() const { return m_posX; }
    float GetPositionY() const { return m_posY; }
    float GetPositionZ() const { return m_posZ; }

    // Check if currently applying rotation
    bool IsActive() const { return m_enabled && m_hasTrackingData; }

private:
    bool m_rotationEnabled = true;

    // State
    bool m_enabled;
    bool m_initialized;
    bool m_hasTrackingData;

    // Current raw offsets (before smoothing)
    double m_rawYaw;
    double m_rawPitch;
    double m_rawRoll;

    // Smoothed offsets (applied to camera)
    double m_smoothedYaw;
    double m_smoothedPitch;
    double m_smoothedRoll;

    // Settings
    double m_localSmoothing;
    double m_remoteSmoothing;
    bool m_remoteConnection;
    // Yaw mode: true = world-space (horizon-locked), false = camera-local
    bool m_worldSpaceYaw;

    // Position offset (meters) - computed by plugin, consumed by D3D9 hook
    float m_posX = 0.0f;
    float m_posY = 0.0f;
    float m_posZ = 0.0f;
};

}  // namespace HeadTracking
