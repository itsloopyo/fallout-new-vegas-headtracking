#pragma once

#include <Windows.h>

#include <cstdint>

// Forward declarations (global namespace - defined in nvse/GameAPI.h)
class InterfaceManager;
class PlayerCamera;
class PlayerCharacter;

namespace HeadTracking {

// Game state flags indicating what the player is currently doing
enum class GameStateFlags : uint32_t {
    None           = 0,
    InMenu         = 1 << 0,   // Any menu is open (pause, inventory, etc.)
    InDialogue     = 1 << 1,   // Dialogue menu is open
    InConsole      = 1 << 2,   // Developer console is open
    InLoading      = 1 << 3,   // Loading screen is active
    InVATS         = 1 << 4,   // VATS targeting is active
    InPipboy       = 1 << 5,   // Pip-Boy menu is open
    InCombat       = 1 << 6,   // Player is in combat
    IsFirstPerson  = 1 << 7,   // First-person camera mode
    IsThirdPerson  = 1 << 8,   // Third-person camera mode
    GamePaused     = 1 << 9,   // Game is paused (pause menu)
    InCharGen      = 1 << 10,  // Character generation active
    InMovie        = 1 << 11   // Video/movie is playing
};

// Bitwise operators for GameStateFlags
inline GameStateFlags operator|(GameStateFlags a, GameStateFlags b) {
    return static_cast<GameStateFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline GameStateFlags operator&(GameStateFlags a, GameStateFlags b) {
    return static_cast<GameStateFlags>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

inline bool HasFlag(GameStateFlags flags, GameStateFlags flag) {
    return (static_cast<uint32_t>(flags) & static_cast<uint32_t>(flag)) != 0;
}

// Input blocking modes - determines when head tracking is paused
enum class InputBlockMode {
    Never,         // Never block (always track, even in menus)
    MenusOnly,     // Block during menus only
    AllDialogue,   // Block during menus and dialogue
    AllOverlays    // Block during any overlay (menus, console, VATS, etc.)
};

class GameState {
public:
    GameState();
    ~GameState();

    // Disable copying
    GameState(const GameState&) = delete;
    GameState& operator=(const GameState&) = delete;
    GameState(GameState&&) = delete;
    GameState& operator=(GameState&&) = delete;

    // Initialize game state detection
    void Initialize();

    // Update game state - call every frame
    // Queries game singletons to determine current state
    void Update();

    // Get current combined state flags
    GameStateFlags GetCurrentState() const { return m_currentState; }

    // Check if loading screen just finished (was loading, now not loading)
    bool JustFinishedLoading() const {
        return HasFlag(m_previousState, GameStateFlags::InLoading) &&
               !HasFlag(m_currentState, GameStateFlags::InLoading);
    }

    // Check if head tracking should be active based on current state
    // Returns true if tracking should be applied to camera
    bool ShouldTrack() const;

    // Check if input (hotkeys) should be processed based on current state
    // Returns true if hotkeys should be processed
    bool CanProcessInput() const;

    // Configure blocking mode
    void SetInputBlockMode(InputBlockMode mode) { m_inputBlockMode = mode; }

    // Configure whether tracking works in third person
    void SetTrackInThirdPerson(bool track) { m_trackInThirdPerson = track; }

    // Configure whether tracking works in VATS
    void SetTrackInVATS(bool track) { m_trackInVATS = track; }

    // Configure whether tracking pauses during combat
    void SetPauseDuringCombat(bool pause) { m_pauseDuringCombat = pause; }

private:
    // Helper to check if a specific menu type is open using cached pointers
    bool IsMenuTypeOpenCached(uint32_t menuType) const;

    // Check specific game states using cached pointers
    bool CheckMenuModeCached() const;
    bool CheckConsoleOpenCached() const;
    bool CheckMoviePlaying() const;
    bool CheckCombatStateCached() const;

    // Cache singleton pointers at start of Update() to avoid multiple lookups
    void CacheSingletons();

    // Current state flags
    GameStateFlags m_currentState;

    // Previous state for change detection
    GameStateFlags m_previousState;

    // Configuration
    InputBlockMode m_inputBlockMode;
    bool m_trackInThirdPerson;
    bool m_trackInVATS;
    bool m_pauseDuringCombat;

    // Initialization state
    bool m_initialized;

    // Performance optimization: cached singleton pointers per-frame
    // These are refreshed at the start of each Update() call
    ::InterfaceManager* m_cachedInterfaceMgr;
    ::PlayerCamera* m_cachedCamera;
    ::PlayerCharacter* m_cachedPlayer;
};

}  // namespace HeadTracking
