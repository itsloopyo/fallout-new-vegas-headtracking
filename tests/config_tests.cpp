// CameraUnlock.ini on the canonical format: the committed file is the table's
// fresh render, a first launch creates exactly those bytes, each saving control
// changes the lines of its own rows and no other byte, and a row holding
// default takes Defaults.ini's value.
//
// fnv_config_tests --render-config <path> writes the rendered file to <path>
// instead (pixi run render-config).

#include "config.h"

#include <cameraunlock/config/canonical_ini.h>

#include <Windows.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace cfg = cameraunlock::config;

using HeadTracking::Config;

namespace {

int g_failures = 0;

void Check(bool condition, const std::string& message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message.c_str());
        ++g_failures;
    }
}

std::string ReadBytes(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot read " + path.string());
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void WriteBytes(const fs::path& path, const std::string& bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!out) throw std::runtime_error("cannot write " + path.string());
}

std::string Replace(std::string text, const std::string& from, const std::string& to) {
    const std::size_t at = text.find(from);
    if (at == std::string::npos) throw std::logic_error("'" + from + "' is not in the text");
    return text.replace(at, from.size(), to);
}

std::string Rendered() {
    cfg::RenderHeader header;
    header.display_name = HeadTracking::kGameDisplayName;
    return cfg::RenderCanonicalFresh(HeadTracking::ConfigTable(), header);
}

cfg::ConfigOwnerOptions<Config> Options(const fs::path& folder, const fs::path& defaults) {
    return HeadTracking::ConfigOwnerOptions(folder, cfg::DefaultsFile::At(defaults.wstring()));
}

std::vector<std::string> Lines(const std::string& bytes) {
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start < bytes.size()) {
        const std::size_t end = bytes.find("\r\n", start);
        if (end == std::string::npos) throw std::logic_error("a rendered line does not end in CRLF");
        lines.push_back(bytes.substr(start, end - start));
        start = end + 2;
    }
    return lines;
}

// The lines that differ between two files with the same number of lines.
std::vector<std::string> ChangedLines(const std::string& before, const std::string& after) {
    const std::vector<std::string> a = Lines(before);
    const std::vector<std::string> b = Lines(after);
    if (a.size() != b.size()) throw std::logic_error("a save added or removed a line");
    std::vector<std::string> changed;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i] != b[i]) changed.push_back(a[i] + " -> " + b[i]);
    }
    return changed;
}

fs::path TempDir() {
    wchar_t temp[MAX_PATH + 1];
    if (GetTempPathW(MAX_PATH + 1, temp) == 0) throw std::runtime_error("GetTempPathW failed");
    const fs::path dir = fs::path(temp) / ("fnv-config-tests-" + std::to_string(GetCurrentProcessId()));
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

void RenderMatchesCommittedFile() {
    const std::string committed = ReadBytes(fs::path(FNV_SOURCE_DIR) / "config" / "HeadTracking.ini");
    Check(committed == Rendered(),
          "config/HeadTracking.ini is not what the table renders; run pixi run render-config");
    const cfg::CanonicalIni doc = cfg::ParseCanonicalIni(committed);
    Check(doc.IsReadable() && doc.diagnostics.empty(), "the committed file reads without a diagnostic");
    Config read = HeadTracking::ConfigTable().defaults();
    Check(cfg::ApplyCanonical(doc, HeadTracking::ConfigTable(), read).diagnostics.empty(),
          "the committed file applies without a diagnostic");
}

void DefaultsAreTheFleetDefaults() {
    const Config defaults = HeadTracking::ConfigTable().defaults();
    Check(defaults.toggle_key == "End, Ctrl+Shift+Y", "ToggleKey defaults to End, Ctrl+Shift+Y");
    Check(defaults.cycle_tracking_mode_key == "PageUp, Ctrl+Shift+G", "CycleTrackingModeKey defaults to PageUp, Ctrl+Shift+G");
    Check(defaults.yaw_mode_key == "PageDown, Ctrl+Shift+H", "YawModeKey defaults to PageDown, Ctrl+Shift+H");
    Check(defaults.world_space_yaw, "WorldSpaceYaw defaults to true");
    Check(HeadTracking::StartupTrackingMode(defaults) == cameraunlock::TrackingMode::RotationAndPosition,
          "the default tracking mode is rotation and position");
    const auto toggle = HeadTracking::KeyBindings(defaults.toggle_key);
    Check(toggle.size() == 2 && toggle[0].vk == VK_END && toggle[0].modifiers == cameraunlock::input::KeyModifiers::kNone &&
              toggle[1].vk == 'Y' &&
              toggle[1].modifiers == (cameraunlock::input::KeyModifiers::kCtrl | cameraunlock::input::KeyModifiers::kShift),
          "ToggleKey registers End and Ctrl+Shift+Y");
}

void SavesChangeOnlyTheirRows(const fs::path& dir) {
    const fs::path folder = dir / "saves";
    fs::create_directories(folder);
    const fs::path defaults = dir / "saves-global" / "Defaults.ini";
    const fs::path path = folder / "CameraUnlock.ini";
    cfg::ConfigOwner<Config> owner(Options(folder, defaults));
    const auto created = owner.Load();
    Check(created.status == cfg::ConfigLoadStatus::Created,
          std::string("a first launch creates the file, not ") + cfg::ConfigLoadStatusName(created.status) + ": " +
              created.reason);
    const std::string fresh = ReadBytes(path);
    Check(fresh == Rendered(), "a first launch writes the committed file's bytes");
    Check(!fs::exists(folder / "HeadTracking.ini"), "a first launch writes no legacy file");
    const std::string defaultsBytes = ReadBytes(defaults);

    const cfg::ConfigSaveResult yawSaved = owner.Save([](Config& c) { c.world_space_yaw = false; });
    Check(yawSaved.status == cfg::ConfigSaveStatus::Saved, "the yaw mode saves");
    Check(yawSaved.log.size() == 1 && yawSaved.log[0].find("WorldSpaceYaw=false") != std::string::npos,
          "the yaw save logs that WorldSpaceYaw no longer follows Defaults.ini");
    const std::string yaw = ReadBytes(path);
    const auto yawChanged = ChangedLines(fresh, yaw);
    Check(yawChanged.size() == 1 && yawChanged[0] == "WorldSpaceYaw=default -> WorldSpaceYaw=false",
          "saving the yaw mode writes its value over default and changes no other byte");

    const auto channels = cameraunlock::EncodeTrackingMode(cameraunlock::TrackingMode::PositionOnly);
    Check(owner.Save([channels](Config& c) {
                   c.rotation_enabled = channels.rotation_enabled;
                   c.position_enabled = channels.position_enabled;
               }).status == cfg::ConfigSaveStatus::Saved,
          "the tracking mode saves");
    const auto modeChanged = ChangedLines(yaw, ReadBytes(path));
    Check(modeChanged.size() == 2 && modeChanged[0] == "RotationEnabled=default -> RotationEnabled=false" &&
              modeChanged[1] == "PositionEnabled=default -> PositionEnabled=true",
          "saving position only writes both tracking mode rows over default and changes no other byte");
    Check(ReadBytes(defaults) == defaultsBytes, "no save changes Defaults.ini");

    bool refused = false;
    try {
        owner.Save([](Config& c) { c.enable_on_startup = false; });
    } catch (const std::logic_error&) {
        refused = true;
    }
    Check(refused, "EnableOnStartup is not Writable, so no control saves it");

    Check(owner.Reload().status == cfg::ConfigReloadStatus::Unchanged, "a reload after a save finds nothing new");

    cfg::ConfigOwner<Config> relaunch(Options(folder, defaults));
    const auto again = relaunch.Load();
    Check(again.status == cfg::ConfigLoadStatus::Canonical, "the next launch reads the saved file as canonical");
    Check(!again.config.world_space_yaw, "the saved yaw mode comes back");
    Check(HeadTracking::StartupTrackingMode(again.config) == cameraunlock::TrackingMode::PositionOnly,
          "the saved tracking mode comes back");
    Check(again.config.enable_on_startup, "EnableOnStartup stays as the file had it");
}

// A fresh file holds default on every global row, so a Defaults.ini the player
// edited reaches the game, and a value the game's own file holds wins over it.
void DefaultRowsFollowDefaultsIni(const fs::path& dir) {
    const fs::path folder = dir / "follows";
    const fs::path defaults = dir / "follows-global" / "Defaults.ini";
    WriteBytes(folder / "CameraUnlock.ini", Rendered());
    WriteBytes(defaults,
               "[CameraUnlock]\r\nConfigFormat=1\r\n\r\n[Network]\r\nUdpPort=5252\r\n\r\n[General]\r\n"
               "WorldSpaceYaw=false\r\n\r\n[Hotkeys]\r\nToggleKey=F8\r\n");
    const auto loaded = cfg::ConfigOwner<Config>(Options(folder, defaults)).Load();
    Check(loaded.status == cfg::ConfigLoadStatus::Canonical, "the committed file loads as canonical");
    Check(loaded.config.udp_port == 5252 && !loaded.config.world_space_yaw && loaded.config.toggle_key == "F8",
          "rows holding default take Defaults.ini's values");
    Check(loaded.config.cycle_tracking_mode_key == "PageUp, Ctrl+Shift+G",
          "a row Defaults.ini leaves out takes the built-in value");

    WriteBytes(folder / "CameraUnlock.ini", Replace(Rendered(), "WorldSpaceYaw=default\r\n", "WorldSpaceYaw=true\r\n"));
    Check(cfg::ConfigOwner<Config>(Options(folder, defaults)).Load().config.world_space_yaw,
          "a value written in CameraUnlock.ini wins over Defaults.ini");
}

}  // namespace

int main(int argc, char** argv) {
    if (argc == 3 && std::string(argv[1]) == "--render-config") {
        WriteBytes(argv[2], Rendered());
        return 0;
    }
    if (argc != 1) {
        std::fprintf(stderr, "usage: fnv_config_tests [--render-config <path>]\n");
        return 2;
    }

    const fs::path dir = TempDir();
    RenderMatchesCommittedFile();
    DefaultsAreTheFleetDefaults();
    SavesChangeOnlyTheirRows(dir);
    DefaultRowsFollowDefaultsIni(dir);
    fs::remove_all(dir);

    if (g_failures != 0) {
        std::fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    std::puts("Config tests passed");
    return 0;
}
