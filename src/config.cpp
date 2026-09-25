#include <Windows.h>

#include "config.h"
#include "plugin.h"
#include "camera_controller.h"
#include "hotkey_handler.h"
#include "game_state.h"
#include "udp_receiver.h"
#include "legacy_config/legacy_config.h"

#include <cameraunlock/logging/file_log.h>
#include <cameraunlock/math/smoothing_utils.h>

#include <cstdarg>
#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <string>

namespace HeadTracking {

namespace culog = cameraunlock::logging;

// Every reason Config::Load can fail has to reach the file log. g_ConsolePrint
// is null in a shipping build (see udp_receiver.cpp), so routing a validation
// failure only there left the user with plugin.cpp's "failed to load config -
// head tracking is inactive" and nothing naming the key at fault.
static void ConfigDiag(const char* level, const char* fmt, ...) {
    char msg[512] = {};
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);
    culog::Line("config: %s - %s", level, msg);
    if (g_ConsolePrint) {
        g_ConsolePrint("HeadTracking: %s - %s", level, msg);
    }
}

// Default values (match HeadTracking.ini defaults)
constexpr uint16_t DEFAULT_UDP_PORT = 4242;
constexpr double DEFAULT_LOCAL_SMOOTHING = cameraunlock::math::kDefaultLocalSmoothing;
constexpr double DEFAULT_REMOTE_SMOOTHING = cameraunlock::math::kDefaultRemoteSmoothing;
constexpr uint64_t DEFAULT_DEBOUNCE_MS = 200;
constexpr int DEFAULT_INPUT_BLOCK_MODE = 0;  // Never

Config::Config()
    : m_iniPath()
    , m_loaded(false) {
}

Config::~Config() {
}

static InputBlockMode ToInputBlockMode(legacy::InputBlockMode mode) {
    switch (mode) {
        case legacy::InputBlockMode::Never:
            return InputBlockMode::Never;
        case legacy::InputBlockMode::MenusOnly:
            return InputBlockMode::MenusOnly;
        case legacy::InputBlockMode::AllDialogue:
            return InputBlockMode::AllDialogue;
        case legacy::InputBlockMode::AllOverlays:
            return InputBlockMode::AllOverlays;
    }
    throw std::logic_error("legacy InputBlockMode " + std::to_string(static_cast<int>(mode)) + " has no runtime mode");
}

// Warned once per process rather than once per load: config is reloadable, and
// repeating this on every reload buries it.
//
// The old value is deliberately NOT migrated into the new keys. The single
// Smoothing value carried a hidden 0.15 floor, so the number in an existing
// config does not mean what it used to: copying it across would hand a local
// user smoothing they never chose under the new semantics, and copying it into
// only one of the two keys would be a guess about which connection they were on.
static void WarnRetiredSmoothingKey(const cameraunlock::IniReader& ini,
                                    const char* section, const char* key) {
    static bool warned = false;
    if (warned) return;
    if (ini.ReadString(section, key, "").empty()) return;
    warned = true;
    ConfigDiag("WARNING",
        "Config key [%s] %s has been retired and is IGNORED. "
        "Smoothing is now two keys: LocalSmoothing (default 0, applies to a tracker "
        "on this machine) and RemoteSmoothing (default 0.15, applies to a tracker on "
        "the network). The old value is not migrated because the semantics changed - "
        "it carried a hidden 0.15 floor that no longer exists. Set the two new keys.",
        section, key);
}

bool Config::Load(const std::string& iniPath) {
    if (iniPath.empty()) {
        ConfigDiag("ERROR", "Config::Load called with empty path");
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
            ConfigDiag("ERROR", "Failed to create default config file at %s", m_iniPath.c_str());
            return false;
        }
        if (g_ConsolePrint) {
            g_ConsolePrint("HeadTracking: Default config created successfully");
        }
    }

    // Open with the shared INI reader (also captures mod time for change detection)
    if (!m_ini.Open(m_iniPath)) {
        ConfigDiag("ERROR", "Failed to open config file %s", m_iniPath.c_str());
        return false;
    }

    if (g_ConsolePrint) {
        g_ConsolePrint("HeadTracking: Loading config from %s", m_iniPath.c_str());
    }

    if (!m_ini.ReadString("Sensitivity", "Yaw", "").empty() ||
        !m_ini.ReadString("Deadzone", "Yaw", "").empty() ||
        !m_ini.ReadString("Camera", "Mode", "").empty()) {
        ConfigDiag("WARNING", "Sensitivity, Deadzone and Camera.Mode are retired and ignored. Configure pose shaping in the tracker; tracking now always leaves player aim unchanged.");
    }

    WarnRetiredSmoothingKey(m_ini, "Smoothing", "Amount");

    const legacy::ReadResult read = legacy::Read(m_iniPath, m_values);
    if (read.status == legacy::ReadStatus::Absent) {
        ConfigDiag("ERROR", "Failed to open config file %s", m_iniPath.c_str());
        return false;
    }
    if (read.status == legacy::ReadStatus::Refused) {
        ConfigDiag("ERROR", "%s", read.error.c_str());
        return false;
    }

    m_loaded = true;

    if (g_ConsolePrint) {
        g_ConsolePrint("HeadTracking: Config loaded successfully");
        g_ConsolePrint("HeadTracking:   UDP Port: %u", m_values.udpPort);
        g_ConsolePrint("HeadTracking:   Smoothing: %.2f local / %.2f remote",
                       m_values.localSmoothing, m_values.remoteSmoothing);

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
        camera->SetLocalSmoothing(m_values.localSmoothing);
        camera->SetRemoteSmoothing(m_values.remoteSmoothing);
        camera->SetWorldSpaceYaw(m_values.worldSpaceYaw);
    }

    // Apply to hotkey handler - FAIL FAST if key codes are invalid
    if (hotkey) {
        if (!hotkey->SetToggleKey(m_values.toggleKey)) {
            return false;
        }
        if (!hotkey->SetCycleTrackingModeKey(m_values.cycleTrackingModeKey)) {
            return false;
        }
        if (!hotkey->SetReticleToggleKey(m_values.reticleToggleKey)) {
            return false;
        }
        if (!hotkey->SetYawModeKey(m_values.yawModeKey)) {
            return false;
        }
        hotkey->SetDebounceTime(m_values.debounceMs);
        hotkey->SetShowMessages(m_values.showMessages);
    }

    // Apply to game state
    if (gameState) {
        gameState->SetInputBlockMode(ToInputBlockMode(m_values.inputBlockMode));
        gameState->SetTrackInThirdPerson(m_values.trackInThirdPerson);
        gameState->SetTrackInVATS(m_values.trackInVATS);
        gameState->SetPauseDuringCombat(m_values.pauseDuringCombat);
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
    file << "[Smoothing]\n";
    file << "; Smoothing applied when the tracker runs on this machine (loopback).\n";
    file << "; 0 = no smoothing, 1 = heavy. Covers rotation and position.\n";
    file << "LocalSmoothing=" << DEFAULT_LOCAL_SMOOTHING << "\n";
    file << "; Smoothing applied when the tracker is a remote device on the network.\n";
    file << "; 0 = no smoothing, 1 = heavy. Covers rotation and position.\n";
    file << "RemoteSmoothing=" << DEFAULT_REMOTE_SMOOTHING << "\n";
    file << "\n";
    file << "[Hotkeys]\n";
    file << "; Nav-cluster virtual key codes (hex). Each action also accepts a\n";
    file << "; fixed Ctrl+Shift+<letter> chord (Y/G/H/J) which is not configurable.\n";
    file << "; End=0x23, PageUp=0x21, PageDown=0x22, Delete=0x2E\n";
    file << "Toggle=0x23\n";
    file << "CycleTrackingMode=0x21\n";
    file << "ReticleToggle=0x22\n";
    file << "; Delete / Ctrl+Shift+J\n";
    file << "YawModeKey=0x2E\n";
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
    file << "; WorldSpaceYaw: 1 = horizon-locked yaw (default), 0 = camera-local\n";
    file << "WorldSpaceYaw=1\n";

    file.close();
    return true;
}

}  // namespace HeadTracking
