#pragma once

// One reading of a config file as the differential test compares it, the same
// shape for the published build (the oracle, its own executable), the frozen
// import and the migration. Names are `status`, `reason`, `field.<name>` for a
// field of the reader's Config, `startup.<name>` for the state the game starts
// in, and `hotkey.<action>` for the bindings it registers.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace fnv_differential {

using Record = std::map<std::string, std::string>;

inline std::string Bits(double value) {
    std::uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    char text[32];
    std::snprintf(text, sizeof(text), "0x%016llX", static_cast<unsigned long long>(bits));
    return text;
}

inline std::string Hex(int value) {
    char text[16];
    std::snprintf(text, sizeof(text), "0x%02X", value);
    return text;
}

inline std::string Flag(bool value) { return value ? "1" : "0"; }

// Modifier bits as core's KeyModifiers numbers them: Ctrl 1, Shift 2, Alt 4.
constexpr unsigned kCtrlShift = 3;

// A binding list as `modifiers:code` items in ascending order, so two lists
// compare as sets: the order a list names its keys in never changes what fires.
inline std::string Bindings(std::vector<std::pair<unsigned, int>> items) {
    std::sort(items.begin(), items.end());
    std::string text;
    for (const auto& item : items) {
        if (!text.empty()) text += ' ';
        text += std::to_string(item.first) + ":" + Hex(item.second);
    }
    return text;
}

// Records are written one `name<TAB>value` line each, ended by a line `end`.
inline std::string Serialize(const Record& record) {
    std::string out;
    for (const auto& entry : record) out += entry.first + "\t" + entry.second + "\n";
    return out + "end\n";
}

}  // namespace fnv_differential
