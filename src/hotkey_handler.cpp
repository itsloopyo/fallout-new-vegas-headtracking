#include <Windows.h>

#include "hotkey_handler.h"
#include "camera_controller.h"
#include "udp_receiver.h"
#include "game_state.h"
#include "plugin.h"
#include "tracking_data.h"
#include "debug_log.h"

namespace HeadTracking {

HotkeyHandler::HotkeyHandler()
    : m_cameraController(nullptr)
    , m_udpReceiver(nullptr)
    , m_recenterKeyCode(VK_RECENTER_DEFAULT)
    , m_toggleKeyCode(VK_TOGGLE_DEFAULT)
    , m_cycleTrackingModeKeyCode(VK_CYCLE_TRACKING_MODE_DEFAULT)
    , m_reticleToggleKeyCode(VK_RETICLE_TOGGLE_DEFAULT)
    , m_yawModeKeyCode(VK_YAW_MODE_DEFAULT)
    , m_recenterKeyState()
    , m_toggleKeyState()
    , m_cycleTrackingModeKeyState()
    , m_reticleToggleKeyState()
    , m_yawModeKeyState()
    , m_enabled(true)
    , m_initialized(false)
    , m_debounceMs(200)
    , m_showMessages(true) {
}

HotkeyHandler::~HotkeyHandler() {
}

void HotkeyHandler::Initialize(CameraController* cameraController, UdpReceiver* udpReceiver, GameState* gameState) {
    (void)gameState;  // Unused - game state check disabled, hotkeys always work

    if (m_initialized) {
        return;
    }

    if (!cameraController) {
        if (g_ConsolePrint) {
            g_ConsolePrint("HeadTracking: ERROR - HotkeyHandler::Initialize called with null CameraController");
        }
        return;
    }

    m_cameraController = cameraController;
    m_udpReceiver = udpReceiver;
    m_initialized = true;

    if (g_ConsolePrint) {
        g_ConsolePrint("HeadTracking: Hotkey handler initialized");
        g_ConsolePrint("HeadTracking:   Recenter:            0x%02X / Ctrl+Shift+T", m_recenterKeyCode);
        g_ConsolePrint("HeadTracking:   Toggle tracking:     0x%02X / Ctrl+Shift+Y", m_toggleKeyCode);
        g_ConsolePrint("HeadTracking:   Cycle tracking mode: 0x%02X / Ctrl+Shift+G", m_cycleTrackingModeKeyCode);
        g_ConsolePrint("HeadTracking:   Reticle toggle:      0x%02X / Ctrl+Shift+H", m_reticleToggleKeyCode);
        g_ConsolePrint("HeadTracking:   Yaw mode toggle:     0x%02X / Ctrl+Shift+U", m_yawModeKeyCode);
    }
}

HotkeyAction HotkeyHandler::Update() {
#if HEADTRACKING_DEBUG_LOGGING
    static int updateCount = 0;
    updateCount++;
    if (updateCount <= 5) {
        HT_LOG_HOTKEY("Update call %d: init=%d enabled=%d recenterKey=0x%02X toggleKey=0x%02X",
                  updateCount, m_initialized, m_enabled, m_recenterKeyCode, m_toggleKeyCode);
    }
#endif

    if (!m_initialized || !m_enabled) {
        return HotkeyAction::None;
    }

    // Resolve modifier state once per frame. GetAsyncKeyState(VK_CONTROL) /
    // VK_SHIFT report either left- or right-side modifier transparently.
    const bool ctrlShiftHeld =
        (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0 &&
        (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;

    // Each action accepts EITHER its nav-cluster key (when Ctrl+Shift is not
    // held) OR its Ctrl+Shift+<letter> chord. Shared edge-detection state
    // means holding both does not double-fire.
    bool recenterDown = IsActionDown(m_recenterKeyCode, VK_CHORD_RECENTER, ctrlShiftHeld);
    bool toggleDown = IsActionDown(m_toggleKeyCode, VK_CHORD_TOGGLE, ctrlShiftHeld);
    bool cycleDown = IsActionDown(m_cycleTrackingModeKeyCode, VK_CHORD_CYCLE_TRACKING_MODE, ctrlShiftHeld);
    bool reticleDown = IsActionDown(m_reticleToggleKeyCode, VK_CHORD_RETICLE_TOGGLE, ctrlShiftHeld);
    bool yawModeDown = IsActionDown(m_yawModeKeyCode, VK_CHORD_YAW_MODE, ctrlShiftHeld);

    if (recenterDown || toggleDown || cycleDown || reticleDown || yawModeDown) {
        HT_LOG_HOTKEY("Key press detected: Recenter=%d Toggle=%d Cycle=%d Reticle=%d YawMode=%d ctrlShift=%d",
                  recenterDown, toggleDown, cycleDown, reticleDown, yawModeDown, ctrlShiftHeld);
    }

    m_recenterKeyState.Update(recenterDown);
    m_toggleKeyState.Update(toggleDown);
    m_cycleTrackingModeKeyState.Update(cycleDown);
    m_reticleToggleKeyState.Update(reticleDown);
    m_yawModeKeyState.Update(yawModeDown);

    uint64_t currentTime = GetTickCount64();

    // Edge-detect + debounce: fire only on rising edge, and only if enough time
    // has passed since the previous fire of the same action. Updates lastPressTime
    // on success so the next fire is gated by the same debounce window.
    auto tryFire = [&](KeyState& state, const char* name) -> bool {
        (void)name;  // Only referenced when debug logging is enabled
        if (!state.JustPressed()) return false;
        HT_LOG_HOTKEY("%s JustPressed! debounce check: %llu >= %llu",
                      name, currentTime - state.lastPressTime, m_debounceMs);
        if (currentTime - state.lastPressTime < m_debounceMs) return false;
        state.lastPressTime = currentTime;
        HT_LOG_HOTKEY("Executing %s!", name);
        return true;
    };

    if (tryFire(m_recenterKeyState, "Recenter")) {
        ExecuteRecenter();
        return HotkeyAction::Recenter;
    }
    if (tryFire(m_toggleKeyState, "Toggle")) {
        ExecuteToggle();
        return HotkeyAction::Toggle;
    }
    if (tryFire(m_cycleTrackingModeKeyState, "CycleTrackingMode")) {
        return HotkeyAction::CycleTrackingMode;
    }
    if (tryFire(m_reticleToggleKeyState, "ReticleToggle")) {
        return HotkeyAction::ReticleToggle;
    }
    if (tryFire(m_yawModeKeyState, "ToggleYawMode")) {
        return HotkeyAction::ToggleYawMode;
    }

    return HotkeyAction::None;
}

bool HotkeyHandler::IsKeyDown(int vkCode) const {
    // GetAsyncKeyState returns the state of the key
    // High-order bit set = key is currently down
    // Using GetAsyncKeyState is appropriate here because:
    // 1. We want global key state (even when game window has focus)
    // 2. We're calling from the game's main loop
    // 3. We track press/release state ourselves for edge detection
    SHORT keyState = GetAsyncKeyState(vkCode);
    return (keyState & 0x8000) != 0;
}

bool HotkeyHandler::IsActionDown(int navVK, int chordLetterVK, bool ctrlShiftHeld) const {
    if (ctrlShiftHeld) {
        // Suppressed when Ctrl+Shift is held so the chord path is the sole
        // trigger - prevents a chord-letter keypress from also satisfying any
        // separately-bound nav-cluster check.
        return IsKeyDown(chordLetterVK);
    }
    return IsKeyDown(navVK);
}

void HotkeyHandler::ExecuteRecenter() {
    if (!m_cameraController) {
        return;
    }

    // Get current tracking data for recentering
    if (m_udpReceiver && m_udpReceiver->IsConnected()) {
        const TrackingData& currentData = m_udpReceiver->GetLatestData();
        if (currentData.valid) {
            m_cameraController->Recenter(currentData);
            ShowFeedback("Head tracking recentered");
        } else {
            ShowFeedback("Cannot recenter - no tracking data");
        }
    } else {
        ShowFeedback("Cannot recenter - tracker not connected");
    }
}

void HotkeyHandler::ExecuteToggle() {
    if (!m_cameraController) {
        return;
    }

    bool wasEnabled = m_cameraController->IsEnabled();
    m_cameraController->SetEnabled(!wasEnabled);

    if (wasEnabled) {
        ShowFeedback("Head tracking disabled");
    } else {
        ShowFeedback("Head tracking enabled");
    }
}

void HotkeyHandler::ShowFeedback(const char* message) {
    if (!m_showMessages || !message) {
        return;
    }

    // Print to NVSE console
    if (g_ConsolePrint) {
        g_ConsolePrint("HeadTracking: %s", message);
    }
}

bool HotkeyHandler::SetKeyBinding(int& target, KeyState& state, int vkCode, const char* actionName) {
    // Valid virtual key codes are 0x01 to 0xFE - FAIL FAST on anything else.
    if (vkCode < 0x01 || vkCode > 0xFE) {
        if (g_ConsolePrint) {
            g_ConsolePrint("HeadTracking: ERROR - Invalid %s key code 0x%02X (valid: 0x01-0xFE)", actionName, vkCode);
        }
        return false;
    }

    target = vkCode;
    state = KeyState();  // Reset edge-detect state for the new binding
    return true;
}

bool HotkeyHandler::SetRecenterKey(int vkCode) {
    return SetKeyBinding(m_recenterKeyCode, m_recenterKeyState, vkCode, "recenter");
}

bool HotkeyHandler::SetToggleKey(int vkCode) {
    return SetKeyBinding(m_toggleKeyCode, m_toggleKeyState, vkCode, "toggle");
}

bool HotkeyHandler::SetCycleTrackingModeKey(int vkCode) {
    return SetKeyBinding(m_cycleTrackingModeKeyCode, m_cycleTrackingModeKeyState, vkCode, "cycle tracking mode");
}

bool HotkeyHandler::SetReticleToggleKey(int vkCode) {
    return SetKeyBinding(m_reticleToggleKeyCode, m_reticleToggleKeyState, vkCode, "reticle toggle");
}

bool HotkeyHandler::SetYawModeKey(int vkCode) {
    return SetKeyBinding(m_yawModeKeyCode, m_yawModeKeyState, vkCode, "yaw mode");
}

}  // namespace HeadTracking
