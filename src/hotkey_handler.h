#pragma once

#include <cameraunlock/input/hotkey_poller.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <vector>

namespace HeadTracking {

struct Config;

// The three hotkey lists from HeadTracking.ini on core's HotkeyPoller. Actions
// run on the poller's thread, never on the render thread, and only while the
// game state allows input and the action has not fired within the debounce
// time.
class HotkeyHandler {
public:
    struct Actions {
        std::function<void()> toggle;
        std::function<void()> cycleTrackingMode;
        std::function<void()> toggleYawMode;
    };

    explicit HotkeyHandler(Actions actions);

    HotkeyHandler(const HotkeyHandler&) = delete;
    HotkeyHandler& operator=(const HotkeyHandler&) = delete;

    // Registers the config's key lists in place of the ones registered before,
    // and takes its debounce time.
    void Bind(const Config& config);

    void Start();
    void Stop();

    // Set from the render thread each frame (GameState::CanProcessInput).
    void SetInputAllowed(bool allowed) { m_inputAllowed.store(allowed); }

private:
    enum Slot { kToggle, kCycleTrackingMode, kToggleYawMode, kSlotCount };

    std::function<void()> Guarded(Slot slot, std::function<void()> action);

    cameraunlock::input::HotkeyPoller m_poller;
    Actions m_actions;
    std::vector<int> m_registered;
    std::atomic<bool> m_inputAllowed{false};
    std::atomic<uint64_t> m_debounceMs{0};
    // Written and read on the poller thread only.
    uint64_t m_lastFire[kSlotCount] = {};
};

}  // namespace HeadTracking
