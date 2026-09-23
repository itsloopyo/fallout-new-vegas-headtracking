#include <Windows.h>

#include "plugin.h"
#include "nvse_abi/PluginAPI.h"
#include "version.h"
#include "config.h"
#include "udp_receiver.h"
#include "camera_controller.h"
#include "game_state.h"
#include "hotkey_handler.h"
#include "tracking_data.h"
#include "d3d9_hook.h"
#include "d3d9_internal.h"
#include "debug_log.h"

#include <cameraunlock/data/position_data.h>
#include <cameraunlock/logging/file_log.h>
#include <cameraunlock/math/quat4.h>
#include <cameraunlock/time/qpc_clock.h>

#include <string>

namespace HeadTracking {

namespace culog = cameraunlock::logging;

constexpr float kMaxDeltaTime = 0.1f;

// OpenTrack sends position in centimeters; the processor/culling pipeline works
// in meters.
constexpr double kCmToM = 0.01;

// Global module handle
HMODULE g_hModule = nullptr;

HeadTrackingPlugin& HeadTrackingPlugin::Instance() {
    static HeadTrackingPlugin instance;
    return instance;
}

HeadTrackingPlugin::HeadTrackingPlugin()
    : m_config(nullptr)
    , m_udpReceiver(nullptr)
    , m_cameraController(nullptr)
    , m_gameState(nullptr)
    , m_hotkeyHandler(nullptr)
    , m_initialized(false)
    , m_gameLoaded(false)
    , m_lastUpdateTime(0)
    , m_configCheckInterval(5000)
    , m_lastConfigCheckTime(0) {
    // Create component instances (not initialized yet)
    m_config = std::make_unique<Config>();
    m_udpReceiver = std::make_unique<UdpReceiver>();
    m_cameraController = std::make_unique<CameraController>();
    m_gameState = std::make_unique<GameState>();
    m_hotkeyHandler = std::make_unique<HotkeyHandler>();
}

HeadTrackingPlugin::~HeadTrackingPlugin() {
    Shutdown();
}

bool HeadTrackingPlugin::Initialize(const NVSEInterface* nvse) {
    if (m_initialized) {
        return true;
    }

    if (!nvse) {
        return false;
    }

    // Confirm the messaging interface exists before we go further - game
    // lifecycle events (load/exit/main loop) are delivered through it, so
    // there is no point initializing without it.
    if (!nvse->QueryInterface(kInterface_Messaging)) {
        culog::Line("ERROR: NVSE has no messaging interface - head tracking is inactive");
        return false;
    }

    return Initialize();
}

// The loader-independent half. The proxy deployment has no script extender to
// interrogate, so it enters here directly.
bool HeadTrackingPlugin::Initialize() {
    if (m_initialized) {
        return true;
    }

    // Load configuration from INI file first - other components depend on it.
    std::string iniPath = GetINIPath();
    if (!m_config->Load(iniPath)) {
        if (g_ConsolePrint) {
            g_ConsolePrint("HeadTracking: ERROR - Failed to load config from %s", iniPath.c_str());
        }
        culog::Line("ERROR: failed to load config from %s - head tracking is inactive",
                    iniPath.c_str());
        return false;
    }
    culog::Line("Config loaded from %s", iniPath.c_str());

    // Non-fatal: if the port is held by another head-tracker the receiver keeps
    // a background thread retrying the bind every 5s and recovers on its own.
    uint16_t udpPort = m_config->GetUdpPort();
    m_udpReceiver->Initialize(udpPort);

    m_cameraController->Initialize();
    m_gameState->Initialize();
    m_hotkeyHandler->Initialize(m_cameraController.get(), m_gameState.get());

    // Apply loaded configuration to all components
    if (!m_config->ApplyToComponents(m_cameraController.get(), m_hotkeyHandler.get(),
                                      m_gameState.get(), m_udpReceiver.get())) {
        if (g_ConsolePrint) {
            g_ConsolePrint("HeadTracking: ERROR - Failed to apply configuration");
        }
        culog::Line("ERROR: failed to apply the loaded configuration - head tracking is inactive");
        return false;
    }

    m_poseInterpolator.Reset();

    // Initialize position processor (6DOF)
    ApplyPositionSettings();
    m_positionInterpolator.Reset();

    m_initialized = true;
    m_lastUpdateTime = cameraunlock::time::QpcNowMicros();
    m_lastConfigCheckTime = cameraunlock::time::QpcNowMicros();

    culog::Line("Plugin initialized; tracking starts when gameplay and tracker data are available.");

    return true;
}

void HeadTrackingPlugin::Shutdown() {
    if (!m_initialized) {
        return;
    }

    // Shutdown D3D hook
    D3D9Hook::Instance().Shutdown();

    // Shutdown UDP receiver
    if (m_udpReceiver) {
        m_udpReceiver->Shutdown();
    }

    m_initialized = false;
    m_gameLoaded = false;
}

void HeadTrackingPlugin::Update() {
#if HEADTRACKING_DEBUG_LOGGING
    static int frameCount = 0;
    frameCount++;
    bool shouldLog = (frameCount % 120 == 0);
    if (frameCount <= 5 || shouldLog) {
        HT_LOG_PLUGIN("Plugin::Update frame %d - init=%d gameLoaded=%d",
            frameCount, m_initialized ? 1 : 0, m_gameLoaded ? 1 : 0);
    }
#endif

    if (!m_initialized) {
        return;
    }

    // Calculate delta time using high-res timer
    uint64_t currentTime = cameraunlock::time::QpcNowMicros();
    float deltaTime = static_cast<float>(currentTime - m_lastUpdateTime) / 1000000.0f;
    m_lastUpdateTime = currentTime;

    // Clamp delta time to prevent large jumps
    if (deltaTime > kMaxDeltaTime) {
        deltaTime = kMaxDeltaTime;
    }

    // Check for config file changes periodically (not every frame to reduce I/O)
    if (m_config && (currentTime - m_lastConfigCheckTime >= m_configCheckInterval * 1000)) {
        m_lastConfigCheckTime = currentTime;
        CheckConfigReload();
    }

    // Update game state detection (must come before hotkey/camera updates)
    if (m_gameState) {
        m_gameState->Update();
    }

    // Process hotkeys
    if (m_hotkeyHandler && m_gameState->CanProcessInput()) {
        ApplyHotkeyAction(m_hotkeyHandler->Update());
    }

    // Poll UDP receiver for tracking data
    bool hasNewData = false;
    if (m_udpReceiver && m_udpReceiver->IsInitialized()) {
        hasNewData = m_udpReceiver->Poll();

        // Re-read every frame rather than once at startup: a player who swaps
        // a local OpenTrack instance for a phone on WiFi mid-session gets the
        // other smoothing parameter without restarting the game.
        const bool isRemote = m_udpReceiver->IsRemoteConnection();
        if (m_cameraController) {
            m_cameraController->SetIsRemoteConnection(isRemote);
        }
        m_positionProcessor.SetIsRemoteConnection(isRemote);
    }

#if HEADTRACKING_DEBUG_LOGGING
    if (shouldLog) {
        bool udpInit = m_udpReceiver && m_udpReceiver->IsInitialized();
        bool udpConn = m_udpReceiver && m_udpReceiver->IsConnected();
        bool shouldTrack = !m_gameState || m_gameState->ShouldTrack();
        HT_LOG_PLUGIN("Update frame %d: udpInit=%d udpConn=%d shouldTrack=%d hasNewData=%d",
                      frameCount, udpInit, udpConn, shouldTrack, hasNewData);
    }
#endif

    const bool suppressed = !m_gameLoaded || D3D9Internal::IsGamePaused() ||
        !m_gameState->ShouldTrack() || !m_cameraController->IsEnabled() || !m_udpReceiver->IsConnected();
    if (suppressed) {
        ResetTracking();
        return;
    }
    if (m_waitingForPose && !hasNewData) return;
    m_waitingForPose = false;

    // Update camera controller with latest tracking data
    if (m_cameraController && m_udpReceiver && m_udpReceiver->IsConnected()) {
        const TrackingData& data = m_udpReceiver->GetLatestData();

        // Interpolate to fill frame gaps when tracking rate < game FPS
        cameraunlock::InterpolatedPose interpolated = m_poseInterpolator.Update(
            static_cast<float>(data.yaw), static_cast<float>(data.pitch),
            static_cast<float>(data.roll), hasNewData, deltaTime);

        // Build interpolated tracking data
        TrackingData interpolatedData;
        interpolatedData.x = data.x;
        interpolatedData.y = data.y;
        interpolatedData.z = data.z;
        interpolatedData.yaw = interpolated.yaw;
        interpolatedData.pitch = interpolated.pitch;
        interpolatedData.roll = interpolated.roll;
        interpolatedData.valid = data.valid;

#if HEADTRACKING_DEBUG_LOGGING
        if (shouldLog) {
            HT_LOG_PLUGIN("  Camera Update: yaw=%.2f pitch=%.2f valid=%d (interpolated: yaw=%.2f pitch=%.2f)",
                          data.yaw, data.pitch, data.valid, interpolated.yaw, interpolated.pitch);
        }
#endif
        m_cameraController->Update(interpolatedData, deltaTime);

        if (m_positionEnabled && data.valid) {
            ProcessPositionTracking(data, hasNewData, deltaTime);
        }
    }
}

void HeadTrackingPlugin::ApplyHotkeyAction(HotkeyAction action) {
    if (action == HotkeyAction::Toggle) {
        ResetTracking();
    } else if (action == HotkeyAction::CycleTrackingMode) {
        // Three-state cycle:
        //   0 = normal (rotation + position)
        //   1 = rotation only (position disabled)
        //   2 = position only (rotation disabled)
        m_trackingModeCycle = (m_trackingModeCycle + 1) % 3;
        bool rotEnabled = (m_trackingModeCycle != 2);
        bool posEnabled = (m_trackingModeCycle != 1);

        if (m_cameraController) {
            m_cameraController->SetRotationEnabled(rotEnabled);
            if (!posEnabled) {
                m_cameraController->SetPositionOffset(0.0f, 0.0f, 0.0f);
            }
        }
        m_positionEnabled = posEnabled;
        m_positionInterpolator.Reset();

        const char* modeName =
            (m_trackingModeCycle == 0) ? "normal (rotation + position)" :
            (m_trackingModeCycle == 1) ? "rotation only" :
                                         "position only";
        HT_LOG_PLUGIN("Tracking mode: %s", modeName);
        if (g_ConsolePrint) {
            g_ConsolePrint("HeadTracking: Tracking mode: %s", modeName);
        }
    } else if (action == HotkeyAction::ReticleToggle) {
        D3D9Internal::g_reticleEnabled = !D3D9Internal::g_reticleEnabled;
        HT_LOG_PLUGIN("Reticle %s", D3D9Internal::g_reticleEnabled ? "enabled" : "disabled");
        if (g_ConsolePrint) {
            g_ConsolePrint("HeadTracking: Reticle %s", D3D9Internal::g_reticleEnabled ? "enabled" : "disabled");
        }
    } else if (action == HotkeyAction::ToggleYawMode) {
        if (m_cameraController) {
            m_cameraController->ToggleYawMode();
            HT_LOG_PLUGIN("Yaw mode: %s",
                          m_cameraController->IsWorldSpaceYaw() ? "world-space" : "camera-local");
        }
    }
}

void HeadTrackingPlugin::ProcessPositionTracking(const TrackingData& data, bool hasNewData, float deltaTime) {
    // Preserve timestamp across stale frames so the interpolator can distinguish
    // new UDP samples from repeated data (auto-timestamp would defeat this).
    if (hasNewData) {
        m_lastPositionTimestampUs = cameraunlock::PositionData::CurrentTimestamp();
    }
    cameraunlock::PositionData rawPos(
        static_cast<float>(data.x * kCmToM),
        static_cast<float>(data.y * kCmToM),
        static_cast<float>(data.z * kCmToM),
        m_lastPositionTimestampUs);
    cameraunlock::PositionData interpPos = m_positionInterpolator.Update(rawPos, deltaTime);

    // Head rotation quaternion drives body-relative offset orientation.
    double yawDeg = m_cameraController->GetCurrentYawOffset();
    double pitchDeg = m_cameraController->GetCurrentPitchOffset();
    double rollDeg = m_cameraController->GetCurrentRollOffset();
    cameraunlock::math::Quat4 headRotQ = cameraunlock::math::Quat4::FromYawPitchRoll(
        static_cast<float>(yawDeg * cameraunlock::math::kDegToRad),
        static_cast<float>(pitchDeg * cameraunlock::math::kDegToRad),
        static_cast<float>(rollDeg * cameraunlock::math::kDegToRad));

    if (cameraunlock::math::GetEffectiveSmoothing(m_config->GetLocalSmoothing(),
            m_config->GetRemoteSmoothing(), m_udpReceiver->IsRemoteConnection()) == 0.0) {
        m_positionProcessor.ResetSmoothing();
    }
    m_lastPositionOffset = m_positionProcessor.Process(interpPos, headRotQ, deltaTime);

    // Push position offset to camera controller for the D3D9 hook to read.
    m_cameraController->SetPositionOffset(
        m_lastPositionOffset.x, m_lastPositionOffset.y, m_lastPositionOffset.z);
}

void HeadTrackingPlugin::ApplyPositionSettings() {
    // Field by field rather than through the positional constructor: the
    // argument list is long enough that a value silently landing on the
    // neighbouring parameter would compile clean and only show up as wrong
    // position limits.
    //
    // The limits are deliberately ordered so the generous 0.40 sits on the
    // BACK limit: this mod feeds the processor a z whose forward lean is the
    // positive direction, and the processor clamps z as [-limit_z,
    // +limit_z_back]. Do not "fix" this by swapping them.
    cameraunlock::PositionSettings posSettings;
    posSettings.sensitivity_x = 1.0f;  // 1:1 physical mapping
    posSettings.sensitivity_y = 1.0f;
    posSettings.sensitivity_z = 1.0f;
    posSettings.limit_x = cameraunlock::PositionSettings{}.limit_x;
    // The clamp is [-limit_y_down, +limit_y] and limit_y_down carries its own
    // default, so mirror the one configured vertical limit the way
    // PositionSettings::Symmetric does. Left unset, raising LimitY widened the
    // upward budget only and downward travel stayed pinned at 0.20m.
    posSettings.limit_y = cameraunlock::PositionSettings{}.limit_y;
    posSettings.limit_y_down = cameraunlock::PositionSettings{}.limit_y_down;
    posSettings.limit_z = 0.10f;
    posSettings.limit_z_back = 0.40f;
    posSettings.invert_x = true;
    posSettings.invert_y = false;
    posSettings.invert_z = true;
    // Position uses the same two smoothing values as rotation; the connection
    // flag that picks between them is pushed from the receiver each frame.
    posSettings.local_smoothing = static_cast<float>(m_config->GetLocalSmoothing());
    posSettings.remote_smoothing = static_cast<float>(m_config->GetRemoteSmoothing());
    m_positionProcessor.SetSettings(posSettings);
}

void HeadTrackingPlugin::CheckConfigReload() {
    if (!m_config || !m_config->IsLoaded()) {
        return;
    }

    // Check if file has been modified
    if (m_config->HasFileChanged()) {
        if (!m_config->Reload()) {
            if (g_ConsolePrint) {
                g_ConsolePrint("HeadTracking: ERROR - Config reload failed, keeping previous settings");
            }
            return;
        }
        // Apply new settings to all components
        if (!m_config->ApplyToComponents(m_cameraController.get(), m_hotkeyHandler.get(),
                                          m_gameState.get(), m_udpReceiver.get())) {
            if (g_ConsolePrint) {
                g_ConsolePrint("HeadTracking: ERROR - Failed to apply reloaded config");
            }
        }

        // ApplyToComponents has no PositionProcessor parameter, so the reloaded
        // smoothing values would otherwise reach rotation only and leave
        // position on whatever was read at startup. Push them here as well or
        // the two halves of the pipeline drift apart for the rest of the
        // session and the camera swims.
        //
        // Unconditional on purpose: ApplyToComponents pushes the camera
        // smoothing before its hotkey validation can fail, so on a partial
        // failure rotation has already moved and position must follow it.
        ApplyPositionSettings();
    }
}

void HeadTrackingPlugin::OnGameLoaded() {
    m_gameLoaded = true;
    m_lastUpdateTime = cameraunlock::time::QpcNowMicros();
    m_poseInterpolator.Reset();

    // Reset cached UI pointers - they become stale after loading screens
    D3D9Hook::Instance().ResetUICache();

    // Log camera mode
    D3D9Hook::Instance().SetCameraController(m_cameraController.get());
    if (!D3D9Hook::Instance().Initialize()) {
        culog::Line("ERROR: render hooks failed: %s", D3D9Hook::Instance().GetErrorMessage());
    }
}

void HeadTrackingPlugin::OnGameExit() {
    m_gameLoaded = false;
}

std::string GetINIPath() {
    if (!g_hModule) {
        return "";
    }

    char dllPath[MAX_PATH];
    DWORD result = GetModuleFileNameA(g_hModule, dllPath, MAX_PATH);
    if (result == 0 || result >= MAX_PATH) {
        return "";
    }

    // The config is named for the mod, not for the file the mod was loaded as.
    // Deriving it from the DLL's own name was fine while the only deployment
    // was Data\NVSE\Plugins\HeadTracking.dll; the proxy deployment is called
    // dsound.dll, and that spelling sent it looking for DSOUND.ini and silently
    // gave every user defaults.
    std::string modulePath(dllPath);
    size_t slashPos = modulePath.find_last_of("\\/");
    const std::string dir =
        (slashPos == std::string::npos) ? std::string() : modulePath.substr(0, slashPos + 1);

    return dir + "HeadTracking.ini";
}

}  // namespace HeadTracking

namespace HeadTracking {
void HeadTrackingPlugin::ResetTracking() {
    D3D9Internal::ResetLeanClamp();
    m_cameraController->ResetTracking();
    m_poseInterpolator.Reset();
    m_positionInterpolator.Reset();
    m_positionProcessor.ResetSmoothing();
    m_waitingForPose = true;
}
}
