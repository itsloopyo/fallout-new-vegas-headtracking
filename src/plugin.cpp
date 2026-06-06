#include <Windows.h>

#include "plugin.h"
#include "../nvse/nvse/PluginAPI.h"
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
#include <cameraunlock/math/quat4.h>
#include <cameraunlock/time/qpc_clock.h>

#include <string>

namespace HeadTracking {

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
        return false;
    }

    // Load configuration from INI file first - other components depend on it.
    std::string iniPath = GetINIPath();
    if (!m_config->Load(iniPath)) {
        if (g_ConsolePrint) {
            g_ConsolePrint("HeadTracking: ERROR - Failed to load config from %s", iniPath.c_str());
        }
        return false;
    }

    // Non-fatal: if the port is held by another head-tracker the receiver keeps
    // a background thread retrying the bind every 5s and recovers on its own.
    // Aborting init here would make NVSE unload the plugin, so tracking could
    // never come back without restarting the game.
    uint16_t udpPort = m_config->GetUdpPort();
    m_udpReceiver->Initialize(udpPort);

    m_cameraController->Initialize();
    m_gameState->Initialize();
    m_hotkeyHandler->Initialize(m_cameraController.get(), m_udpReceiver.get(), m_gameState.get());

    // Apply loaded configuration to all components
    if (!m_config->ApplyToComponents(m_cameraController.get(), m_hotkeyHandler.get(),
                                      m_gameState.get(), m_udpReceiver.get())) {
        if (g_ConsolePrint) {
            g_ConsolePrint("HeadTracking: ERROR - Failed to apply configuration");
        }
        return false;
    }

    m_poseInterpolator.Reset();

    // Initialize position processor (6DOF)
    cameraunlock::PositionSettings posSettings(
        1.0f, 1.0f, 1.0f,            // sensitivity X, Y, Z (1:1 physical mapping)
        0.30f, 0.20f, 0.10f, 0.40f,  // limits X, Y, Z-forward, Z-back (inverted asymmetry)
        0.15f,                        // smoothing
        true, false, true             // invert X, Y, Z
    );
    m_positionProcessor.SetSettings(posSettings);
    m_positionInterpolator.Reset();

    m_initialized = true;
    m_lastUpdateTime = cameraunlock::time::QpcNowMicros();
    m_lastConfigCheckTime = cameraunlock::time::QpcNowMicros();

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

    if (!m_initialized || !m_gameLoaded) {
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
    // m_configCheckInterval is in ms, currentTime is in μs
    if (m_config && (currentTime - m_lastConfigCheckTime >= m_configCheckInterval * 1000)) {
        m_lastConfigCheckTime = currentTime;
        CheckConfigReload();
    }

    // Update game state detection (must come before hotkey/camera updates)
    if (m_gameState) {
        m_gameState->Update();

        // Recenter head tracking when loading finishes (player enters game world)
        if (m_gameState->JustFinishedLoading()) {
            if (m_cameraController && m_udpReceiver && m_udpReceiver->IsConnected()) {
                const TrackingData& data = m_udpReceiver->GetLatestData();
                if (data.valid) {
                    m_cameraController->Recenter(data);
                    SetPositionCenter(data);
                    m_positionInterpolator.Reset();
                    HT_LOG_PLUGIN("Auto-recentered after loading screen finished");
                    if (g_ConsolePrint) {
                        g_ConsolePrint("HeadTracking: Auto-recentered after loading");
                    }
                }
            }
        }
    }

    // Process hotkeys (handles recenter, toggle, etc.)
    if (m_hotkeyHandler) {
        ApplyHotkeyAction(m_hotkeyHandler->Update());
    }

    // Poll UDP receiver for tracking data
    bool hasNewData = false;
    if (m_udpReceiver && m_udpReceiver->IsInitialized()) {
        hasNewData = m_udpReceiver->Poll();
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

void HeadTrackingPlugin::SetPositionCenter(const TrackingData& data) {
    m_positionProcessor.SetCenter(cameraunlock::PositionData(
        static_cast<float>(data.x * kCmToM),
        static_cast<float>(data.y * kCmToM),
        static_cast<float>(data.z * kCmToM)));
}

void HeadTrackingPlugin::ApplyHotkeyAction(HotkeyAction action) {
    if (action == HotkeyAction::Recenter || action == HotkeyAction::Toggle) {
        m_poseInterpolator.Reset();
        m_positionInterpolator.Reset();
        if (action == HotkeyAction::Recenter && m_udpReceiver && m_udpReceiver->IsConnected()) {
            const TrackingData& recData = m_udpReceiver->GetLatestData();
            if (recData.valid) {
                SetPositionCenter(recData);
            }
        }
    } else if (action == HotkeyAction::CycleTrackingMode) {
        // Three-state cycle:
        //   0 = normal (rotation + position)
        //   1 = rotation only (position disabled)
        //   2 = position only (rotation disabled)
        m_trackingModeCycle = (m_trackingModeCycle + 1) % 3;
        bool rotEnabled = (m_trackingModeCycle != 2);
        bool posEnabled = (m_trackingModeCycle != 1);

        if (m_cameraController) {
            m_cameraController->SetEnabled(rotEnabled);
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

    m_lastPositionOffset = m_positionProcessor.Process(interpPos, headRotQ, deltaTime);

    // Push position offset to camera controller for the D3D9 hook to read.
    m_cameraController->SetPositionOffset(
        m_lastPositionOffset.x, m_lastPositionOffset.y, m_lastPositionOffset.z);
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
    }
}

void HeadTrackingPlugin::OnGameLoaded() {
    m_gameLoaded = true;
    m_lastUpdateTime = cameraunlock::time::QpcNowMicros();
    m_poseInterpolator.Reset();

    // Reset cached UI pointers - they become stale after loading screens
    D3D9Hook::Instance().ResetUICache();

    // Log camera mode
    if (m_cameraController) {
        const char* modeName = "Unknown";
        if (m_cameraController->GetCameraMode() == CameraMode::Coupled) {
            modeName = "Coupled";
            HT_LOG_PLUGIN("Camera mode: Coupled");
        } else if (m_cameraController->IsDecoupled()) {
            modeName = "Decoupled (D3D9 EndScene hook)";
            HT_LOG_PLUGIN("Camera mode: Decoupled - initializing D3D9 EndScene hook");

            // Initialize D3D9 EndScene hook for decoupled mode
            D3D9Hook::Instance().SetCameraController(m_cameraController.get());
            D3D9Hook::Instance().SetUdpReceiver(m_udpReceiver.get());
            if (D3D9Hook::Instance().Initialize()) {
                HT_LOG_PLUGIN("D3D9 EndScene hook initialized successfully");
                if (g_ConsolePrint) {
                    g_ConsolePrint("HeadTracking: D3D9 hook active for decoupled mode");
                }
            } else {
                HT_LOG_PLUGIN("ERROR: D3D9 hook initialization failed");
                if (g_ConsolePrint) {
                    g_ConsolePrint("HeadTracking: ERROR - D3D9 hook failed: %s",
                                   D3D9Hook::Instance().GetErrorMessage());
                }
            }
        } else if (m_cameraController->GetCameraMode() == CameraMode::BodyTracking) {
            modeName = "BodyTracking";
            HT_LOG_PLUGIN("Camera mode: BodyTracking");
        }
        if (g_ConsolePrint) {
            g_ConsolePrint("HeadTracking: Game loaded, camera mode: %s", modeName);
        }
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

    std::string iniPath(dllPath);
    size_t dotPos = iniPath.rfind('.');
    if (dotPos != std::string::npos) {
        iniPath = iniPath.substr(0, dotPos) + ".ini";
    } else {
        iniPath += ".ini";
    }

    return iniPath;
}

}  // namespace HeadTracking
