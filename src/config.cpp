#include "config.h"

#include "legacy_config/legacy_config.h"

#include <cameraunlock/config/canonical_ini.h>

#include <cstdio>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <utility>

namespace HeadTracking {

namespace cfg = cameraunlock::config;
using cfg::schema::Concept;
using cameraunlock::input::KeyBinding;
using cameraunlock::input::KeyModifiers;

cfg::ConfigTable<Config> ConfigTable() {
    cfg::ConfigTable<Config> table{Config{}};
    table.Concept<Concept::UdpPort>(&Config::udp_port)
        .Comment("UDP port the mod receives tracker data on (OpenTrack protocol).\n"
                 "Restart the game after changing it.")
        .Concept<Concept::EnableOnStartup>(&Config::enable_on_startup)
        .Concept<Concept::WorldSpaceYaw>(&Config::world_space_yaw)
        .Writable()
        .Concept<Concept::RotationEnabled>(&Config::rotation_enabled)
        .Writable()
        .Concept<Concept::LocalSmoothing>(&Config::local_smoothing)
        .Concept<Concept::RemoteSmoothing>(&Config::remote_smoothing)
        .Concept<Concept::PositionEnabled>(&Config::position_enabled)
        .Writable()
        .Concept<Concept::TrueFreeLook>(&Config::true_free_look)
        .Writable()
        .Concept<Concept::CollisionEnabled>(&Config::collision_enabled)
        .Concept<Concept::CollisionReleaseSmoothing>(&Config::collision_release_smoothing)
        .Concept<Concept::ToggleKey>(&Config::toggle_key)
        .Concept<Concept::CycleTrackingModeKey>(&Config::cycle_tracking_mode_key)
        .Concept<Concept::YawModeKey>(&Config::yaw_mode_key)
        .Concept<Concept::TrueFreeLookKey>(&Config::true_free_look_key)
        .Local("GameState", "TrackInThirdPerson", &Config::track_in_third_person, cfg::BoolCodec(),
               "true: head tracking also works in the third-person camera.")
        .Local("GameState", "TrackInVATS", &Config::track_in_vats, cfg::BoolCodec(),
               "true: head tracking stays on in VATS. false: it pauses there.")
        .Local("GameState", "PauseDuringCombat", &Config::pause_during_combat, cfg::BoolCodec(),
               "true: head tracking pauses while you are in combat.")
        .Local("Input", "InputBlockMode", &Config::input_block_mode,
               cfg::EnumCodec<InputBlockMode>{{"Never", InputBlockMode::Never},
                                              {"MenusOnly", InputBlockMode::MenusOnly},
                                              {"AllDialogue", InputBlockMode::AllDialogue},
                                              {"AllOverlays", InputBlockMode::AllOverlays}},
               "When the hotkeys are ignored. They never work on a loading screen or in character creation.\n"
               "Never: they work everywhere else.\n"
               "MenusOnly: not while a menu is open or the game is paused.\n"
               "AllDialogue: not in menus or conversations either.\n"
               "AllOverlays: not in menus, conversations, the console, VATS or the Pip-Boy either.")
        .Local("Input", "HotkeyDebounceMs", &Config::hotkey_debounce_ms, cfg::IntCodec<uint64_t>(),
               "Milliseconds after a hotkey fires before it can fire again, 50 to 2000.")
        .Range(50, 2000);
    return table;
}

namespace {

// Insert, the yaw mode key v0.1.0 and v0.2.0 wrote on their first start.
constexpr int kV020YawModeKey = 0x2D;

InputBlockMode ToInputBlockMode(legacy::InputBlockMode mode) {
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

// A legacy action: its code in the file, which fired while Ctrl and Shift were
// not both held, and its fleet Ctrl+Shift letter. A code no hotkey can hold, a
// Ctrl, Shift or Alt key alone, imports as unbound (N3) and the action keeps
// its chord.
std::string WithChord(int code, const char* key, char letter, std::vector<cfg::DroppedValue>& dropped) {
    std::string list = cfg::LegacyVirtualKeyToBindings(code, "Hotkeys", key, dropped);
    const std::string chord = cameraunlock::input::FormatKeyBindings(
        {KeyBinding{KeyModifiers::kCtrl | KeyModifiers::kShift, letter}});
    return list.empty() ? chord : list + ", " + chord;
}

std::string CodeText(int code) {
    char text[16];
    std::snprintf(text, sizeof(text), "0x%02X", code);
    return text;
}

// Only a file that gives the key a value had a reticle toggle to lose: without one
// the frozen struct holds the build's default, which no file named. Opened by its
// ANSI path, the file the frozen reader read.
bool GivesReticleToggle(const std::string& ansiPath) {
    std::ifstream in(ansiPath, std::ios::binary);
    if (!in) throw std::runtime_error("cannot reopen the legacy file the frozen reader just read");
    const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const cfg::CanonicalIni doc = cfg::ParseCanonicalIni(bytes);
    // The Windows profile API the frozen reader uses reads a UTF-16 file and one
    // holding a NUL, which the canonical parser does not, so those count as giving it.
    if (!doc.IsReadable()) return true;
    const cfg::CanonicalValue* value = doc.Find("Hotkeys", "ReticleToggle");
    return value && !value->value.empty();
}

cfg::ImportResult RunImport(const cfg::LegacyInput& input, Config& out) {
    legacy::Config read;
    const legacy::ReadResult result = legacy::Read(input.ansi_path, read);
    if (result.status == legacy::ReadStatus::Refused) return cfg::ImportResult::Refused(result.error);

    const Config defaults;
    std::vector<cfg::DroppedValue> dropped;
    out.udp_port = read.udpPort;
    // The published build started with head tracking on, in the rotation and
    // position mode, whatever the file said.
    out.enable_on_startup = true;
    out.rotation_enabled = true;
    out.position_enabled = true;
    out.world_space_yaw = read.worldSpaceYaw;
    // The published build always held a lean off the walls, easing back out at
    // core's default release.
    out.collision_enabled = true;
    out.collision_release_smoothing = defaults.collision_release_smoothing;
    // The range checks let a NaN through, which N2 takes to the default.
    out.local_smoothing =
        cfg::LegacyFiniteOrDefault(read.localSmoothing, defaults.local_smoothing, "Smoothing", "LocalSmoothing", dropped);
    out.remote_smoothing = cfg::LegacyFiniteOrDefault(read.remoteSmoothing, defaults.remote_smoothing, "Smoothing",
                                                      "RemoteSmoothing", dropped);
    out.toggle_key = WithChord(read.toggleKey, "Toggle", 'Y', dropped);
    out.cycle_tracking_mode_key = WithChord(read.cycleTrackingModeKey, "CycleTrackingMode", 'G', dropped);
    if (result.status != legacy::ReadStatus::Absent && GivesReticleToggle(input.ansi_path)) {
        dropped.push_back({cfg::DropRule::Reticle, "Hotkeys", "ReticleToggle", CodeText(read.reticleToggleKey)});
    }
    // The published build fixed yaw's chord at Ctrl+Shift+J in code, which no file
    // could set; it takes the fleet's Ctrl+Shift+H, freed with the reticle toggle.
    out.yaw_mode_key = WithChord(read.yawModeKey, "YawModeKey", 'H', dropped);
    // The build had no true free look; its [Camera] ads_mode is not carried.
    out.true_free_look = defaults.true_free_look;
    out.true_free_look_key = defaults.true_free_look_key;
    out.hotkey_debounce_ms = read.debounceMs;
    out.input_block_mode = ToInputBlockMode(read.inputBlockMode);
    out.track_in_third_person = read.trackInThirdPerson;
    out.track_in_vats = read.trackInVATS;
    out.pause_during_combat = read.pauseDuringCombat;
    // [Feedback] ShowMessages is not carried: it only gated messages to a game
    // console the build never had (g_ConsolePrint was always null).

    // A setting the player never changed from what the build wrote follows
    // Defaults.ini. The build had no setting for the start state, the tracking
    // mode, true free look or the lean collision. v0.3.1 ran YawModeKey=0x2D
    // (what v0.1.0 and v0.2.0 wrote on first start) as 0x2E, so both are its
    // shipped Delete.
    const legacy::Config shipped;
    cfg::LegacyFollowsDefaultsIni follows;
    follows.Setting(Concept::UdpPort, read.udpPort, shipped.udpPort);
    follows.NotInLegacy(Concept::EnableOnStartup);
    follows.Setting(Concept::WorldSpaceYaw, read.worldSpaceYaw, shipped.worldSpaceYaw);
    follows.TrackingMode(true);
    follows.Setting(Concept::LocalSmoothing, read.localSmoothing, shipped.localSmoothing);
    follows.Setting(Concept::RemoteSmoothing, read.remoteSmoothing, shipped.remoteSmoothing);
    follows.NotInLegacy(Concept::CollisionEnabled);
    follows.NotInLegacy(Concept::CollisionReleaseSmoothing);
    follows.Setting(Concept::ToggleKey, read.toggleKey, shipped.toggleKey);
    follows.Setting(Concept::CycleTrackingModeKey, read.cycleTrackingModeKey, shipped.cycleTrackingModeKey);
    follows.Setting(Concept::YawModeKey, read.yawModeKey == shipped.yawModeKey || read.yawModeKey == kV020YawModeKey);
    follows.NotInLegacy(Concept::TrueFreeLook);
    follows.NotInLegacy(Concept::TrueFreeLookKey);

    if (result.status == legacy::ReadStatus::Absent) {
        return cfg::ImportResult::Absent(std::move(dropped), {}, follows.Concepts());
    }
    return cfg::ImportResult::Imported(std::move(dropped), {}, follows.Concepts());
}

}  // namespace

cfg::LegacyImport<Config> ConfigLegacyImport() {
    cfg::LegacyImport<Config> import;
    import.run = &RunImport;
    for (const legacy::Key& key : legacy::ReadKeys()) import.keys.push_back({key.section, key.key});
    return import;
}

cfg::ConfigOwnerOptions<Config> ConfigOwnerOptions(const std::filesystem::path& folder, cfg::DefaultsFile defaults) {
    cfg::ConfigOwnerOptions<Config> options;
    options.path = (folder / kConfigFileName).wstring();
    options.table = ConfigTable();
    options.import = ConfigLegacyImport();
    options.legacy_path = (folder / kLegacyFileName).wstring();
    options.header.display_name = kGameDisplayName;
    options.defaults = std::move(defaults);
    return options;
}

cameraunlock::TrackingMode StartupTrackingMode(const Config& config) {
    const auto mode = cameraunlock::DecodeTrackingMode(config.rotation_enabled, config.position_enabled);
    if (!mode) throw std::logic_error("RotationEnabled and PositionEnabled are both false, which the table never gives");
    return *mode;
}

std::vector<KeyBinding> KeyBindings(const std::string& list) {
    cameraunlock::input::KeyBindingsParseResult parsed = cameraunlock::input::ParseKeyBindings(list);
    if (!parsed.ok()) throw std::invalid_argument("hotkey list '" + list + "': " + parsed.error);
    return std::move(parsed.bindings);
}

}  // namespace HeadTracking
