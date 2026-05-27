#pragma once

#include <cstdint>

namespace HeadTracking {

// Version components for scripts/packaging
constexpr int VERSION_MAJOR = 1;
constexpr int VERSION_MINOR = 0;
constexpr int VERSION_PATCH = 0;

// Packed version for NVSE (major.minor.patch.0)
constexpr uint32_t PLUGIN_VERSION = (VERSION_MAJOR << 24) | (VERSION_MINOR << 16) | (VERSION_PATCH << 8);

constexpr const char* PLUGIN_NAME = "HeadTracking";
constexpr uint32_t NVSE_VERSION_REQUIRED = 0x06040001;  // 6.4.1

}  // namespace HeadTracking
