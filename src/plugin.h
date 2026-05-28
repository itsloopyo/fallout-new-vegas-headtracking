#pragma once

#include <Windows.h>

#include <cstdint>
#include <memory>
#include <string>

#include <cameraunlock/processing/pose_interpolator.h>
#include <cameraunlock/processing/position_processor.h>
#include <cameraunlock/processing/position_interpolator.h>
#include <cameraunlock/math/vec3.h>

// Forward declarations
struct NVSEInterface;
struct NVSEMessagingInterface;

namespace HeadTracking {

// Forward declarations for plugin components
class Config;
class UdpReceiver;
class CameraController;
class GameState;
class HotkeyHandler;
enum class HotkeyAction;
struct TrackingData;

class HeadTrackingPlugin {
public:
    static HeadTrackingPlugin& Instance();

    bool Initialize(const NVSEInterface* nvse);
    void Shutdown();
    void Update();
    void OnGameLoaded();
    void OnGameExit();

    bool IsInitialized() const { return m_initialized; }

private:
    HeadTrackingPlugin();
    ~HeadTrackingPlugin();

    HeadTrackingPlugin(const HeadTrackingPlugin&) = delete;
    HeadTrackingPlugin& operator=(const HeadTrackingPlugin&) = delete;
    HeadTrackingPlugin(HeadTrackingPlugin&&) = delete;
    HeadTrackingPlugin& operator=(HeadTrackingPlugin&&) = delete;

    // Check for config file changes and reload if needed
    void CheckConfigReload();

    // Carry out the effect of a hotkey press (recenter, toggle, mode cycle, ...).
    void ApplyHotkeyAction(HotkeyAction action);

    // Run the 6DOF position pipeline for this frame's tracking sample and push
    // the resulting offset to the camera controller.
    void ProcessPositionTracking(const TrackingData& data, bool hasNewData, float deltaTime);

    // Set the position-tracking neutral point from the current head pose.
    void SetPositionCenter(const TrackingData& data);

    std::unique_ptr<Config> m_config;
    std::unique_ptr<UdpReceiver> m_udpReceiver;
    std::unique_ptr<CameraController> m_cameraController;
    std::unique_ptr<GameState> m_gameState;
    std::unique_ptr<HotkeyHandler> m_hotkeyHandler;

    cameraunlock::PoseInterpolator m_poseInterpolator;

    // Position processing (6DOF)
    cameraunlock::PositionProcessor m_positionProcessor;
    cameraunlock::PositionInterpolator m_positionInterpolator;
    cameraunlock::math::Vec3 m_lastPositionOffset;
    int64_t m_lastPositionTimestampUs = 0;
    bool m_positionEnabled = true;

    // Tracking-mode cycle state advanced by Page Up / Ctrl+Shift+G:
    // 0 = normal (rotation + position), 1 = rotation only, 2 = position only.
    int m_trackingModeCycle = 0;

    bool m_initialized;
    bool m_gameLoaded;
    uint64_t m_lastUpdateTime;

    // Config auto-reload settings
    uint64_t m_configCheckInterval;   // Interval between file checks (ms)
    uint64_t m_lastConfigCheckTime;   // Last time we checked for file changes
};

// Global module handle (set in DllMain)
extern HMODULE g_hModule;

// Console print function pointer - always nullptr since NVSE doesn't expose this
// All if(g_ConsolePrint) checks will fail, effectively disabling console output
inline void (*g_ConsolePrint)(const char* fmt, ...) = nullptr;

// Get INI file path (next to DLL)
std::string GetINIPath();

}  // namespace HeadTracking
