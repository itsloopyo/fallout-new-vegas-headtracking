#include <Windows.h>

#include "config.h"
#include "plugin.h"
#include "camera_controller.h"
#include "hotkey_handler.h"
#include "game_state.h"
#include "udp_receiver.h"

#include <fstream>

namespace HeadTracking {

// Default values (match HeadTracking.ini defaults)
constexpr uint16_t DEFAULT_UDP_PORT = 4242;
constexpr double DEFAULT_SENSITIVITY = 1.0;
constexpr double DEFAULT_SMOOTHING = 0.0;
constexpr double DEFAULT_DEADZONE = 0.0;
constexpr int DEFAULT_RECENTER_KEY = 0x24;              // Home
constexpr int DEFAULT_TOGGLE_KEY = 0x23;                // End
constexpr int DEFAULT_CYCLE_TRACKING_MODE_KEY = 0x21;   // Page Up
constexpr int DEFAULT_RETICLE_TOGGLE_KEY = 0x22;        // Page Down
// Insert, not the catalogue-standard Page Down (0x22): Page Down is already
// the reticle toggle in this mod, so the yaw-mode toggle takes the next free
// nav-cluster key. Chord stays in the T/Y/U/G/H/J cluster (Ctrl+Shift+U).
constexpr int DEFAULT_YAW_MODE_KEY = 0x2D;              // Insert
constexpr uint64_t DEFAULT_DEBOUNCE_MS = 200;
constexpr int DEFAULT_INPUT_BLOCK_MODE = 0;  // Never

Config::Config()
    : m_iniPath()
    , m_loaded(false)
    , m_udpPort(DEFAULT_UDP_PORT)
    , m_sensitivityYaw(DEFAULT_SENSITIVITY)
    , m_sensitivityPitch(DEFAULT_SENSITIVITY)
    , m_sensitivityRoll(DEFAULT_SENSITIVITY)
    , m_smoothing(DEFAULT_SMOOTHING)
    , m_deadzoneYaw(DEFAULT_DEADZONE)
    , m_deadzonePitch(DEFAULT_DEADZONE)
    , m_deadzoneRoll(DEFAULT_DEADZONE)
    , m_recenterKey(DEFAULT_RECENTER_KEY)
    , m_toggleKey(DEFAULT_TOGGLE_KEY)
    , m_cycleTrackingModeKey(DEFAULT_CYCLE_TRACKING_MODE_KEY)
    , m_reticleToggleKey(DEFAULT_RETICLE_TOGGLE_KEY)
    , m_yawModeKey(DEFAULT_YAW_MODE_KEY)
    , m_debounceMs(DEFAULT_DEBOUNCE_MS)
    , m_inputBlockMode(InputBlockMode::Never)
    , m_trackInThirdPerson(true)
    , m_trackInVATS(false)
    , m_pauseDuringCombat(false)
    , m_showMessages(true)
    , m_cameraMode(CameraMode::Decoupled)
    , m_worldSpaceYaw(true) {
}

Config::~Config() {
}

bool Config::Load(const std::string& iniPath) {
    if (iniPath.empty()) {
        if (g_ConsolePrint) {
            g_ConsolePrint("HeadTracking: ERROR - Config::Load called with empty path");
        }
        return false;
    }

    m_iniPath = iniPath;

    // Check if file exists - create default config if missing
    DWORD attrs = GetFileAttributesA(m_iniPath.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        if (g_ConsolePrint) {
            g_ConsolePrint("HeadTracking: Config file not found: %s", m_iniPath.c_str());
            g_ConsolePrint("HeadTracking: Creating default configuration...");
        }
        if (!CreateDefaultConfig()) {
            if (g_ConsolePrint) {
                g_ConsolePrint("HeadTracking: ERROR - Failed to create default config file");
            }
            return false;
        }
        if (g_ConsolePrint) {
            g_ConsolePrint("HeadTracking: Default config created successfully");
        }
    }

    // Open with the shared INI reader (also captures mod time for change detection)
    if (!m_ini.Open(m_iniPath)) {
        if (g_ConsolePrint) {
            g_ConsolePrint("HeadTracking: ERROR - Failed to open config file %s", m_iniPath.c_str());
        }
        return false;
    }

    if (g_ConsolePrint) {
        g_ConsolePrint("HeadTracking: Loading config from %s", m_iniPath.c_str());
    }

    // Network section
    m_udpPort = static_cast<uint16_t>(m_ini.ReadInt("Network", "Port", DEFAULT_UDP_PORT));

    // Sensitivity section
    m_sensitivityYaw = m_ini.ReadDouble("Sensitivity", "Yaw", DEFAULT_SENSITIVITY);
    m_sensitivityPitch = m_ini.ReadDouble("Sensitivity", "Pitch", DEFAULT_SENSITIVITY);
    m_sensitivityRoll = m_ini.ReadDouble("Sensitivity", "Roll", DEFAULT_SENSITIVITY);

    // Smoothing section
    m_smoothing = m_ini.ReadDouble("Smoothing", "Amount", DEFAULT_SMOOTHING);

    // Deadzone section
    m_deadzoneYaw = m_ini.ReadDouble("Deadzone", "Yaw", DEFAULT_DEADZONE);
    m_deadzonePitch = m_ini.ReadDouble("Deadzone", "Pitch", DEFAULT_DEADZONE);
    m_deadzoneRoll = m_ini.ReadDouble("Deadzone", "Roll", DEFAULT_DEADZONE);

    // Hotkeys section
    m_recenterKey = m_ini.ReadHex("Hotkeys", "Recenter", DEFAULT_RECENTER_KEY);
    m_toggleKey = m_ini.ReadHex("Hotkeys", "Toggle", DEFAULT_TOGGLE_KEY);
    m_cycleTrackingModeKey = m_ini.ReadHex("Hotkeys", "CycleTrackingMode", DEFAULT_CYCLE_TRACKING_MODE_KEY);
    m_reticleToggleKey = m_ini.ReadHex("Hotkeys", "ReticleToggle", DEFAULT_RETICLE_TOGGLE_KEY);
    m_yawModeKey = m_ini.ReadHex("Hotkeys", "YawModeKey", DEFAULT_YAW_MODE_KEY);
    m_debounceMs = static_cast<uint64_t>(m_ini.ReadInt("Hotkeys", "DebounceMs", static_cast<int>(DEFAULT_DEBOUNCE_MS)));

    // GameState section
    int inputBlockMode = m_ini.ReadInt("GameState", "InputBlockMode", DEFAULT_INPUT_BLOCK_MODE);
    switch (inputBlockMode) {
        case 0:
            m_inputBlockMode = InputBlockMode::Never;
            break;
        case 1:
            m_inputBlockMode = InputBlockMode::MenusOnly;
            break;
        case 2:
            m_inputBlockMode = InputBlockMode::AllDialogue;
            break;
        case 3:
            m_inputBlockMode = InputBlockMode::AllOverlays;
            break;
        default:
            if (g_ConsolePrint) {
                g_ConsolePrint("HeadTracking: ERROR - Invalid InputBlockMode %d (valid: 0-3)", inputBlockMode);
            }
            return false;
    }

    m_trackInThirdPerson = m_ini.ReadBool("GameState", "TrackInThirdPerson", true);
    m_trackInVATS = m_ini.ReadBool("GameState", "TrackInVATS", false);
    m_pauseDuringCombat = m_ini.ReadBool("GameState", "PauseDuringCombat", false);

    // Feedback section
    m_showMessages = m_ini.ReadBool("Feedback", "ShowMessages", true);

    // Camera section
    // CameraMode: 0 = Coupled (camera follows player rotation, affects aim)
    //             1 = Decoupled (camera independent, uses D3D hook)
    //             2 = BodyTracking (camera follows head, movement uses body direction)
    int cameraMode = m_ini.ReadInt("Camera", "Mode", 1);
    switch (cameraMode) {
        case 0:
            m_cameraMode = CameraMode::Coupled;
            break;
        case 1:
            m_cameraMode = CameraMode::Decoupled;
            break;
        case 2:
            m_cameraMode = CameraMode::BodyTracking;
            break;
        default:
            if (g_ConsolePrint) {
                g_ConsolePrint("HeadTracking: ERROR - Invalid Camera Mode %d (valid: 0-2)", cameraMode);
            }
            return false;
    }

    // WorldSpaceYaw: true = horizon-locked yaw (default), false = camera-local
    m_worldSpaceYaw = m_ini.ReadBool("Camera", "WorldSpaceYaw", true);

    // Validate values - FAIL FAST on invalid config
    auto inDoubleRange = [](double v, double lo, double hi, const char* name) -> bool {
        if (v < lo || v > hi) {
            if (g_ConsolePrint) {
                g_ConsolePrint("HeadTracking: ERROR - %s %.2f out of range (valid: %.2f to %.2f)", name, v, lo, hi);
            }
            return false;
        }
        return true;
    };
    auto keyInRange = [](int code, const char* name) -> bool {
        if (code < 0x01 || code > 0xFE) {
            if (g_ConsolePrint) {
                g_ConsolePrint("HeadTracking: ERROR - Invalid %s key 0x%02X (valid: 0x01-0xFE)", name, code);
            }
            return false;
        }
        return true;
    };

    if (m_udpPort == 0) {
        if (g_ConsolePrint) {
            g_ConsolePrint("HeadTracking: ERROR - Invalid UDP port 0 (must be non-zero)");
        }
        return false;
    }

    if (!inDoubleRange(m_sensitivityYaw,   0.1, 5.0,  "Sensitivity Yaw"))   return false;
    if (!inDoubleRange(m_sensitivityPitch, 0.1, 5.0,  "Sensitivity Pitch")) return false;
    if (!inDoubleRange(m_sensitivityRoll,  0.1, 5.0,  "Sensitivity Roll"))  return false;
    if (!inDoubleRange(m_smoothing,        0.0, 0.99, "Smoothing"))         return false;
    if (!inDoubleRange(m_deadzoneYaw,      0.0, 30.0, "Deadzone Yaw"))      return false;
    if (!inDoubleRange(m_deadzonePitch,    0.0, 30.0, "Deadzone Pitch"))    return false;
    if (!inDoubleRange(m_deadzoneRoll,     0.0, 30.0, "Deadzone Roll"))     return false;

    if (!keyInRange(m_recenterKey,          "recenter"))            return false;
    if (!keyInRange(m_toggleKey,            "toggle"))              return false;
    if (!keyInRange(m_cycleTrackingModeKey, "cycle tracking mode")) return false;
    if (!keyInRange(m_reticleToggleKey,     "reticle toggle"))      return false;
    if (!keyInRange(m_yawModeKey,           "yaw mode"))            return false;

    if (m_debounceMs < 50 || m_debounceMs > 2000) {
        if (g_ConsolePrint) {
            g_ConsolePrint("HeadTracking: ERROR - DebounceMs %llu out of range (valid: 50-2000)", m_debounceMs);
        }
        return false;
    }

    m_loaded = true;

    if (g_ConsolePrint) {
        g_ConsolePrint("HeadTracking: Config loaded successfully");
        g_ConsolePrint("HeadTracking:   UDP Port: %u", m_udpPort);
        g_ConsolePrint("HeadTracking:   Sensitivity: %.2f / %.2f / %.2f (yaw/pitch/roll)",
                       m_sensitivityYaw, m_sensitivityPitch, m_sensitivityRoll);
        g_ConsolePrint("HeadTracking:   Smoothing: %.2f", m_smoothing);
        g_ConsolePrint("HeadTracking:   Deadzone: %.2f / %.2f / %.2f",
                       m_deadzoneYaw, m_deadzonePitch, m_deadzoneRoll);
    }

    return true;
}

bool Config::Reload() {
    if (m_iniPath.empty()) {
        if (g_ConsolePrint) {
            g_ConsolePrint("HeadTracking: ERROR - Cannot reload config, no path set");
        }
        return false;
    }

    if (g_ConsolePrint) {
        g_ConsolePrint("HeadTracking: Reloading configuration...");
    }

    // Re-load from the same path
    return Load(m_iniPath);
}

bool Config::HasFileChanged() const {
    return m_ini.HasChanged();
}

bool Config::ApplyToComponents(CameraController* camera, HotkeyHandler* hotkey,
                               GameState* gameState, UdpReceiver* udpReceiver) {
    if (!m_loaded) {
        if (g_ConsolePrint) {
            g_ConsolePrint("HeadTracking: ERROR - Cannot apply config, not loaded");
        }
        return false;
    }

    // Apply to camera controller
    if (camera) {
        camera->SetSensitivity(GetSensitivity());
        camera->SetSmoothing(m_smoothing);
        camera->SetDeadzone(GetDeadzone());
        camera->SetCameraMode(m_cameraMode);
        camera->SetWorldSpaceYaw(m_worldSpaceYaw);
    }

    // Apply to hotkey handler - FAIL FAST if key codes are invalid
    if (hotkey) {
        if (!hotkey->SetRecenterKey(m_recenterKey)) {
            return false;
        }
        if (!hotkey->SetToggleKey(m_toggleKey)) {
            return false;
        }
        if (!hotkey->SetCycleTrackingModeKey(m_cycleTrackingModeKey)) {
            return false;
        }
        if (!hotkey->SetReticleToggleKey(m_reticleToggleKey)) {
            return false;
        }
        if (!hotkey->SetYawModeKey(m_yawModeKey)) {
            return false;
        }
        hotkey->SetDebounceTime(m_debounceMs);
        hotkey->SetShowMessages(m_showMessages);
    }

    // Apply to game state
    if (gameState) {
        gameState->SetInputBlockMode(m_inputBlockMode);
        gameState->SetTrackInThirdPerson(m_trackInThirdPerson);
        gameState->SetTrackInVATS(m_trackInVATS);
        gameState->SetPauseDuringCombat(m_pauseDuringCombat);
    }

    // Note: UDP port can't be changed at runtime without reinitializing the socket
    // That would require a full shutdown/init cycle which is disruptive
    // Users must restart the game to change UDP port
    (void)udpReceiver;

    if (g_ConsolePrint) {
        g_ConsolePrint("HeadTracking: Configuration applied to components");
    }

    return true;
}

SensitivitySettings Config::GetSensitivity() const {
    SensitivitySettings settings;
    settings.yaw = m_sensitivityYaw;
    settings.pitch = m_sensitivityPitch;
    settings.roll = m_sensitivityRoll;
    return settings;
}

DeadzoneSettings Config::GetDeadzone() const {
    DeadzoneSettings deadzone;
    deadzone.yaw = m_deadzoneYaw;
    deadzone.pitch = m_deadzonePitch;
    deadzone.roll = m_deadzoneRoll;
    return deadzone;
}

bool Config::CreateDefaultConfig() {
    if (m_iniPath.empty()) {
        return false;
    }

    std::ofstream file(m_iniPath);
    if (!file.is_open()) {
        return false;
    }

    file << "; HeadTracking Configuration\n";
    file << "; Auto-generated with default values\n";
    file << "\n";
    file << "[Network]\n";
    file << "; UDP port for OpenTrack data (default: 4242)\n";
    file << "Port=" << DEFAULT_UDP_PORT << "\n";
    file << "\n";
    file << "[Sensitivity]\n";
    file << "; Multipliers for each axis (0.1 to 5.0)\n";
    file << "Yaw=" << DEFAULT_SENSITIVITY << "\n";
    file << "Pitch=" << DEFAULT_SENSITIVITY << "\n";
    file << "Roll=" << DEFAULT_SENSITIVITY << "\n";
    file << "\n";
    file << "[Smoothing]\n";
    file << "; Smoothing amount (0.0 = instant, 0.99 = maximum smoothing)\n";
    file << "Amount=" << DEFAULT_SMOOTHING << "\n";
    file << "\n";
    file << "[Deadzone]\n";
    file << "; Deadzone thresholds in degrees (0.0 to 30.0)\n";
    file << "Yaw=" << DEFAULT_DEADZONE << "\n";
    file << "Pitch=" << DEFAULT_DEADZONE << "\n";
    file << "Roll=" << DEFAULT_DEADZONE << "\n";
    file << "\n";
    file << "[Hotkeys]\n";
    file << "; Nav-cluster virtual key codes (hex). Each action also accepts a\n";
    file << "; fixed Ctrl+Shift+<letter> chord (T/Y/G/H/U) which is not configurable.\n";
    file << "; Home=0x24, End=0x23, PageUp=0x21, PageDown=0x22, Insert=0x2D\n";
    file << "Recenter=0x24\n";
    file << "Toggle=0x23\n";
    file << "CycleTrackingMode=0x21\n";
    file << "ReticleToggle=0x22\n";
    file << "; Insert (Page Down is taken by ReticleToggle in this mod)\n";
    file << "YawModeKey=0x2D\n";
    file << "DebounceMs=" << DEFAULT_DEBOUNCE_MS << "\n";
    file << "\n";
    file << "[GameState]\n";
    file << "; InputBlockMode: 0=Never, 1=MenusOnly, 2=AllDialogue, 3=AllOverlays\n";
    file << "InputBlockMode=" << DEFAULT_INPUT_BLOCK_MODE << "\n";
    file << "TrackInThirdPerson=1\n";
    file << "TrackInVATS=0\n";
    file << "PauseDuringCombat=0\n";
    file << "\n";
    file << "[Feedback]\n";
    file << "ShowMessages=1\n";
    file << "\n";
    file << "[Camera]\n";
    file << "; Mode: 0=Coupled (affects aim), 1=Decoupled (free-look), 2=BodyTracking\n";
    file << "Mode=1\n";
    file << "; WorldSpaceYaw: 1 = horizon-locked yaw (default), 0 = camera-local\n";
    file << "WorldSpaceYaw=1\n";

    file.close();
    return true;
}

}  // namespace HeadTracking
