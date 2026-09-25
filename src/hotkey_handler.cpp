#include <Windows.h>

#include "hotkey_handler.h"
#include "config.h"

#include <cameraunlock/input/key_binding_registration.h>

#include <utility>

namespace HeadTracking {

HotkeyHandler::HotkeyHandler(Actions actions) : m_actions(std::move(actions)) {}

std::function<void()> HotkeyHandler::Guarded(Slot slot, std::function<void()> action) {
    return [this, slot, action = std::move(action)]() {
        if (!m_inputAllowed.load()) return;
        const uint64_t now = GetTickCount64();
        if (now - m_lastFire[slot] < m_debounceMs.load()) return;
        m_lastFire[slot] = now;
        action();
    };
}

void HotkeyHandler::Bind(const Config& config) {
    for (const int id : m_registered) m_poller.RemoveHotkey(id);
    m_registered.clear();
    m_debounceMs.store(config.hotkey_debounce_ms);

    const auto add = [this](const std::string& list, Slot slot, const std::function<void()>& action) {
        const std::vector<int> ids =
            cameraunlock::input::RegisterKeyBindings(m_poller, KeyBindings(list), Guarded(slot, action));
        m_registered.insert(m_registered.end(), ids.begin(), ids.end());
    };
    add(config.toggle_key, kToggle, m_actions.toggle);
    add(config.cycle_tracking_mode_key, kCycleTrackingMode, m_actions.cycleTrackingMode);
    add(config.yaw_mode_key, kToggleYawMode, m_actions.toggleYawMode);
}

void HotkeyHandler::Start() { m_poller.Start(); }

void HotkeyHandler::Stop() { m_poller.Stop(); }

}  // namespace HeadTracking
