// fnv_config_oracle <list>: reads each path the list file names, one per line,
// as v0.3.1 did at startup, and prints one record per path (record.h). A path
// with no file behaves as it did in the published build: Load writes the
// first-run file there and reads it back.

#include "published_config.h"
#include "../record.h"

#include <Windows.h>

#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>

namespace {

using fnv_differential::Bindings;
using fnv_differential::Bits;
using fnv_differential::Flag;
using fnv_differential::Hex;
using fnv_differential::kCtrlShift;
using fnv_differential::Record;

Record Read(const std::string& path) {
    published::g_diagnostics.clear();
    const bool present = GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES;
    published::Config config;
    const bool loaded = config.Load(path);

    Record record;
    if (!loaded) {
        record["status"] = "refused";
        std::string reason;
        for (const auto& diagnostic : published::g_diagnostics) {
            if (diagnostic.level == "ERROR") reason = diagnostic.message;
        }
        record["reason"] = reason;
        return record;
    }
    record["status"] = present ? "usable" : "absent";

    record["field.udpPort"] = std::to_string(config.m_udpPort);
    record["field.localSmoothing"] = Bits(config.m_localSmoothing);
    record["field.remoteSmoothing"] = Bits(config.m_remoteSmoothing);
    record["field.toggleKey"] = Hex(config.m_toggleKey);
    record["field.cycleTrackingModeKey"] = Hex(config.m_cycleTrackingModeKey);
    record["field.reticleToggleKey"] = Hex(config.m_reticleToggleKey);
    record["field.yawModeKey"] = Hex(config.m_yawModeKey);
    record["field.debounceMs"] = std::to_string(config.m_debounceMs);
    record["field.inputBlockMode"] = std::to_string(static_cast<int>(config.m_inputBlockMode));
    record["field.trackInThirdPerson"] = Flag(config.m_trackInThirdPerson);
    record["field.trackInVATS"] = Flag(config.m_trackInVATS);
    record["field.pauseDuringCombat"] = Flag(config.m_pauseDuringCombat);
    record["field.showMessages"] = Flag(config.m_showMessages);
    record["field.worldSpaceYaw"] = Flag(config.m_worldSpaceYaw);
    record["field.adsMode"] = cameraunlock::ads::AdsModeValue(config.m_adsMode);
    record["field.adsModeKey"] = config.m_adsModeKey;

    // v0.3.1's HeadTrackingPlugin::Initialize: the camera controller starts
    // enabled, the mode cycle at 0 (rotation and position), and
    // ApplyToComponents hands it the yaw mode and the ADS mode.
    record["startup.enabled"] = "1";
    record["startup.mode"] = "RotationAndPosition";
    record["startup.worldSpaceYaw"] = Flag(config.m_worldSpaceYaw);
    record["startup.adsMode"] = cameraunlock::ads::AdsModeValue(config.m_adsMode);

    // v0.3.1's HotkeyHandler: each action's nav-cluster code, which does not
    // fire while Ctrl and Shift are both held, and its fixed Ctrl+Shift letter.
    // The ADS cycle was Insert and Ctrl+Shift+U, neither configurable.
    record["hotkey.Toggle"] = Bindings({{0, config.m_toggleKey}, {kCtrlShift, 'Y'}});
    record["hotkey.CycleTrackingMode"] = Bindings({{0, config.m_cycleTrackingModeKey}, {kCtrlShift, 'G'}});
    record["hotkey.ReticleToggle"] = Bindings({{0, config.m_reticleToggleKey}, {kCtrlShift, 'H'}});
    record["hotkey.YawMode"] = Bindings({{0, config.m_yawModeKey}, {kCtrlShift, 'J'}});
    record["hotkey.CycleAdsMode"] = Bindings({{0, VK_INSERT}, {kCtrlShift, 'U'}});
    return record;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: fnv_config_oracle <file listing one path per line>\n");
        return 2;
    }
    std::ifstream list(argv[1]);
    if (!list) {
        std::fprintf(stderr, "cannot open %s\n", argv[1]);
        return 2;
    }
    std::string path;
    while (std::getline(list, path)) {
        if (path.empty()) continue;
        std::cout << fnv_differential::Serialize(Read(path));
    }
    return 0;
}
