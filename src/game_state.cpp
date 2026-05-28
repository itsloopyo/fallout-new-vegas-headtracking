#include <Windows.h>

#include "game_state.h"
#include "plugin.h"

#include "nvse/GameAPI.h"

namespace HeadTracking {

GameState::GameState()
    : m_currentState(GameStateFlags::None)
    , m_previousState(GameStateFlags::None)
    , m_inputBlockMode(InputBlockMode::AllOverlays)
    , m_trackInThirdPerson(true)
    , m_trackInVATS(false)
    , m_pauseDuringCombat(false)
    , m_initialized(false)
    , m_cachedInterfaceMgr(nullptr)
    , m_cachedCamera(nullptr)
    , m_cachedPlayer(nullptr) {
}

GameState::~GameState() {
}

void GameState::Initialize() {
    if (m_initialized) {
        return;
    }

    m_currentState = GameStateFlags::None;
    m_previousState = GameStateFlags::None;
    m_initialized = true;

    if (g_ConsolePrint) {
        g_ConsolePrint("HeadTracking: Game state detector initialized");
    }
}

void GameState::CacheSingletons() {
    // Cache all singleton pointers at the start of Update()
    // This avoids multiple pointer dereferences per frame
    m_cachedInterfaceMgr = InterfaceManager::GetSingleton();
    m_cachedCamera = PlayerCamera::GetSingleton();
    m_cachedPlayer = PlayerCharacter::GetSingleton();
}

void GameState::Update() {
    if (!m_initialized) {
        return;
    }

    // Cache singleton pointers once per frame (optimization)
    CacheSingletons();

    // Store previous state for change detection
    m_previousState = m_currentState;

    // Build new state from individual checks using cached pointers
    GameStateFlags newState = GameStateFlags::None;

    // Fast path: Check menu mode first (most common state change)
    bool inMenuMode = CheckMenuModeCached();
    if (inMenuMode) {
        newState = newState | GameStateFlags::InMenu;

        // Only check specific menu types if we're in a menu
        // This avoids 6+ GetMenuByType() calls during active gameplay
        if (CheckConsoleOpenCached()) {
            newState = newState | GameStateFlags::InConsole;
        }
        if (IsMenuTypeOpenCached(kMenuType_Dialog)) {
            newState = newState | GameStateFlags::InDialogue;
        }
        if (IsMenuTypeOpenCached(kMenuType_Loading)) {
            newState = newState | GameStateFlags::InLoading;
        }
        if (IsMenuTypeOpenCached(kMenuType_VATS)) {
            newState = newState | GameStateFlags::InVATS;
        }
        if (IsMenuTypeOpenCached(kMenuType_Pipboy)) {
            newState = newState | GameStateFlags::InPipboy;
        }
        if (IsMenuTypeOpenCached(kMenuType_Pause)) {
            newState = newState | GameStateFlags::GamePaused;
        }
        if (IsMenuTypeOpenCached(kMenuType_CharGen)) {
            newState = newState | GameStateFlags::InCharGen;
        }
    }

    // Always check movie (can play without menu mode)
    if (CheckMoviePlaying()) {
        newState = newState | GameStateFlags::InMovie;
    }

    // Check gameplay states using cached player pointer
    if (CheckCombatStateCached()) {
        newState = newState | GameStateFlags::InCombat;
    }

    // Check camera mode using cached camera pointer
    if (m_cachedCamera) {
        if (m_cachedCamera->IsFirstPerson()) {
            newState = newState | GameStateFlags::IsFirstPerson;
        } else if (m_cachedCamera->IsThirdPerson()) {
            newState = newState | GameStateFlags::IsThirdPerson;
        }
    }

    m_currentState = newState;
}

bool GameState::ShouldTrack() const {
    // Never track during loading screens or movies
    if (HasFlag(m_currentState, GameStateFlags::InLoading) ||
        HasFlag(m_currentState, GameStateFlags::InMovie) ||
        HasFlag(m_currentState, GameStateFlags::InCharGen)) {
        return false;
    }

    // Check third-person setting
    if (!m_trackInThirdPerson && HasFlag(m_currentState, GameStateFlags::IsThirdPerson)) {
        return false;
    }

    // Check VATS setting
    if (!m_trackInVATS && HasFlag(m_currentState, GameStateFlags::InVATS)) {
        return false;
    }

    // Check combat pause setting
    if (m_pauseDuringCombat && HasFlag(m_currentState, GameStateFlags::InCombat)) {
        return false;
    }

    // Track during menus if user configured to do so
    // Default behavior: don't track during menus (handled by input block mode for hotkeys)
    // But camera tracking can still work in menus - user might want to look around inventory
    // The key difference is hotkey processing vs camera tracking

    return true;
}

bool GameState::CanProcessInput() const {
    // Never process during loading or character generation
    if (HasFlag(m_currentState, GameStateFlags::InLoading) ||
        HasFlag(m_currentState, GameStateFlags::InCharGen) ||
        HasFlag(m_currentState, GameStateFlags::InMovie)) {
        return false;
    }

    switch (m_inputBlockMode) {
        case InputBlockMode::Never:
            return true;

        case InputBlockMode::MenusOnly:
            return !HasFlag(m_currentState, GameStateFlags::InMenu) &&
                   !HasFlag(m_currentState, GameStateFlags::GamePaused);

        case InputBlockMode::AllDialogue:
            return !HasFlag(m_currentState, GameStateFlags::InMenu) &&
                   !HasFlag(m_currentState, GameStateFlags::InDialogue) &&
                   !HasFlag(m_currentState, GameStateFlags::GamePaused);

        case InputBlockMode::AllOverlays:
            return !HasFlag(m_currentState, GameStateFlags::InMenu) &&
                   !HasFlag(m_currentState, GameStateFlags::InDialogue) &&
                   !HasFlag(m_currentState, GameStateFlags::InConsole) &&
                   !HasFlag(m_currentState, GameStateFlags::InVATS) &&
                   !HasFlag(m_currentState, GameStateFlags::InPipboy) &&
                   !HasFlag(m_currentState, GameStateFlags::GamePaused);
    }

    return false;
}

bool GameState::CheckMenuModeCached() const {
    if (!m_cachedInterfaceMgr) {
        return false;
    }
    return m_cachedInterfaceMgr->IsMenuMode();
}

bool GameState::CheckConsoleOpenCached() const {
    if (!m_cachedInterfaceMgr) {
        return false;
    }
    return m_cachedInterfaceMgr->IsConsoleOpen();
}

bool GameState::IsMenuTypeOpenCached(uint32_t menuType) const {
    if (!m_cachedInterfaceMgr) {
        return false;
    }
    return m_cachedInterfaceMgr->GetMenuByType(menuType) != nullptr;
}

bool GameState::CheckMoviePlaying() const {
    BSWin32MoviePlayer* moviePlayer = BSWin32MoviePlayer::GetSingleton();
    if (!moviePlayer) {
        return false;
    }
    return moviePlayer->IsPlaying();
}

bool GameState::CheckCombatStateCached() const {
    if (!m_cachedPlayer) {
        return false;
    }
    return m_cachedPlayer->IsInCombat();
}

}  // namespace HeadTracking
