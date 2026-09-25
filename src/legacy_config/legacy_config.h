#pragma once

// The pre-canonical HeadTracking.ini reader, frozen. It reads a file exactly as
// the reader at commit ecb14e4 did, through the same IniReader calls in the same
// order, and must never change: the canonical config's legacy import runs it on
// every file an older build wrote. Three things differ from that reader. It
// fills this frozen Config rather than the runtime one, it writes nothing (no
// first-run file, no log line), and the refusal it would have logged comes back
// in the result instead.
//
// The warnings about retired keys ([Sensitivity] Yaw, [Deadzone] Yaw,
// [Camera] Mode, [Smoothing] Amount) are not here. They read no setting, and
// the published build ignored those keys.

#include <cstdint>
#include <string>
#include <vector>

namespace HeadTracking::legacy {

enum class InputBlockMode {
    Never,
    MenusOnly,
    AllDialogue,
    AllOverlays,
};

// The runtime Config's fields and defaults at ecb14e4.
struct Config {
    uint16_t udpPort = 4242;
    double localSmoothing = 0.0;
    double remoteSmoothing = 0.15;
    int toggleKey = 0x23;
    int cycleTrackingModeKey = 0x21;
    int reticleToggleKey = 0x22;
    int yawModeKey = 0x2E;
    uint64_t debounceMs = 200;
    InputBlockMode inputBlockMode = InputBlockMode::Never;
    bool trackInThirdPerson = true;
    bool trackInVATS = false;
    bool pauseDuringCombat = false;
    bool showMessages = true;
    bool worldSpaceYaw = true;
};

enum class ReadStatus {
    Read,
    // The build logged `error` and ran without head tracking.
    Refused,
    // No file at the path. The build wrote its first-run file there, which
    // reads back as the defaults.
    Absent,
};

struct ReadResult {
    ReadStatus status = ReadStatus::Read;
    std::string error;
};

// Reads iniPath (an ANSI path, as GetPrivateProfileStringA takes it) into
// config, field by field in the published order. Like the members it stands in
// for, config keeps what it held for every field the read does not reach
// before a refusal.
ReadResult Read(const std::string& iniPath, Config& config);

struct Key {
    std::string section;
    std::string key;
};

// Every key Read reads, in the order it reads them.
std::vector<Key> ReadKeys();

}  // namespace HeadTracking::legacy
