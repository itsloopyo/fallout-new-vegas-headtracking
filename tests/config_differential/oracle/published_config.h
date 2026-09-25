#pragma once

// v0.3.1's Config, with the members it declared (src/config.h) made public so
// the oracle can report them, and the InputBlockMode it read into
// (src/game_state.h).

#include <cameraunlock/ads/ads_mode.h>
#include <cameraunlock/config/ini_reader.h>

#include <cstdint>
#include <string>
#include <vector>

namespace published {

enum class InputBlockMode {
    Never,
    MenusOnly,
    AllDialogue,
    AllOverlays
};

struct Diagnostic {
    std::string level;
    std::string message;
};

// Every ConfigDiag call, in order.
extern std::vector<Diagnostic> g_diagnostics;

class Config {
public:
    Config();
    ~Config();

    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;

    bool Load(const std::string& iniPath);
    bool CreateDefaultConfig();

    std::string m_iniPath;
    bool m_loaded;
    cameraunlock::IniReader m_ini;
    uint16_t m_udpPort;
    double m_localSmoothing;
    double m_remoteSmoothing;
    int m_toggleKey;
    int m_cycleTrackingModeKey;
    int m_reticleToggleKey;
    int m_yawModeKey;
    uint64_t m_debounceMs;
    InputBlockMode m_inputBlockMode;
    bool m_trackInThirdPerson;
    bool m_trackInVATS;
    bool m_pauseDuringCombat;
    bool m_showMessages;
    bool m_worldSpaceYaw;
    std::string m_adsModeKey = "ads_mode";
    cameraunlock::ads::AdsMode m_adsMode = cameraunlock::ads::kDefaultAdsMode;
};

}  // namespace published
