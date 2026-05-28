#pragma once

// The bound UDP socket and the automatic bind-retry behaviour live in the
// shared core receiver. This class adapts cameraunlock::UdpReceiver to the
// polling, TrackingData-based call style the rest of the plugin is built
// around. The core header pulls in WinSock2.h (ahead of Windows.h) via
// socket_types.h, preserving the include order consumers rely on.
#include <cameraunlock/protocol/udp_receiver.h>

#include <Windows.h>

#include "tracking_data.h"

#include <cstdint>

namespace HeadTracking {

class UdpReceiver {
public:
    UdpReceiver() = default;
    ~UdpReceiver();

    // Disable copying/moving (owns the threaded core receiver)
    UdpReceiver(const UdpReceiver&) = delete;
    UdpReceiver& operator=(const UdpReceiver&) = delete;
    UdpReceiver(UdpReceiver&&) = delete;
    UdpReceiver& operator=(UdpReceiver&&) = delete;

    // Starts the shared receiver on the specified port. If the port is held by
    // another process, the core receiver keeps a background thread retrying the
    // bind every 5s and recovers on its own; this returns regardless so the
    // plugin stays alive and tracking resumes the moment the port frees up.
    // Always succeeds from the caller's perspective.
    bool Initialize(uint16_t port);

    // Stops the shared receiver and joins its threads.
    void Shutdown();

    // Refreshes the latest snapshot from the core receiver.
    // Returns true when a new sample arrived since the previous call.
    // Should be called every frame from the main loop.
    bool Poll();

    // Most recently received tracking data (holds last known pose when idle).
    const TrackingData& GetLatestData() const { return m_latestData; }

    // True if data was received within the core's connection timeout.
    bool IsConnected() const { return m_core.IsReceiving(); }

    // True once Initialize has run (bound or retrying in the background).
    bool IsInitialized() const { return m_started; }

private:
    cameraunlock::UdpReceiver m_core;
    TrackingData m_latestData;
    int64_t m_lastTimestampUs = 0;
    uint16_t m_port = 0;
    bool m_started = false;
};

}  // namespace HeadTracking
