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

    // Network settings
    uint16_t GetUdpPort() const { return m_udpPort; }

    // Sensitivity settings (multipliers)
    SensitivitySettings GetSensitivity() const;

    // Deadzone settings (degrees)
    DeadzoneSettings GetDeadzone() const;

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

    // Sensitivity settings (multipliers)
    double m_sensitivityYaw;
    double m_sensitivityPitch;
    double m_sensitivityRoll;

    // Smoothing factor
    double m_smoothing;

    // Deadzone settings (degrees)
    double m_deadzoneYaw;
    double m_deadzonePitch;
    double m_deadzoneRoll;

    // Hotkey settings
    int m_recenterKey;
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

    // Camera mode (coupled or decoupled/free-look)
    CameraMode m_cameraMode;

    // Yaw mode: true = world-space (horizon-locked, default), false = camera-local
    bool m_worldSpaceYaw;
};

}  // namespace HeadTracking
