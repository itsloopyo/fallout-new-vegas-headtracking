#pragma once

#include "game_state.h"

#include <cameraunlock/config/config_concepts.g.h>
#include <cameraunlock/config/config_owner.h>
#include <cameraunlock/config/config_table.h>
#include <cameraunlock/config/defaults_file.h>
#include <cameraunlock/config/legacy_import.h>
#include <cameraunlock/input/key_bindings.h>
#include <cameraunlock/math/smoothing_utils.h>
#include <cameraunlock/tracking/tracking_mode.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace HeadTracking {

// Every setting CameraUnlock.ini holds. Its canonical format, the file's rows
// and their comments are ConfigTable(); ConfigOwner is the only reader and
// writer of the file.
struct Config {
    uint16_t udp_port = 4242;
    bool enable_on_startup = true;
    bool world_space_yaw = true;
    // The tracking mode at startup, with position_enabled.
    bool rotation_enabled = true;
    double local_smoothing = cameraunlock::math::kDefaultLocalSmoothing;
    double remote_smoothing = cameraunlock::math::kDefaultRemoteSmoothing;
    bool position_enabled = true;
    std::string toggle_key =
        cameraunlock::config::schema::ConceptTraits<cameraunlock::config::schema::Concept::ToggleKey>::kCanonicalDefault;
    std::string cycle_tracking_mode_key = cameraunlock::config::schema::ConceptTraits<
        cameraunlock::config::schema::Concept::CycleTrackingModeKey>::kCanonicalDefault;
    std::string yaw_mode_key =
        cameraunlock::config::schema::ConceptTraits<cameraunlock::config::schema::Concept::YawModeKey>::kCanonicalDefault;
    bool track_in_third_person = true;
    bool track_in_vats = false;
    bool pause_during_combat = false;
    InputBlockMode input_block_mode = InputBlockMode::Never;
    uint64_t hotkey_debounce_ms = 200;
};

// The game's name as cameraunlock-core's data/games.json spells it.
inline constexpr const char* kGameDisplayName = "Fallout: New Vegas";

cameraunlock::config::ConfigTable<Config> ConfigTable();

// The settings file, and the file every build before it read, which the owner
// imports while the settings file is absent and never writes.
inline constexpr const wchar_t* kConfigFileName = L"CameraUnlock.ini";
inline constexpr const wchar_t* kLegacyFileName = L"HeadTracking.ini";

// The import of a HeadTracking.ini an older build wrote: the frozen reader in
// src/legacy_config/, and the map from what it read into Config.
cameraunlock::config::LegacyImport<Config> ConfigLegacyImport();

// The owner's options for CameraUnlock.ini in `folder`, a full path, with
// HeadTracking.ini beside it as the legacy file.
cameraunlock::config::ConfigOwnerOptions<Config> ConfigOwnerOptions(const std::filesystem::path& folder,
                                                                    cameraunlock::config::DefaultsFile defaults);

// The tracking mode the RotationEnabled / PositionEnabled pair names. The
// table never gives a config both false.
cameraunlock::TrackingMode StartupTrackingMode(const Config& config);

// A hotkey list from Config as bindings. Throws std::invalid_argument for a list
// ParseKeyBindings refuses, which the table's hotkey codec never lets through.
std::vector<cameraunlock::input::KeyBinding> KeyBindings(const std::string& list);

}  // namespace HeadTracking
