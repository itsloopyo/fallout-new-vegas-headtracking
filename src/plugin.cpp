#include <Windows.h>

#include "plugin.h"
#include "nvse_abi/PluginAPI.h"
#include "version.h"
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

#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

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
    : m_udpReceiver(nullptr)
    , m_cameraController(nullptr)
    , m_gameState(nullptr)
    , m_hotkeyHandler(nullptr)
    , m_initialized(false)
    , m_gameLoaded(false)
    , m_lastUpdateTime(0)
    , m_configCheckInterval(5000)
    , m_lastConfigCheckTime(0) {
    // Create component instances (not initialized yet)
    m_udpReceiver = std::make_unique<UdpReceiver>();
    m_cameraController = std::make_unique<CameraController>();
    m_gameState = std::make_unique<GameState>();
    m_hotkeyHandler = std::make_unique<HotkeyHandler>(HotkeyHandler::Actions{
        [this] { OnToggleKey(); },
        [this] { OnCycleTrackingModeKey(); },
        [this] { OnToggleYawModeKey(); },
    });
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

    // The config first - other components depend on it. Load imports an older
    // file and creates files, so it runs here on the init thread and never in
    // DllMain.
    const std::filesystem::path configFolder = ConfigFolder();
    m_configOwner = std::make_unique<cameraunlock::config::ConfigOwner<Config>>(
        ConfigOwnerOptions(configFolder, cameraunlock::config::DefaultsFile::PerUser()));
    const cameraunlock::config::ConfigLoadResult<Config> loaded = m_configOwner->Load();
    for (const std::string& line : loaded.log) culog::Line("%s", line.c_str());
    if (!loaded.reason.empty()) culog::Line("%s", loaded.reason.c_str());
    if (loaded.status == cameraunlock::config::ConfigLoadStatus::LegacyRefused) {
        // What the published build did with a file it refused.
        culog::Line("ERROR: failed to load config from %ls - head tracking is inactive",
                    (configFolder / kLegacyFileName).c_str());
        return false;
    }
    culog::Line("Config %s: %ls", cameraunlock::config::ConfigLoadStatusName(loaded.status),
                (configFolder / kConfigFileName).c_str());
    m_config = loaded.config;

    // Non-fatal: if the port is held by another head-tracker the receiver keeps
    // a background thread retrying the bind every 5s and recovers on its own.
    m_udpReceiver->Initialize(m_config.udp_port);

    m_cameraController->Initialize();
    m_gameState->Initialize();
    ApplyConfig(m_config);
    m_cameraController->SetEnabled(m_config.enable_on_startup);
    m_hotkeyHandler->Start();

    m_poseInterpolator.Reset();
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

    m_hotkeyHandler->Stop();

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
    if (currentTime - m_lastConfigCheckTime >= m_configCheckInterval * 1000) {
        m_lastConfigCheckTime = currentTime;
        CheckConfigReload();
    }

    // Update game state detection (must come before hotkey/camera updates)
    if (m_gameState) {
        m_gameState->Update();
    }

    // Hotkeys fire on the poller thread and act only while the game state
    // allows input; what they asked for is applied here, on the render thread.
    m_hotkeyHandler->SetInputAllowed(m_gameState->CanProcessInput());
    ApplyRequestedActions();

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

void HeadTrackingPlugin::OnToggleKey() {
    m_toggleRequest.Request();
}

void HeadTrackingPlugin::OnCycleTrackingModeKey() {
    // Rotation and position -> rotation only -> position only -> rotation and position.
    const auto next = static_cast<cameraunlock::TrackingMode>((static_cast<int>(m_appliedMode.load()) + 1) % 3);
    m_desiredMode.store(next);
    m_modeRequest.Request();
    const cameraunlock::TrackingModeChannels channels = cameraunlock::EncodeTrackingMode(next);
    Save([channels](Config& config) {
        config.rotation_enabled = channels.rotation_enabled;
        config.position_enabled = channels.position_enabled;
    });
}

void HeadTrackingPlugin::OnToggleYawModeKey() {
    const bool next = !m_appliedWorldYaw.load();
    m_desiredWorldYaw.store(next);
    m_yawRequest.Request();
    Save([next](Config& config) { config.world_space_yaw = next; });
}

void HeadTrackingPlugin::Save(const std::function<void(Config&)>& change) {
    const cameraunlock::config::ConfigSaveResult saved = m_configOwner->Save(change);
    // A save that succeeds can carry a line too, naming a row that stopped
    // following Defaults.ini.
    for (const std::string& line : saved.log) culog::Line("%s", line.c_str());
    if (saved.status != cameraunlock::config::ConfigSaveStatus::Saved) culog::Line("%s", saved.reason.c_str());
}

void HeadTrackingPlugin::ApplyRequestedActions() {
    if (m_toggleRequest.Consume()) {
        const bool enable = !m_cameraController->IsEnabled();
        m_cameraController->SetEnabled(enable);
        ResetTracking();
        culog::Line("Head tracking %s", enable ? "enabled" : "disabled");
    }
    if (m_modeRequest.Consume()) {
        ApplyTrackingMode(m_desiredMode.load());
    }
    if (m_yawRequest.Consume()) {
        const bool worldSpace = m_desiredWorldYaw.load();
        m_cameraController->SetWorldSpaceYaw(worldSpace);
        m_appliedWorldYaw.store(worldSpace);
        culog::Line("Yaw mode: %s", worldSpace ? "world-space (horizon-locked)" : "camera-local");
    }
}

void HeadTrackingPlugin::ApplyTrackingMode(cameraunlock::TrackingMode mode) {
    const cameraunlock::TrackingModeChannels channels = cameraunlock::EncodeTrackingMode(mode);
    m_cameraController->SetRotationEnabled(channels.rotation_enabled);
    if (!channels.position_enabled) {
        m_cameraController->SetPositionOffset(0.0f, 0.0f, 0.0f);
    }
    m_positionEnabled = channels.position_enabled;
    m_positionInterpolator.Reset();
    m_appliedMode.store(mode);
    const char* name = mode == cameraunlock::TrackingMode::RotationAndPosition ? "rotation and position"
                       : mode == cameraunlock::TrackingMode::RotationOnly      ? "rotation only"
                                                                               : "position only";
    culog::Line("Tracking mode: %s", name);
}

void HeadTrackingPlugin::ApplyConfig(const Config& config) {
    m_cameraController->SetLocalSmoothing(config.local_smoothing);
    m_cameraController->SetRemoteSmoothing(config.remote_smoothing);
    m_cameraController->SetWorldSpaceYaw(config.world_space_yaw);
    m_appliedWorldYaw.store(config.world_space_yaw);
    m_desiredWorldYaw.store(config.world_space_yaw);

    m_gameState->SetInputBlockMode(config.input_block_mode);
    m_gameState->SetTrackInThirdPerson(config.track_in_third_person);
    m_gameState->SetTrackInVATS(config.track_in_vats);
    m_gameState->SetPauseDuringCombat(config.pause_during_combat);

    m_hotkeyHandler->Bind(config);

    const cameraunlock::TrackingMode mode = StartupTrackingMode(config);
    m_desiredMode.store(mode);
    ApplyTrackingMode(mode);

    ApplyPositionSettings();
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

    if (cameraunlock::math::GetEffectiveSmoothing(m_config.local_smoothing,
            m_config.remote_smoothing, m_udpReceiver->IsRemoteConnection()) == 0.0) {
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
    posSettings.local_smoothing = static_cast<float>(m_config.local_smoothing);
    posSettings.remote_smoothing = static_cast<float>(m_config.remote_smoothing);
    m_positionProcessor.SetSettings(posSettings);
}

void HeadTrackingPlugin::CheckConfigReload() {
    if (!m_configOwner->FileChanged()) {
        return;
    }
    const cameraunlock::config::ConfigReloadResult<Config> reloaded = m_configOwner->Reload();
    for (const std::string& line : reloaded.log) culog::Line("%s", line.c_str());
    if (!reloaded.reason.empty()) culog::Line("%s", reloaded.reason.c_str());
    if (!reloaded.config) {
        return;
    }
    // The UDP port is bound once at startup; a changed port applies at the
    // next launch.
    m_config = *reloaded.config;
    ApplyConfig(m_config);
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

std::filesystem::path ConfigFolder() {
    if (!g_hModule) {
        throw std::logic_error("ConfigFolder needs the module handle DllMain records");
    }
    std::vector<wchar_t> buffer(MAX_PATH);
    for (;;) {
        const DWORD written = GetModuleFileNameW(g_hModule, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (written == 0) {
            throw std::runtime_error("GetModuleFileNameW failed: Windows error " + std::to_string(GetLastError()));
        }
        if (written < buffer.size()) {
            buffer.resize(written);
            break;
        }
        buffer.resize(buffer.size() * 2);
    }

    return std::filesystem::path(std::wstring(buffer.begin(), buffer.end())).parent_path();
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
