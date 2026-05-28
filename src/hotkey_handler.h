#pragma once

#include <Windows.h>

#include <cstdint>

namespace HeadTracking {

// Forward declarations
class CameraController;
class UdpReceiver;
class GameState;

// Default nav-cluster bindings
//   Home      = recenter
//   End       = toggle tracking
//   Page Up   = cycle tracking mode (normal -> rot only -> pos only -> normal)
//   Page Down = reticle toggle
constexpr int VK_RECENTER_DEFAULT = VK_HOME;                // 0x24
constexpr int VK_TOGGLE_DEFAULT = VK_END;                   // 0x23
constexpr int VK_CYCLE_TRACKING_MODE_DEFAULT = VK_PRIOR;    // 0x21 (Page Up)
constexpr int VK_RETICLE_TOGGLE_DEFAULT = VK_NEXT;          // 0x22 (Page Down)
// Page Down is taken by the reticle toggle in this mod, so the yaw-mode
// toggle uses the next free nav-cluster key (Insert) instead of the
// catalogue-standard Page Down.
constexpr int VK_YAW_MODE_DEFAULT = VK_INSERT;              // 0x2D

// Fixed Ctrl+Shift+<letter> chord letters (T/Y/U/G/H/J cluster).
// Slot order across all CameraUnlock mods: T, Y, G, H, U, J.
constexpr int VK_CHORD_RECENTER = 'T';              // 0x54
constexpr int VK_CHORD_TOGGLE = 'Y';                // 0x59
constexpr int VK_CHORD_CYCLE_TRACKING_MODE = 'G';   // 0x47
constexpr int VK_CHORD_RETICLE_TOGGLE = 'H';        // 0x48
constexpr int VK_CHORD_YAW_MODE = 'U';              // 0x55

// Key state tracking to detect press events (not hold)
struct KeyState {
    bool wasPressed;   // Key was down last frame
    bool isPressed;    // Key is down this frame
    uint64_t lastPressTime;  // Timestamp of last press (for debounce)

    KeyState()
        : wasPressed(false)
        , isPressed(false)
        , lastPressTime(0) {
    }

    // Check if key was just pressed (transition from up to down)
    bool JustPressed() const {
        return isPressed && !wasPressed;
    }

    // Update state from current key status
    void Update(bool currentlyDown) {
        wasPressed = isPressed;
        isPressed = currentlyDown;
    }
};

// Hotkey action types
enum class HotkeyAction {
    None,
    Recenter,            // Reset center position to current head orientation
    Toggle,              // Toggle head tracking on/off
    CycleTrackingMode,   // Cycle: normal -> rotation-only -> position-only -> normal
    ReticleToggle,       // Toggle aim reticle on/off
    ToggleYawMode        // Toggle world-space (horizon-locked) vs camera-local yaw
};

class HotkeyHandler {
public:
    HotkeyHandler();
    ~HotkeyHandler();

    // Disable copying
    HotkeyHandler(const HotkeyHandler&) = delete;
    HotkeyHandler& operator=(const HotkeyHandler&) = delete;
    HotkeyHandler(HotkeyHandler&&) = delete;
    HotkeyHandler& operator=(HotkeyHandler&&) = delete;

    // Initialize with references to controlled components
    void Initialize(CameraController* cameraController, UdpReceiver* udpReceiver, GameState* gameState);

    // Process hotkeys - call every frame
    // Returns the action that was triggered (if any)
    HotkeyAction Update();

    // Set whether hotkeys are processed
    void SetEnabled(bool enabled) { m_enabled = enabled; }

    // Configure nav-cluster hotkey bindings (virtual key codes).
    // Chord (Ctrl+Shift+<letter>) bindings are fixed and not configurable.
    // Returns false if key code is invalid (fails fast).
    bool SetRecenterKey(int vkCode);
    bool SetToggleKey(int vkCode);
    bool SetCycleTrackingModeKey(int vkCode);
    bool SetReticleToggleKey(int vkCode);
    bool SetYawModeKey(int vkCode);

    // Debounce time in milliseconds (prevent rapid repeated triggers)
    void SetDebounceTime(uint64_t ms) { m_debounceMs = ms; }

    // Feedback settings
    void SetShowMessages(bool show) { m_showMessages = show; }

private:
    // Validate and assign a nav-cluster binding, resetting its edge-detect
    // state. actionName is used only in the fail-fast error message.
    // Returns false (and leaves the binding untouched) if vkCode is invalid.
    bool SetKeyBinding(int& target, KeyState& state, int vkCode, const char* actionName);

    // Check if a key is currently held down
    bool IsKeyDown(int vkCode) const;

    // Resolve whether an action's binding is currently held. Suppresses the
    // nav-cluster check while Ctrl+Shift is held so a single keypress does
    // not double-fire when both bindings would otherwise resolve true.
    bool IsActionDown(int navVK, int chordLetterVK, bool ctrlShiftHeld) const;

    // Execute the recenter action
    void ExecuteRecenter();

    // Execute the toggle action
    void ExecuteToggle();

    // Show feedback message to player
    void ShowFeedback(const char* message);

    // References to controlled components (not owned)
    CameraController* m_cameraController;
    UdpReceiver* m_udpReceiver;

    // Nav-cluster key bindings (virtual key codes)
    int m_recenterKeyCode;
    int m_toggleKeyCode;
    int m_cycleTrackingModeKeyCode;
    int m_reticleToggleKeyCode;
    int m_yawModeKeyCode;

    // Shared edge-detect state per action (covers both nav and chord bindings)
    KeyState m_recenterKeyState;
    KeyState m_toggleKeyState;
    KeyState m_cycleTrackingModeKeyState;
    KeyState m_reticleToggleKeyState;
    KeyState m_yawModeKeyState;

    // Enabled state
    bool m_enabled;
    bool m_initialized;

    // Debounce timing
    uint64_t m_debounceMs;

    // Feedback settings
    bool m_showMessages;
};

}  // namespace HeadTracking
