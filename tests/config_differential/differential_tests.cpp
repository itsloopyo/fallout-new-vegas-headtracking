// The differential test for the canonical config conversion.
//
// fnv_config_differential <path to fnv_config_oracle.exe>
//
// Every input is read three ways:
//   oracle    - v0.3.1's reader and startup code, the newest published build
//               (fnv_config_oracle, built from oracle/);
//   import    - the frozen reader in src/legacy_config/ and the startup code
//               that ran on it;
//   migration - ConfigOwner on a copy: the import, the map into Config, the
//               render, the commit, then the canonical reader and table, and
//               the game's startup code on the result.
//
// Comparison 1, oracle against import, is what a player sees change that the
// conversion did not cause: commits since v0.3.1 that changed how the file is
// read. Every difference it finds must be one of kReaderChanges below.
//
// Comparison 2, import against migration, is the proof for the migration: no
// difference apart from the approved drops the import records (core's
// data/config-format.json) and the one default the conversion moves, which
// only the no-file input shows.

#include "record.h"

#include "config.h"
#include "legacy_config/legacy_config.h"

#include <cameraunlock/config/canonical_ini.h>
#include <cameraunlock/config/config_owner.h>
#include <cameraunlock/config/legacy_import.h>
#include <cameraunlock/config/testing/ini_mutations.h>

#include <Windows.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

using fnv_differential::Bindings;
using fnv_differential::Bits;
using fnv_differential::Flag;
using fnv_differential::Hex;
using fnv_differential::kCtrlShift;
using fnv_differential::Record;
using HeadTracking::Config;
using HeadTracking::legacy::ReadStatus;
namespace cfg = cameraunlock::config;

namespace {

int g_failures = 0;

void Fail(const std::string& message) {
    std::fprintf(stderr, "FAIL: %s\n", message.c_str());
    ++g_failures;
}

void Check(bool condition, const std::string& message) {
    if (!condition) Fail(message);
}

std::string ReadBytes(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot read " + path.string());
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void WriteBytes(const fs::path& path, const std::string& bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("cannot write " + path.string());
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!out) throw std::runtime_error("cannot write " + path.string());
}

struct Input {
    std::string name;
    // No bytes: there is no file.
    std::optional<std::string> bytes;
};

const fs::path kData = fs::path(FNV_SOURCE_DIR) / "tests" / "config_differential" / "data";

// The published inputs: the shipped config and the launcher seed at every
// published build (v0.1.0 and v0.2.0 seeded Data/NVSE/Plugins/HeadTracking.ini,
// v0.3.1 and dev the root file), each build's first-run output, the two other
// committed versions of config/HeadTracking.ini before v0.3.1, and the file the
// predecessor repo fallout-new-vegas-headtracking-delete shipped at v1.0.1 and
// v1.0.3.
const char* const kDataFiles[] = {
    "v0.1.0/shipped.ini",   "v0.1.0/seed.ini",   "v0.1.0/first-run.ini",
    "v0.2.0/shipped.ini",   "v0.2.0/seed.ini",   "v0.2.0/first-run.ini",
    "v0.3.1/shipped.ini",   "v0.3.1/seed.ini",   "v0.3.1/first-run.ini",
    "dev/shipped.ini",      "dev/seed.ini",      "dev/first-run.ini",
    "committed/4df0584.ini", "committed/74431db.ini",
    "predecessor/v1.0.1.ini", "predecessor/v1.0.3.ini",
};

std::string Replace(std::string text, const std::string& from, const std::string& to) {
    const std::size_t at = text.find(from);
    if (at == std::string::npos) throw std::logic_error("'" + from + "' is not in the base file");
    return text.replace(at, from.size(), to);
}

// Each key the frozen reader reads, for the corpus: another valid value, and one
// value outside each range the reader refuses. The hotkey codes are read with
// ReadHex, and their Ctrl+Shift letters were fixed in code, so no key of the
// file folds a chord into them.
std::vector<cameraunlock::config::testing::MutationKey> MutationKeys() {
    return {
        {"Network", "Port", "5252", {"0", "65536"}, false, {}},
        {"Smoothing", "LocalSmoothing", "0.3", {"-0.5", "1.5"}, false, {}},
        {"Smoothing", "RemoteSmoothing", "0.5", {"-0.5", "1.5"}, false, {}},
        {"Hotkeys", "Toggle", "0x78", {"0xFF"}, true, {}},
        {"Hotkeys", "CycleTrackingMode", "0x70", {"0xFF"}, true, {}},
        {"Hotkeys", "ReticleToggle", "0x71", {"0xFF"}, true, {}},
        // Insert, which v0.3.1 moved to Delete (see kReaderChanges).
        {"Hotkeys", "YawModeKey", "0x2D", {"0xFF"}, true, {}},
        {"Hotkeys", "DebounceMs", "300", {"49", "2001"}, false, {}},
        {"GameState", "InputBlockMode", "2", {"-1", "4"}, false, {}},
        {"GameState", "TrackInThirdPerson", "0", {}, false, {}},
        {"GameState", "TrackInVATS", "1", {}, false, {}},
        {"GameState", "PauseDuringCombat", "1", {}, false, {}},
        {"Feedback", "ShowMessages", "0", {}, false, {}},
        {"Camera", "WorldSpaceYaw", "0", {}, false, {}},
    };
}

std::vector<cameraunlock::config::LegacyKey> ImportKeys() {
    std::vector<cameraunlock::config::LegacyKey> keys;
    for (const auto& key : HeadTracking::legacy::ReadKeys()) keys.push_back({key.section, key.key});
    return keys;
}

std::vector<Input> Inputs() {
    std::vector<Input> inputs;
    for (const char* file : kDataFiles) inputs.push_back({file, ReadBytes(kData / file)});
    inputs.push_back({"no file", std::nullopt});
    inputs.push_back({"empty file", std::string()});

    const std::string base = ReadBytes(kData / "v0.3.1" / "shipped.ini");
    inputs.push_back({"v0.3.1 shipped, ads_mode=marker", Replace(base, "ads_mode=paused", "ads_mode=marker")});
    std::string padded = base;
    for (int i = 0; i < 64; ++i) padded += "Padding" + std::to_string(i) + "=" + std::string(64, 'x') + "\n";
    inputs.push_back({"v0.3.1 shipped, [Camera] past 4094 bytes", padded});

    for (auto& mutation : cameraunlock::config::testing::GenerateIniMutations(base, ImportKeys(), MutationKeys())) {
        inputs.push_back({"corpus: " + mutation.name, std::move(mutation.bytes)});
    }
    return inputs;
}

// The import as the game ran it: the frozen reader, then the startup code at
// ecb14e4 (HeadTrackingPlugin::Initialize, Config::ApplyToComponents and the
// HotkeyHandler), which started enabled, in the rotation and position mode,
// with each action's code and its fixed Ctrl+Shift letter.
Record ImportRecord(const fs::path& path) {
    HeadTracking::legacy::Config config;
    const auto read = HeadTracking::legacy::Read(path.string(), config);
    Record record;
    if (read.status == ReadStatus::Refused) {
        record["status"] = "refused";
        record["reason"] = read.error;
        return record;
    }
    record["status"] = read.status == ReadStatus::Absent ? "absent" : "usable";
    record["field.udpPort"] = std::to_string(config.udpPort);
    record["field.localSmoothing"] = Bits(config.localSmoothing);
    record["field.remoteSmoothing"] = Bits(config.remoteSmoothing);
    record["field.toggleKey"] = Hex(config.toggleKey);
    record["field.cycleTrackingModeKey"] = Hex(config.cycleTrackingModeKey);
    record["field.reticleToggleKey"] = Hex(config.reticleToggleKey);
    record["field.yawModeKey"] = Hex(config.yawModeKey);
    record["field.debounceMs"] = std::to_string(config.debounceMs);
    record["field.inputBlockMode"] = std::to_string(static_cast<int>(config.inputBlockMode));
    record["field.trackInThirdPerson"] = Flag(config.trackInThirdPerson);
    record["field.trackInVATS"] = Flag(config.trackInVATS);
    record["field.pauseDuringCombat"] = Flag(config.pauseDuringCombat);
    record["field.showMessages"] = Flag(config.showMessages);
    record["field.worldSpaceYaw"] = Flag(config.worldSpaceYaw);
    record["startup.enabled"] = "1";
    record["startup.mode"] = "RotationAndPosition";
    record["startup.worldSpaceYaw"] = Flag(config.worldSpaceYaw);
    record["hotkey.Toggle"] = Bindings({{0, config.toggleKey}, {kCtrlShift, 'Y'}});
    record["hotkey.CycleTrackingMode"] = Bindings({{0, config.cycleTrackingModeKey}, {kCtrlShift, 'G'}});
    record["hotkey.ReticleToggle"] = Bindings({{0, config.reticleToggleKey}, {kCtrlShift, 'H'}});
    record["hotkey.YawMode"] = Bindings({{0, config.yawModeKey}, {kCtrlShift, 'J'}});
    return record;
}

std::vector<Record> RunOracle(const fs::path& oracle, const fs::path& list, const fs::path& output) {
    SECURITY_ATTRIBUTES inherit{sizeof(inherit), nullptr, TRUE};
    HANDLE out = CreateFileW(output.c_str(), GENERIC_WRITE, 0, &inherit, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (out == INVALID_HANDLE_VALUE) throw std::runtime_error("cannot create " + output.string());
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = out;
    startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    std::wstring command = L"\"" + oracle.wstring() + L"\" \"" + list.wstring() + L"\"";
    PROCESS_INFORMATION process{};
    const BOOL started = CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr,
                                        &startup, &process);
    CloseHandle(out);
    if (!started) throw std::runtime_error("cannot start " + oracle.string());
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    if (code != 0) throw std::runtime_error("the oracle exited with " + std::to_string(code));

    std::vector<Record> records;
    std::istringstream lines(ReadBytes(output));
    std::string line;
    Record record;
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line == "end") {
            records.push_back(std::move(record));
            record.clear();
            continue;
        }
        const std::size_t tab = line.find('\t');
        if (tab == std::string::npos) throw std::runtime_error("the oracle wrote '" + line + "'");
        record[line.substr(0, tab)] = line.substr(tab + 1);
    }
    return records;
}

// What changed in how the file is read between v0.3.1 and the frozen reader,
// each with the commit that changed it.
struct ReaderChange {
    const char* id;
    const char* description;
};

const ReaderChange kReaderChanges[] = {
    {"ads-mode",
     "e3f6d3f (feat: keep head tracking on through aim down sights) retired the ADS mode cycle: [Camera] ads_mode is no "
     "longer read, and Insert / Ctrl+Shift+U no longer cycle it"},
    {"insert-yaw",
     "e3f6d3f stopped moving a YawModeKey of Insert (0x2D) to Delete (0x2E), which v0.3.1 did because Insert was the "
     "ADS key: a file that says Insert now toggles yaw mode on Insert"},
    {"camera-section",
     "e3f6d3f stopped reading [Camera] with GetPrivateProfileSectionA, and with it v0.3.1's refusal of a [Camera] "
     "section longer than 4094 bytes"},
};

// Comparison 1 for one input: the ids of the reader changes that explain every
// difference, or nullopt when a difference has no explanation.
std::optional<std::set<std::string>> ExplainReaderChanges(const Record& oracle, const Record& import) {
    std::set<std::string> found;
    if (oracle.at("status") == "refused" && oracle.at("reason") == "[Camera] section exceeds 4094 bytes") {
        if (oracle != import) found.insert("camera-section");
        return found;
    }
    if (oracle.at("status") != import.at("status")) return std::nullopt;
    if (oracle.at("status") == "refused") {
        if (oracle.at("reason") != import.at("reason")) return std::nullopt;
        return found;
    }
    std::set<std::string> names;
    for (const auto& entry : oracle) names.insert(entry.first);
    for (const auto& entry : import) names.insert(entry.first);
    for (const std::string& name : names) {
        const auto o = oracle.find(name);
        const auto i = import.find(name);
        if (o != oracle.end() && i != import.end() && o->second == i->second) continue;
        if (i == import.end() && (name == "field.adsMode" || name == "field.adsModeKey" || name == "startup.adsMode" ||
                                  name == "hotkey.CycleAdsMode")) {
            found.insert("ads-mode");
            continue;
        }
        const bool insertMoved = import.at("field.yawModeKey") == Hex(VK_INSERT) &&
                                 oracle.at("field.yawModeKey") == Hex(VK_DELETE);
        if (insertMoved && (name == "field.yawModeKey" || name == "hotkey.YawMode")) {
            found.insert("insert-yaw");
            continue;
        }
        return std::nullopt;
    }
    return found;
}

std::string Describe(const Record& record) {
    std::string text;
    for (const auto& entry : record) text += "\n    " + entry.first + " = " + entry.second;
    return text;
}

const char* ModeName(cameraunlock::TrackingMode mode) {
    switch (mode) {
        case cameraunlock::TrackingMode::RotationAndPosition:
            return "RotationAndPosition";
        case cameraunlock::TrackingMode::RotationOnly:
            return "RotationOnly";
        case cameraunlock::TrackingMode::PositionOnly:
            return "PositionOnly";
    }
    throw std::logic_error("not a tracking mode");
}

std::string ListBindings(const std::string& list) {
    std::vector<std::pair<unsigned, int>> items;
    for (const auto& binding : HeadTracking::KeyBindings(list)) {
        items.push_back({static_cast<unsigned>(binding.modifiers), binding.vk});
    }
    return Bindings(items);
}

// A Config through the game's startup code (HeadTrackingPlugin::Initialize and
// ApplyConfig), in the names ImportRecord uses.
Record ConfigRecord(const Config& config) {
    Record record;
    record["field.udpPort"] = std::to_string(config.udp_port);
    record["field.localSmoothing"] = Bits(config.local_smoothing);
    record["field.remoteSmoothing"] = Bits(config.remote_smoothing);
    record["field.debounceMs"] = std::to_string(config.hotkey_debounce_ms);
    record["field.inputBlockMode"] = std::to_string(static_cast<int>(config.input_block_mode));
    record["field.trackInThirdPerson"] = Flag(config.track_in_third_person);
    record["field.trackInVATS"] = Flag(config.track_in_vats);
    record["field.pauseDuringCombat"] = Flag(config.pause_during_combat);
    record["field.worldSpaceYaw"] = Flag(config.world_space_yaw);
    record["startup.enabled"] = Flag(config.enable_on_startup);
    record["startup.mode"] = ModeName(HeadTracking::StartupTrackingMode(config));
    record["startup.worldSpaceYaw"] = Flag(config.world_space_yaw);
    record["hotkey.Toggle"] = ListBindings(config.toggle_key);
    record["hotkey.CycleTrackingMode"] = ListBindings(config.cycle_tracking_mode_key);
    record["hotkey.YawMode"] = ListBindings(config.yaw_mode_key);
    return record;
}

bool Dropped(const cfg::ImportResult& import, cfg::DropRule rule, const char* section, const char* key) {
    for (const auto& dropped : import.dropped) {
        if (dropped.rule == rule && dropped.section == section && dropped.key == key) return true;
    }
    return false;
}

bool IsNaN(const std::string& bits) {
    const unsigned long long value = std::stoull(bits, nullptr, 16);
    double number = 0;
    std::memcpy(&number, &value, sizeof(number));
    return std::isnan(number);
}

// Comparison 2 for one input: empty when every difference between the import
// and the migration is one core's data/config-format.json approves, and the
// import recorded it; otherwise what is left.
std::vector<std::string> UnexplainedMigrationDifferences(const std::string& input, const Record& import,
                                                         const cfg::ImportResult& result, const Record& migration) {
    std::vector<std::string> left;
    if (import.at("status") != migration.at("status")) {
        left.push_back("status " + import.at("status") + " -> " + migration.at("status"));
        return left;
    }
    if (import.at("status") == "refused") {
        if (import.at("reason") != migration.at("reason")) left.push_back("reason");
        return left;
    }
    std::set<std::string> names;
    for (const auto& entry : import) names.insert(entry.first);
    for (const auto& entry : migration) names.insert(entry.first);
    const Config defaults;
    for (const std::string& name : names) {
        const auto i = import.find(name);
        const auto m = migration.find(name);
        if (i != import.end() && m != migration.end() && i->second == m->second) continue;
        // The raw codes are compared as the bindings they register (hotkey.*).
        if (name == "field.toggleKey" || name == "field.cycleTrackingModeKey" || name == "field.yawModeKey" ||
            name == "field.reticleToggleKey") {
            continue;
        }
        // [Feedback] ShowMessages is dead: it gated messages to a game console
        // this build never had (g_ConsolePrint is always null).
        if (name == "field.showMessages") continue;
        // Approved change `reticle`: the reticle toggle key and its chord.
        if (name == "hotkey.ReticleToggle" && m == migration.end() &&
            Dropped(result, cfg::DropRule::Reticle, "Hotkeys", "ReticleToggle")) {
            continue;
        }
        // Normalisation N2: a NaN smoothing, which the range check let through.
        if (name == "field.localSmoothing" && IsNaN(i->second) && m->second == Bits(defaults.local_smoothing) &&
            Dropped(result, cfg::DropRule::NonFiniteNumber, "Smoothing", "LocalSmoothing")) {
            continue;
        }
        if (name == "field.remoteSmoothing" && IsNaN(i->second) && m->second == Bits(defaults.remote_smoothing) &&
            Dropped(result, cfg::DropRule::NonFiniteNumber, "Smoothing", "RemoteSmoothing")) {
            continue;
        }
        // The moved default: with no file, the yaw mode takes the fleet's
        // PageDown / Ctrl+Shift+H in place of the Delete / Ctrl+Shift+J the
        // published build's first-run file wrote.
        if (name == "hotkey.YawMode" && input == "no file" && m->second == ListBindings(defaults.yaw_mode_key)) {
            continue;
        }
        left.push_back(name + ": " + (i == import.end() ? "(none)" : i->second) + " -> " +
                       (m == migration.end() ? "(none)" : m->second));
    }
    return left;
}

std::vector<std::pair<std::string, std::string>> Listing(const fs::path& dir) {
    std::vector<std::pair<std::string, std::string>> files;
    for (const auto& entry : fs::directory_iterator(dir)) {
        files.push_back({entry.path().filename().string(), ReadBytes(entry.path())});
    }
    std::sort(files.begin(), files.end());
    return files;
}

cfg::LegacyInput InputFor(const fs::path& path) {
    return cfg::LegacyInput{path.wstring(), path.string(), false};
}

std::string RenderedDefaults() {
    const cfg::ConfigTable<Config> table = HeadTracking::ConfigTable();
    cfg::RenderHeader header;
    header.display_name = HeadTracking::kGameDisplayName;
    return cfg::RenderCanonical(table, table.defaults(), header);
}

// Runs the migration on the input at dir/migrate and checks what the design
// asks of it beyond comparison 2. Returns the migration's record.
Record Migrate(const Input& input, const fs::path& dir, const cfg::ImportResult& imported) {
    const fs::path path = dir / "HeadTracking.ini";
    cfg::ConfigOwner<Config> owner(HeadTracking::ConfigOwnerOptions(path.wstring()));
    const cfg::ConfigLoadResult<Config> loaded = owner.Load();
    const std::string label = input.name + ": ";
    Record record;
    switch (loaded.status) {
        case cfg::ConfigLoadStatus::LegacyRefused:
            record["status"] = "refused";
            record["reason"] = imported.status == cfg::ImportStatus::Refused ? imported.reason : std::string();
            Check(ReadBytes(path) == *input.bytes, label + "a refused file keeps its bytes");
            Check(!fs::exists(dir / "HeadTracking.ini.pre-canonical"), label + "a refused file gets no copy");
            return record;
        case cfg::ConfigLoadStatus::Created:
            record = ConfigRecord(loaded.config);
            record["status"] = "absent";
            Check(ReadBytes(path) == RenderedDefaults(), label + "a first launch writes the committed file's bytes");
            return record;
        case cfg::ConfigLoadStatus::Migrated:
            break;
        default:
            Fail(label + "the migration loaded " + cfg::ConfigLoadStatusName(loaded.status) +
                 (loaded.reason.empty() ? std::string() : ": " + loaded.reason));
            return Record{{"status", cfg::ConfigLoadStatusName(loaded.status)}};
    }
    record = ConfigRecord(loaded.config);
    record["status"] = "usable";

    const std::string migrated = ReadBytes(path);
    Check(cfg::HasCanonicalStamp(migrated), label + "the migrated file carries the stamp");
    const cfg::CanonicalIni doc = cfg::ParseCanonicalIni(migrated);
    Config reread = HeadTracking::ConfigTable().defaults();
    Check(doc.IsReadable() && doc.diagnostics.empty() &&
              cfg::ApplyCanonical(doc, HeadTracking::ConfigTable(), reread).diagnostics.empty(),
          label + "the migrated file reads without a diagnostic");
    cfg::RenderHeader header;
    header.display_name = HeadTracking::kGameDisplayName;
    Check(cfg::RenderCanonical(HeadTracking::ConfigTable(), reread, header) == migrated,
          label + "rendering the migrated settings gives the migrated bytes");
    Check(ReadBytes(dir / "HeadTracking.ini.pre-canonical") == *input.bytes,
          label + "HeadTracking.ini.pre-canonical holds the input");

    cfg::ConfigOwner<Config> relaunch(HeadTracking::ConfigOwnerOptions(path.wstring()));
    Check(relaunch.Load().status == cfg::ConfigLoadStatus::Canonical && ReadBytes(path) == migrated,
          label + "migrating the migrated file does nothing");
    return record;
}

fs::path MakeTempRoot() {
    wchar_t temp[MAX_PATH + 1];
    if (GetTempPathW(MAX_PATH + 1, temp) == 0) throw std::runtime_error("GetTempPathW failed");
    const fs::path root = fs::path(temp) / ("fnv-config-differential-" + std::to_string(GetCurrentProcessId()));
    fs::remove_all(root);
    fs::create_directories(root);
    return root;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: fnv_config_differential <fnv_config_oracle.exe>\n");
        return 2;
    }
    const fs::path oracle = argv[1];
    const fs::path root = MakeTempRoot();
    const std::vector<Input> inputs = Inputs();

    std::string list;
    for (std::size_t i = 0; i < inputs.size(); ++i) {
        const fs::path dir = root / std::to_string(i);
        fs::create_directories(dir / "oracle");
        fs::create_directories(dir / "import");
        if (inputs[i].bytes) {
            WriteBytes(dir / "oracle" / "HeadTracking.ini", *inputs[i].bytes);
            WriteBytes(dir / "import" / "HeadTracking.ini", *inputs[i].bytes);
        }
        list += (dir / "oracle" / "HeadTracking.ini").string() + "\n";
    }
    WriteBytes(root / "oracle-inputs.txt", list);
    const std::vector<Record> published = RunOracle(oracle, root / "oracle-inputs.txt", root / "oracle-records.txt");
    Check(published.size() == inputs.size(), "the oracle read " + std::to_string(published.size()) + " of " +
                                                 std::to_string(inputs.size()) + " inputs");

    std::map<std::string, std::vector<std::string>> changesSeen;
    for (std::size_t i = 0; i < inputs.size() && i < published.size(); ++i) {
        const Input& input = inputs[i];
        const Record import = ImportRecord(root / std::to_string(i) / "import" / "HeadTracking.ini");
        const auto explained = ExplainReaderChanges(published[i], import);
        if (!explained) {
            Fail("comparison 1, " + input.name + ": the frozen reader differs from v0.3.1 in a way no commit explains" +
                 "\n  oracle:" + Describe(published[i]) + "\n  import:" + Describe(import));
            continue;
        }
        for (const std::string& id : *explained) changesSeen[id].push_back(input.name);
    }

    std::printf("Comparison 1 (v0.3.1 against the frozen reader) over %zu inputs:\n", inputs.size());
    for (const ReaderChange& change : kReaderChanges) {
        const auto seen = changesSeen.find(change.id);
        const std::size_t count = seen == changesSeen.end() ? 0 : seen->second.size();
        std::printf("  %s\n    %zu inputs, e.g. %s\n", change.description, count,
                    count == 0 ? "none" : seen->second.front().c_str());
        Check(count > 0, std::string("no input shows the recorded change '") + change.id + "'");
    }

    // Comparison 2 and the migration's own checks.
    std::size_t migrated = 0;
    for (std::size_t i = 0; i < inputs.size(); ++i) {
        const Input& input = inputs[i];
        const fs::path dir = root / std::to_string(i);
        const Record import = ImportRecord(dir / "import" / "HeadTracking.ini");

        // The import on a read-only copy, as the owner runs it, leaves the
        // folder as it was.
        const fs::path readOnly = dir / "read-only";
        fs::create_directories(readOnly);
        if (input.bytes) {
            WriteBytes(readOnly / "HeadTracking.ini", *input.bytes);
            SetFileAttributesW((readOnly / "HeadTracking.ini").c_str(), FILE_ATTRIBUTE_READONLY);
        }
        const auto before = Listing(readOnly);
        Config mapped;
        const cfg::ImportResult result = HeadTracking::ConfigLegacyImport().run(InputFor(readOnly / "HeadTracking.ini"), mapped);
        Check(Listing(readOnly) == before, input.name + ": the import writes nothing");
        if (input.bytes) SetFileAttributesW((readOnly / "HeadTracking.ini").c_str(), FILE_ATTRIBUTE_NORMAL);

        fs::create_directories(dir / "migrate");
        if (input.bytes) WriteBytes(dir / "migrate" / "HeadTracking.ini", *input.bytes);
        const Record migration = Migrate(input, dir / "migrate", result);
        if (migration.at("status") == "usable") ++migrated;
        const auto left = UnexplainedMigrationDifferences(input.name, import, result, migration);
        for (const std::string& difference : left) {
            Fail("comparison 2, " + input.name + ": " + difference);
        }
    }
    std::printf("Comparison 2 (the frozen reader against the migration): %zu inputs migrated\n", migrated);

    // A player who installed the newest published build and changed nothing
    // gets the committed file, apart from the yaw mode key the build shipped.
    const std::string committed = ReadBytes(fs::path(FNV_SOURCE_DIR) / "config" / "HeadTracking.ini");
    Check(committed == RenderedDefaults(), "config/HeadTracking.ini is what the table renders");
    const std::string expectedUpgrade =
        Replace(committed, "YawModeKey=PageDown, Ctrl+Shift+H\r\n", "YawModeKey=Delete, Ctrl+Shift+J\r\n");
    for (const char* file : {"v0.3.1/shipped.ini", "v0.3.1/seed.ini", "v0.3.1/first-run.ini"}) {
        const fs::path dir = root / "upgrade" / fs::path(file).stem();
        fs::create_directories(dir);
        WriteBytes(dir / "HeadTracking.ini", ReadBytes(kData / file));
        cfg::ConfigOwner<Config> owner(HeadTracking::ConfigOwnerOptions((dir / "HeadTracking.ini").wstring()));
        Check(owner.Load().status == cfg::ConfigLoadStatus::Migrated, std::string(file) + " migrates");
        Check(ReadBytes(dir / "HeadTracking.ini") == expectedUpgrade,
              std::string(file) + " migrates to the committed file with the Delete / Ctrl+Shift+J yaw key it shipped");
    }

    fs::remove_all(root);
    if (g_failures != 0) {
        std::fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    std::puts("Config differential tests passed");
    return 0;
}
