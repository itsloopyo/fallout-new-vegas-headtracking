#pragma once

#include <Windows.h>

#include "config.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include <cameraunlock/config/config_owner.h>
#include <cameraunlock/input/deferred_actions.h>
#include <cameraunlock/tracking/tracking_mode.h>

#include <cameraunlock/processing/pose_interpolator.h>
#include <cameraunlock/processing/position_processor.h>
#include <cameraunlock/processing/position_interpolator.h>
#include <cameraunlock/math/vec3.h>

// Forward declarations
struct NVSEInterface;
struct NVSEMessagingInterface;

namespace HeadTracking {

// Forward declarations for plugin components
class UdpReceiver;
class CameraController;
class GameState;
class HotkeyHandler;
struct TrackingData;

class HeadTrackingPlugin {
public:
    static HeadTrackingPlugin& Instance();

    bool Initialize(const NVSEInterface* nvse);

    // Loader-independent initialization, used by the proxy deployment.
    bool Initialize();
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

    // Reload HeadTracking.ini when it changed on disk, and apply what it holds.
    void CheckConfigReload();

    // Hand the settings to the components: smoothing, yaw mode, game state
    // gating, hotkeys and the tracking mode. Render thread, or init before it
    // runs.
    void ApplyConfig(const Config& config);

    void ApplyTrackingMode(cameraunlock::TrackingMode mode);

    // Hotkey actions, on the poller thread. The toggle changes this session
    // only; the mode and yaw toggles store the state they want, ask the render
    // thread to apply it, and save it.
    void OnToggleKey();
    void OnCycleTrackingModeKey();
    void OnToggleYawModeKey();
    void Save(const std::function<void(Config&)>& change);

    // Carries out what the hotkeys asked for since the last frame.
    void ApplyRequestedActions();
    void ResetTracking();
    bool m_waitingForPose = true;

    // Pushes the position limits, inversions and both smoothing values onto the
    // position processor. Called at init and again on every config reload, so
    // an edited smoothing value reaches position tracking as well as rotation.
    void ApplyPositionSettings();

    // Run the 6DOF position pipeline for this frame's tracking sample and push
    // the resulting offset to the camera controller.
    void ProcessPositionTracking(const TrackingData& data, bool hasNewData, float deltaTime);

    std::unique_ptr<cameraunlock::config::ConfigOwner<Config>> m_configOwner;
    // The settings the session runs on. Render thread (and init) only.
    Config m_config;
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

    // The mode and yaw toggles use the desired-state pattern: the poller thread
    // computes the next state from the one the render thread last applied, so
    // two presses before one frame take one step.
    cameraunlock::input::DeferredAction m_toggleRequest;
    cameraunlock::input::DeferredAction m_modeRequest;
    cameraunlock::input::DeferredAction m_yawRequest;
    std::atomic<cameraunlock::TrackingMode> m_appliedMode{cameraunlock::TrackingMode::RotationAndPosition};
    std::atomic<cameraunlock::TrackingMode> m_desiredMode{cameraunlock::TrackingMode::RotationAndPosition};
    std::atomic<bool> m_appliedWorldYaw{true};
    std::atomic<bool> m_desiredWorldYaw{true};

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

// HeadTracking.ini beside this DLL.
std::wstring ConfigPath();

}  // namespace HeadTracking
