#include "udp_receiver.h"
#include "plugin.h"
#include "debug_log.h"

#include <string>

namespace HeadTracking {

UdpReceiver::~UdpReceiver() {
    Shutdown();
}

bool UdpReceiver::Initialize(uint16_t port) {
    if (m_started) {
        return true;
    }

    m_port = port;
    m_latestData.Clear();
    m_lastTimestampUs = 0;

    // Surface the core's bind-failure / retry / recovery messages through the
    // plugin's existing log sinks. This callback also fires from the core's
    // background retry thread, so both sinks must be safe off the game thread:
    // g_ConsolePrint is null in shipping builds and the debug log compiles out.
    m_core.SetLog([](const std::string& msg) {
        if (g_ConsolePrint) {
            g_ConsolePrint("HeadTracking: %s", msg.c_str());
        }
        HT_LOG_UDP("%s", msg.c_str());
    });

    bool bound = m_core.Start(port);
    m_started = true;

    if (g_ConsolePrint) {
        if (bound) {
            g_ConsolePrint("HeadTracking: UDP receiver listening on port %d", m_port);
        } else {
            g_ConsolePrint("HeadTracking: UDP port %d in use - retrying in background", m_port);
        }
    }

    return true;
}

void UdpReceiver::Shutdown() {
    if (!m_started) {
        return;
    }

    m_core.Stop();
    m_started = false;
    m_latestData.Clear();
    m_lastTimestampUs = 0;
}

bool UdpReceiver::Poll() {
    if (!m_started) {
        return false;
    }

    // GetLastReceiveTimestamp stays 0 until the first packet arrives, which
    // also covers the window where the port is still being retried.
    int64_t ts = m_core.GetLastReceiveTimestamp();
    if (ts == 0) {
        return false;
    }

    // The core applies no offset (the plugin recenters via CameraController),
    // so GetRotation returns the raw OpenTrack pose.
    float yaw, pitch, roll;
    if (m_core.GetRotation(yaw, pitch, roll)) {
        m_latestData.yaw = yaw;
        m_latestData.pitch = pitch;
        m_latestData.roll = roll;

        // The core parser converts OpenTrack position from centimeters to
        // meters; convert back so TrackingData keeps the centimeter contract
        // the downstream 6DOF pipeline (which applies its own cm->m scale)
        // expects. Skipping this would shrink positional tracking 100x.
        float px, py, pz;
        if (m_core.GetPosition(px, py, pz)) {
            m_latestData.x = px * 100.0f;
            m_latestData.y = py * 100.0f;
            m_latestData.z = pz * 100.0f;
        }

        m_latestData.valid = true;
    }

    bool isNewSample = (ts != m_lastTimestampUs);
    m_lastTimestampUs = ts;
    return isNewSample;
}

}  // namespace HeadTracking
