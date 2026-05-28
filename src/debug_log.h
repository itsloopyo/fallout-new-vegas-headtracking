#pragma once

// Centralized debug logging with compile-time control
// Define HEADTRACKING_DEBUG_LOGGING=1 to enable debug file logging
// In release builds, all logging calls compile to nothing (zero overhead)

#ifndef HEADTRACKING_DEBUG_LOGGING
#define HEADTRACKING_DEBUG_LOGGING 0
#endif

#if HEADTRACKING_DEBUG_LOGGING

#include <Windows.h>
#include <cstdio>
#include <cstdarg>
#include <cstdint>

namespace HeadTracking {
namespace Debug {

// Log categories for selective enabling
enum class LogCategory : uint8_t {
    Main     = 1 << 0,
    Camera   = 1 << 1,
    Udp      = 1 << 2,
    D3D      = 1 << 3,
    Plugin   = 1 << 4,
    Hotkey   = 1 << 5,
    All      = 0xFF
};

// Enable all categories by default when debug is on
constexpr uint8_t kEnabledCategories = static_cast<uint8_t>(LogCategory::All);

// Maximum log entries per category to prevent runaway disk usage
constexpr int kMaxLogEntriesPerFile = 5000;

// Buffered logger - reduces fflush() calls by batching writes
class BufferedLogger {
public:
    static BufferedLogger& Instance() {
        static BufferedLogger instance;
        return instance;
    }

    void Log(LogCategory category, const char* filename, const char* fmt, ...) {
        if (!(static_cast<uint8_t>(category) & kEnabledCategories)) {
            return;
        }

        // Get or create file for this category
        FILE* file = GetOrCreateFile(category, filename);
        if (!file) {
            return;
        }

        // Check entry limit
        int& count = GetEntryCount(category);
        if (count >= kMaxLogEntriesPerFile) {
            return;
        }

        va_list args;
        va_start(args, fmt);
        vfprintf(file, fmt, args);
        fprintf(file, "\n");
        va_end(args);

        count++;

        // Batch flush: only flush every N entries to reduce disk I/O
        if (count % 100 == 0) {
            fflush(file);
        }
    }

    // Force flush all open log files (call on shutdown)
    void FlushAll() {
        for (int i = 0; i < 6; i++) {
            if (m_files[i]) {
                fflush(m_files[i]);
            }
        }
    }

private:
    BufferedLogger() {
        for (int i = 0; i < 6; i++) {
            m_files[i] = nullptr;
            m_entryCounts[i] = 0;
        }
    }

    ~BufferedLogger() {
        FlushAll();
        for (int i = 0; i < 6; i++) {
            if (m_files[i]) {
                fclose(m_files[i]);
                m_files[i] = nullptr;
            }
        }
    }

    int CategoryIndex(LogCategory cat) {
        switch (cat) {
            case LogCategory::Main:   return 0;
            case LogCategory::Camera: return 1;
            case LogCategory::Udp:    return 2;
            case LogCategory::D3D:    return 3;
            case LogCategory::Plugin: return 4;
            case LogCategory::Hotkey: return 5;
            default: return 0;
        }
    }

    FILE* GetOrCreateFile(LogCategory category, const char* filename) {
        int idx = CategoryIndex(category);
        if (!m_files[idx]) {
            m_files[idx] = fopen(filename, "w");
            if (m_files[idx]) {
                fprintf(m_files[idx], "=== HeadTracking Debug Log Started ===\n");
            }
        }
        return m_files[idx];
    }

    int& GetEntryCount(LogCategory category) {
        return m_entryCounts[CategoryIndex(category)];
    }

    FILE* m_files[6];
    int m_entryCounts[6];
};

}  // namespace Debug
}  // namespace HeadTracking

// Macro-based logging that includes category
#define HT_DEBUG_LOG(category, filename, fmt, ...) \
    HeadTracking::Debug::BufferedLogger::Instance().Log( \
        HeadTracking::Debug::LogCategory::category, filename, fmt, ##__VA_ARGS__)

// Convenience macros for each category
#define HT_LOG_MAIN(fmt, ...)   HT_DEBUG_LOG(Main, "HeadTracking_debug.log", fmt, ##__VA_ARGS__)
#define HT_LOG_CAMERA(fmt, ...) HT_DEBUG_LOG(Camera, "HeadTracking_camera.log", fmt, ##__VA_ARGS__)
#define HT_LOG_UDP(fmt, ...)    HT_DEBUG_LOG(Udp, "HeadTracking_udp.log", fmt, ##__VA_ARGS__)
#define HT_LOG_D3D(fmt, ...)    HT_DEBUG_LOG(D3D, "HeadTracking_d3d.log", fmt, ##__VA_ARGS__)
#define HT_LOG_PLUGIN(fmt, ...) HT_DEBUG_LOG(Plugin, "HeadTracking_debug.log", fmt, ##__VA_ARGS__)
#define HT_LOG_HOTKEY(fmt, ...) HT_DEBUG_LOG(Hotkey, "HeadTracking_hotkey.log", fmt, ##__VA_ARGS__)

// Conditional logging based on frame count
#define HT_LOG_PERIODIC(category, filename, interval, counter, fmt, ...) \
    do { if ((counter) % (interval) == 0) HT_DEBUG_LOG(category, filename, fmt, ##__VA_ARGS__); } while(0)

#else  // !HEADTRACKING_DEBUG_LOGGING

// When debug logging is disabled, all macros compile to nothing
#define HT_DEBUG_LOG(category, filename, fmt, ...) ((void)0)
#define HT_LOG_MAIN(fmt, ...)   ((void)0)
#define HT_LOG_CAMERA(fmt, ...) ((void)0)
#define HT_LOG_UDP(fmt, ...)    ((void)0)
#define HT_LOG_D3D(fmt, ...)    ((void)0)
#define HT_LOG_PLUGIN(fmt, ...) ((void)0)
#define HT_LOG_HOTKEY(fmt, ...) ((void)0)
#define HT_LOG_PERIODIC(category, filename, interval, counter, fmt, ...) ((void)0)

namespace HeadTracking {
namespace Debug {
class BufferedLogger {
public:
    static BufferedLogger& Instance() { static BufferedLogger instance; return instance; }
    void FlushAll() {}
};
}  // namespace Debug
}  // namespace HeadTracking

#endif  // HEADTRACKING_DEBUG_LOGGING
