#pragma once

#include <Windows.h>

#include "camera_controller.h"
#include "game_state.h"

#include <cameraunlock/config/ini_reader.h>

#include <cstdint>
#include <string>

namespace HeadTracking {

// Forward declarations (only for types not included above)
class HotkeyHandler;
class UdpReceiver;

class Config {
public:
    Config();
    ~Config();

    // Disable copying
    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;
    Config(Config&&) = delete;
    Config& operator=(Config&&) = delete;

    // Load configuration from INI file
    // Returns true if file was found and parsed with all valid values
    // Returns false if file is missing or contains invalid values (FAIL FAST)
    bool Load(const std::string& iniPath);

    // Reload configuration from disk
    // Call this when user modifies the INI file
    // Returns true if reload succeeded
    bool Reload();

    // Check if the INI file has been modified since last load
    // Uses file modification timestamp
    bool HasFileChanged() const;

    // Apply loaded settings to plugin components
    // Should be called after Load() or Reload()
    // Returns false if config not loaded or invalid values (FAIL FAST)
    bool ApplyToComponents(CameraController* camera, HotkeyHandler* hotkey,
                           GameState* gameState, UdpReceiver* udpReceiver);

    void SetAdsMode(cameraunlock::ads::AdsMode mode);

    // Network settings
    uint16_t GetUdpPort() const { return m_udpPort; }

    // Smoothing, chosen per connection: local for a tracker on this machine,
    // remote for a device sending over the network. Both cover rotation and
    // position.
    double GetLocalSmoothing() const { return m_localSmoothing; }
    double GetRemoteSmoothing() const { return m_remoteSmoothing; }

    // Check if configuration has been loaded
    bool IsLoaded() const { return m_loaded; }

private:
    // Create default config file when missing
    bool CreateDefaultConfig();

    // Path to INI file
    std::string m_iniPath;

    // Load state
    bool m_loaded;

    // INI reader (handles parsing and change detection)
    cameraunlock::IniReader m_ini;

    // Network settings
    uint16_t m_udpPort;

    // Smoothing factors (local / remote connection)
    double m_localSmoothing;
    double m_remoteSmoothing;

    // Hotkey settings
    int m_toggleKey;
    int m_cycleTrackingModeKey;
    int m_reticleToggleKey;
    int m_yawModeKey;
    uint64_t m_debounceMs;

    // Game state settings
    InputBlockMode m_inputBlockMode;
    bool m_trackInThirdPerson;
    bool m_trackInVATS;
    bool m_pauseDuringCombat;

    // Feedback settings
    bool m_showMessages;

    // Yaw mode: true = world-space (horizon-locked, default), false = camera-local
    bool m_worldSpaceYaw;
    std::string m_adsModeKey = "ads_mode";
    cameraunlock::ads::AdsMode m_adsMode = cameraunlock::ads::kDefaultAdsMode;
};

}  // namespace HeadTracking
