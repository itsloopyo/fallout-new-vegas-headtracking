// The published reader: HeadTracking.ini as v0.3.1 read it, the newest published
// build of this mod, and the dev pre-release, whose src/config.cpp is the same
// file. Config::Load, Config::CreateDefaultConfig, WarnRetiredSmoothingKey, the
// constructor and the default constants are copied byte for byte from
// `git show v0.3.1:src/config.cpp`. Around them: the Config members from
// v0.3.1's config.h, InputBlockMode from its game_state.h, ConfigDiag keeping
// its messages instead of logging them, and a null g_ConsolePrint as in the
// shipped build. The core headers it includes are core's at c480d8a, the
// commit v0.3.1 pins, under core-c480d8a/.
//
// Compiled into fnv_config_oracle only, never beside current core: the
// c480d8a config_key_schema.g.h defines cameraunlock::ResolveConfigKey
// differently from the current one.

#include "published_config.h"

#include <Windows.h>

#include <cameraunlock/config/config_key_schema.g.h>
#include <cameraunlock/math/smoothing_utils.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <fstream>

namespace published {

std::vector<Diagnostic> g_diagnostics;

namespace {

void (*g_ConsolePrint)(const char* fmt, ...) = nullptr;

void ConfigDiag(const char* level, const char* fmt, ...) {
    char msg[512] = {};
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);
    g_diagnostics.push_back({level, msg});
}

}  // namespace

// Default values (match HeadTracking.ini defaults)
constexpr uint16_t DEFAULT_UDP_PORT = 4242;
constexpr double DEFAULT_LOCAL_SMOOTHING = cameraunlock::math::kDefaultLocalSmoothing;
constexpr double DEFAULT_REMOTE_SMOOTHING = cameraunlock::math::kDefaultRemoteSmoothing;
constexpr int DEFAULT_TOGGLE_KEY = 0x23;                // End
constexpr int DEFAULT_CYCLE_TRACKING_MODE_KEY = 0x21;   // Page Up
constexpr int DEFAULT_RETICLE_TOGGLE_KEY = 0x22;        // Page Down
constexpr int DEFAULT_YAW_MODE_KEY = 0x2E;              // Delete
constexpr uint64_t DEFAULT_DEBOUNCE_MS = 200;
constexpr int DEFAULT_INPUT_BLOCK_MODE = 0;  // Never

Config::Config()
    : m_iniPath()
    , m_loaded(false)
    , m_udpPort(DEFAULT_UDP_PORT)
    , m_localSmoothing(DEFAULT_LOCAL_SMOOTHING)
    , m_remoteSmoothing(DEFAULT_REMOTE_SMOOTHING)
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
    , m_worldSpaceYaw(true) {
}

Config::~Config() {
}

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

    // Network section
    m_udpPort = static_cast<uint16_t>(m_ini.ReadInt("Network", "Port", DEFAULT_UDP_PORT));

    // Smoothing section
    m_localSmoothing = m_ini.ReadDouble("Smoothing", "LocalSmoothing", DEFAULT_LOCAL_SMOOTHING);
    m_remoteSmoothing = m_ini.ReadDouble("Smoothing", "RemoteSmoothing", DEFAULT_REMOTE_SMOOTHING);

    WarnRetiredSmoothingKey(m_ini, "Smoothing", "Amount");

    // Hotkeys section
    m_toggleKey = m_ini.ReadHex("Hotkeys", "Toggle", DEFAULT_TOGGLE_KEY);
    m_cycleTrackingModeKey = m_ini.ReadHex("Hotkeys", "CycleTrackingMode", DEFAULT_CYCLE_TRACKING_MODE_KEY);
    m_reticleToggleKey = m_ini.ReadHex("Hotkeys", "ReticleToggle", DEFAULT_RETICLE_TOGGLE_KEY);
    m_yawModeKey = m_ini.ReadHex("Hotkeys", "YawModeKey", DEFAULT_YAW_MODE_KEY);
    if (m_yawModeKey == VK_INSERT) {
        ConfigDiag("WARNING", "Insert now cycles ADS mode; yaw mode moved to Delete / Ctrl+Shift+J");
        m_yawModeKey = VK_DELETE;
    }
    char entries[4096] = {};
    const DWORD count = GetPrivateProfileSectionA("Camera", entries, sizeof(entries), m_iniPath.c_str());
    if (count >= sizeof(entries) - 2) {
        ConfigDiag("ERROR", "[Camera] section exceeds 4094 bytes");
        return false;
    }
    m_adsMode = cameraunlock::ads::kDefaultAdsMode;
    m_adsModeKey = "ads_mode";
    for (const char* entry = entries; *entry; entry += strlen(entry) + 1) {
        const std::string line(entry);
        const size_t equals = line.find('=');
        const char* key = cameraunlock::ResolveConfigKey(line.substr(0, equals));
        if (key && std::string(key) == cameraunlock::config_keys::kAdsMode && equals != std::string::npos) {
            const std::string value = line.substr(equals + 1);
            m_adsMode = cameraunlock::ads::ParseAdsMode(value.c_str());
            m_adsModeKey = line.substr(0, equals);
        }
    }
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
            ConfigDiag("ERROR", "Invalid InputBlockMode %d (valid: 0-3)", inputBlockMode);
            return false;
    }

    m_trackInThirdPerson = m_ini.ReadBool("GameState", "TrackInThirdPerson", true);
    m_trackInVATS = m_ini.ReadBool("GameState", "TrackInVATS", false);
    m_pauseDuringCombat = m_ini.ReadBool("GameState", "PauseDuringCombat", false);

    // Feedback section
    m_showMessages = m_ini.ReadBool("Feedback", "ShowMessages", true);

    // WorldSpaceYaw: true = horizon-locked yaw (default), false = camera-local
    m_worldSpaceYaw = m_ini.ReadBool("Camera", "WorldSpaceYaw", true);

    // Validate values - FAIL FAST on invalid config
    auto inDoubleRange = [](double v, double lo, double hi, const char* name) -> bool {
        if (v < lo || v > hi) {
            ConfigDiag("ERROR", "%s %.2f out of range (valid: %.2f to %.2f)", name, v, lo, hi);
            return false;
        }
        return true;
    };
    auto keyInRange = [](int code, const char* name) -> bool {
        if (code < 0x01 || code > 0xFE) {
            ConfigDiag("ERROR", "Invalid %s key 0x%02X (valid: 0x01-0xFE)", name, code);
            return false;
        }
        return true;
    };

    if (m_udpPort == 0) {
        ConfigDiag("ERROR", "Invalid UDP port 0 (must be non-zero)");
        return false;
    }

    if (!inDoubleRange(m_localSmoothing,   0.0, 1.0,  "LocalSmoothing"))    return false;
    if (!inDoubleRange(m_remoteSmoothing,  0.0, 1.0,  "RemoteSmoothing"))   return false;

    if (!keyInRange(m_toggleKey,            "toggle"))              return false;
    if (!keyInRange(m_cycleTrackingModeKey, "cycle tracking mode")) return false;
    if (!keyInRange(m_reticleToggleKey,     "reticle toggle"))      return false;
    if (!keyInRange(m_yawModeKey,           "yaw mode"))            return false;

    if (m_debounceMs < 50 || m_debounceMs > 2000) {
        ConfigDiag("ERROR", "DebounceMs %llu out of range (valid: 50-2000)", m_debounceMs);
        return false;
    }

    m_loaded = true;

    if (g_ConsolePrint) {
        g_ConsolePrint("HeadTracking: Config loaded successfully");
        g_ConsolePrint("HeadTracking:   UDP Port: %u", m_udpPort);
        g_ConsolePrint("HeadTracking:   Smoothing: %.2f local / %.2f remote",
                       m_localSmoothing, m_remoteSmoothing);

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
    file << "; End=0x23, PageUp=0x21, PageDown=0x22, Delete=0x2E; Insert cycles ADS\n";
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
    file << "; Insert / Ctrl+Shift+U: paused, marker, tracked\n";
    file << "ads_mode=paused\n";

    file.close();
    return true;
}

}  // namespace published
