// HeadTracking.ini on the canonical format: the committed file is what the
// table renders, a first launch creates exactly those bytes, and each saving
// control changes the lines of its own rows and no other byte.
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
#include <sstream>
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
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!out) throw std::runtime_error("cannot write " + path.string());
}

std::string Rendered() {
    const cfg::ConfigTable<Config> table = HeadTracking::ConfigTable();
    cfg::RenderHeader header;
    header.display_name = HeadTracking::kGameDisplayName;
    return cfg::RenderCanonical(table, table.defaults(), header);
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
    const fs::path path = dir / "HeadTracking.ini";
    cfg::ConfigOwner<Config> owner(HeadTracking::ConfigOwnerOptions(path.wstring()));
    const auto created = owner.Load();
    Check(created.status == cfg::ConfigLoadStatus::Created, "a first launch creates the file");
    const std::string fresh = ReadBytes(path);
    Check(fresh == Rendered(), "a first launch writes the committed file's bytes");

    Check(owner.Save([](Config& c) { c.world_space_yaw = false; }).status == cfg::ConfigSaveStatus::Saved,
          "the yaw mode saves");
    const std::string yaw = ReadBytes(path);
    const auto yawChanged = ChangedLines(fresh, yaw);
    Check(yawChanged.size() == 1 && yawChanged[0] == "WorldSpaceYaw=true -> WorldSpaceYaw=false",
          "saving the yaw mode changes the WorldSpaceYaw line and no other byte");

    const auto channels = cameraunlock::EncodeTrackingMode(cameraunlock::TrackingMode::PositionOnly);
    Check(owner.Save([channels](Config& c) {
                   c.rotation_enabled = channels.rotation_enabled;
                   c.position_enabled = channels.position_enabled;
               }).status == cfg::ConfigSaveStatus::Saved,
          "the tracking mode saves");
    const auto modeChanged = ChangedLines(yaw, ReadBytes(path));
    Check(modeChanged.size() == 1 && modeChanged[0] == "RotationEnabled=true -> RotationEnabled=false",
          "saving position only changes the RotationEnabled line and no other byte");

    bool refused = false;
    try {
        owner.Save([](Config& c) { c.enable_on_startup = false; });
    } catch (const std::logic_error&) {
        refused = true;
    }
    Check(refused, "EnableOnStartup is not Writable, so no control saves it");

    Check(owner.Reload().status == cfg::ConfigReloadStatus::Unchanged, "a reload after a save finds nothing new");

    cfg::ConfigOwner<Config> relaunch(HeadTracking::ConfigOwnerOptions(path.wstring()));
    const auto again = relaunch.Load();
    Check(again.status == cfg::ConfigLoadStatus::Canonical, "the next launch reads the saved file as canonical");
    Check(!again.config.world_space_yaw, "the saved yaw mode comes back");
    Check(HeadTracking::StartupTrackingMode(again.config) == cameraunlock::TrackingMode::PositionOnly,
          "the saved tracking mode comes back");
    Check(again.config.enable_on_startup, "EnableOnStartup stays as the file had it");
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
    fs::remove_all(dir);

    if (g_failures != 0) {
        std::fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    std::puts("Config tests passed");
    return 0;
}
