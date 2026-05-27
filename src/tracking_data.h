#pragma once

#include <cstdint>

namespace HeadTracking {

// OpenTrack UDP protocol data structure
// OpenTrack sends 48 bytes: 6 doubles (x, y, z, yaw, pitch, roll)
// Position is in centimeters, rotation in degrees
// Data is little-endian (matches x86/x64 Windows)
struct TrackingData {
    // Position (centimeters - matches the OpenTrack wire contract; the 6DOF
    // pipeline applies its own cm->m scale downstream)
    double x;      // Lateral position (cm) - left/right
    double y;      // Vertical position (cm) - up/down
    double z;      // Forward position (cm) - forward/back

    // Rotation (degrees)
    double yaw;    // Rotation around vertical axis
    double pitch;  // Rotation around lateral axis (looking up/down)
    double roll;   // Rotation around longitudinal axis (head tilt)

    // Data validity flag
    bool valid;

    TrackingData()
        : x(0.0)
        , y(0.0)
        , z(0.0)
        , yaw(0.0)
        , pitch(0.0)
        , roll(0.0)
        , valid(false) {
    }

    void Clear() {
        x = 0.0;
        y = 0.0;
        z = 0.0;
        yaw = 0.0;
        pitch = 0.0;
        roll = 0.0;
        valid = false;
    }
};

}  // namespace HeadTracking
