#include "legacy_config.h"

#include <cameraunlock/config/ini_reader.h>

#include <cstdarg>
#include <cstdio>

namespace HeadTracking::legacy {

namespace {

constexpr uint16_t DEFAULT_UDP_PORT = 4242;
constexpr double DEFAULT_LOCAL_SMOOTHING = 0.0;
constexpr double DEFAULT_REMOTE_SMOOTHING = 0.15;
constexpr int DEFAULT_TOGGLE_KEY = 0x23;
constexpr int DEFAULT_CYCLE_TRACKING_MODE_KEY = 0x21;
constexpr int DEFAULT_RETICLE_TOGGLE_KEY = 0x22;
constexpr int DEFAULT_YAW_MODE_KEY = 0x2E;
constexpr uint64_t DEFAULT_DEBOUNCE_MS = 200;
constexpr int DEFAULT_INPUT_BLOCK_MODE = 0;

// The published ConfigDiag formatted into a 512-byte buffer, so a long message
// was cut there.
ReadResult Refuse(const char* fmt, ...) {
    char msg[512] = {};
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);
    return ReadResult{ReadStatus::Refused, msg};
}

}  // namespace

ReadResult Read(const std::string& iniPath, Config& c) {
    cameraunlock::IniReader ini;
    if (!ini.Open(iniPath)) {
        return ReadResult{ReadStatus::Absent, {}};
    }

    c.udpPort = static_cast<uint16_t>(ini.ReadInt("Network", "Port", DEFAULT_UDP_PORT));

    c.localSmoothing = ini.ReadDouble("Smoothing", "LocalSmoothing", DEFAULT_LOCAL_SMOOTHING);
    c.remoteSmoothing = ini.ReadDouble("Smoothing", "RemoteSmoothing", DEFAULT_REMOTE_SMOOTHING);

    c.toggleKey = ini.ReadHex("Hotkeys", "Toggle", DEFAULT_TOGGLE_KEY);
    c.cycleTrackingModeKey = ini.ReadHex("Hotkeys", "CycleTrackingMode", DEFAULT_CYCLE_TRACKING_MODE_KEY);
    c.reticleToggleKey = ini.ReadHex("Hotkeys", "ReticleToggle", DEFAULT_RETICLE_TOGGLE_KEY);
    c.yawModeKey = ini.ReadHex("Hotkeys", "YawModeKey", DEFAULT_YAW_MODE_KEY);
    c.debounceMs = static_cast<uint64_t>(ini.ReadInt("Hotkeys", "DebounceMs", static_cast<int>(DEFAULT_DEBOUNCE_MS)));

    int inputBlockMode = ini.ReadInt("GameState", "InputBlockMode", DEFAULT_INPUT_BLOCK_MODE);
    switch (inputBlockMode) {
        case 0:
            c.inputBlockMode = InputBlockMode::Never;
            break;
        case 1:
            c.inputBlockMode = InputBlockMode::MenusOnly;
            break;
        case 2:
            c.inputBlockMode = InputBlockMode::AllDialogue;
            break;
        case 3:
            c.inputBlockMode = InputBlockMode::AllOverlays;
            break;
        default:
            return Refuse("Invalid InputBlockMode %d (valid: 0-3)", inputBlockMode);
    }

    c.trackInThirdPerson = ini.ReadBool("GameState", "TrackInThirdPerson", true);
    c.trackInVATS = ini.ReadBool("GameState", "TrackInVATS", false);
    c.pauseDuringCombat = ini.ReadBool("GameState", "PauseDuringCombat", false);

    c.showMessages = ini.ReadBool("Feedback", "ShowMessages", true);

    c.worldSpaceYaw = ini.ReadBool("Camera", "WorldSpaceYaw", true);

    if (c.udpPort == 0) {
        return Refuse("Invalid UDP port 0 (must be non-zero)");
    }

    // A NaN passes both comparisons, so the published build accepted it.
    const auto outOfRange = [](double v, double lo, double hi) { return v < lo || v > hi; };
    if (outOfRange(c.localSmoothing, 0.0, 1.0)) {
        return Refuse("%s %.2f out of range (valid: %.2f to %.2f)", "LocalSmoothing", c.localSmoothing, 0.0, 1.0);
    }
    if (outOfRange(c.remoteSmoothing, 0.0, 1.0)) {
        return Refuse("%s %.2f out of range (valid: %.2f to %.2f)", "RemoteSmoothing", c.remoteSmoothing, 0.0, 1.0);
    }

    const auto keyOutOfRange = [](int code) { return code < 0x01 || code > 0xFE; };
    if (keyOutOfRange(c.toggleKey)) {
        return Refuse("Invalid %s key 0x%02X (valid: 0x01-0xFE)", "toggle", c.toggleKey);
    }
    if (keyOutOfRange(c.cycleTrackingModeKey)) {
        return Refuse("Invalid %s key 0x%02X (valid: 0x01-0xFE)", "cycle tracking mode", c.cycleTrackingModeKey);
    }
    if (keyOutOfRange(c.reticleToggleKey)) {
        return Refuse("Invalid %s key 0x%02X (valid: 0x01-0xFE)", "reticle toggle", c.reticleToggleKey);
    }
    if (keyOutOfRange(c.yawModeKey)) {
        return Refuse("Invalid %s key 0x%02X (valid: 0x01-0xFE)", "yaw mode", c.yawModeKey);
    }

    if (c.debounceMs < 50 || c.debounceMs > 2000) {
        return Refuse("DebounceMs %llu out of range (valid: 50-2000)", c.debounceMs);
    }

    return ReadResult{};
}

std::vector<Key> ReadKeys() {
    return {
        {"Network", "Port"},
        {"Smoothing", "LocalSmoothing"},
        {"Smoothing", "RemoteSmoothing"},
        {"Hotkeys", "Toggle"},
        {"Hotkeys", "CycleTrackingMode"},
        {"Hotkeys", "ReticleToggle"},
        {"Hotkeys", "YawModeKey"},
        {"Hotkeys", "DebounceMs"},
        {"GameState", "InputBlockMode"},
        {"GameState", "TrackInThirdPerson"},
        {"GameState", "TrackInVATS"},
        {"GameState", "PauseDuringCombat"},
        {"Feedback", "ShowMessages"},
        {"Camera", "WorldSpaceYaw"},
    };
}

}  // namespace HeadTracking::legacy
