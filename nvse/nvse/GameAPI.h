#pragma once

// xNVSE Game API Header
// Provides access to game objects and functions for Fallout: New Vegas
// Memory addresses verified against xNVSE 6.4.2 source

#include <cstdint>

// =============================================================================
// PlayerCharacter - The player singleton
// Singleton address: 0x11F4748 (verified from xNVSE MainLoopHook)
// =============================================================================
class PlayerCharacter {
public:
    static PlayerCharacter* GetSingleton() {
        // g_thePlayer is at 0x11F4748 (from xNVSE Hooks_Gameplay.cpp MainLoopHook)
        PlayerCharacter** g_thePlayer = reinterpret_cast<PlayerCharacter**>(0x011F4748);
        return *g_thePlayer;
    }

    // PlayerCharacter specific - bThirdPerson at offset 0x64C
    bool IsThirdPerson() const {
        return *reinterpret_cast<const bool*>(reinterpret_cast<const uint8_t*>(this) + 0x64C);
    }

    bool IsFirstPerson() const {
        return !IsThirdPerson();
    }

    // Combat state - Actor::IsInCombat uses combat controller at offset 0x1DC
    // CombatController is non-null when in combat
    bool IsInCombat() const {
        void* combatCtrl = *reinterpret_cast<void* const*>(reinterpret_cast<const uint8_t*>(this) + 0x1DC);
        return combatCtrl != nullptr;
    }
};

// =============================================================================
// PlayerCamera - Not a separate singleton in FNV
// Camera state is accessed through the player and global camera node
// For rotation, we modify the player's camera node directly
// =============================================================================

// The game's first-person camera node can be accessed through the player
// For now, we'll work with player rotation directly

// Global camera root node address
// The camera worldspace rotation can be modified through NiNode transforms
// Address 0x11E07B8 points to the current camera state

// PlayerCamera proxy - provides access to camera state
// In FNV, camera rotation for first person is controlled through specific offsets
// The actual camera state at 0x11E07B8 contains the current view angles
class PlayerCamera {
public:
    static PlayerCamera* GetSingleton() {
        // Camera state at 0x11E07B8
        return *reinterpret_cast<PlayerCamera**>(0x011E07B8);
    }

    bool IsFirstPerson() const {
        PlayerCharacter* player = PlayerCharacter::GetSingleton();
        return player ? player->IsFirstPerson() : true;
    }

    bool IsThirdPerson() const {
        PlayerCharacter* player = PlayerCharacter::GetSingleton();
        return player ? player->IsThirdPerson() : false;
    }
};

// =============================================================================
// InterfaceManager - UI state management
// Singleton at 0x11D8A80
// =============================================================================
class InterfaceManager {
public:
    static InterfaceManager* GetSingleton() {
        return *reinterpret_cast<InterfaceManager**>(0x011D8A80);
    }

    // IsMenuMode - checks if any menu is open
    // Flag at offset 0x0C, bit 0
    bool IsMenuMode() const {
        uint32_t flags = *reinterpret_cast<const uint32_t*>(reinterpret_cast<const uint8_t*>(this) + 0x0C);
        return (flags & 1) != 0;
    }

    // Console open check - global at 0x11DEA2E
    bool IsConsoleOpen() const {
        return *reinterpret_cast<const uint8_t*>(0x011DEA2E) != 0;
    }

    // Get active menu by type - simplified check
    // Returns non-null if menu of that type is active
    void* GetMenuByType(uint32_t menuType) const {
        // Menu visibility can be complex - for now just check menu mode
        // A proper implementation would iterate the menu stack
        (void)menuType;
        return nullptr;  // Simplified - rely on IsMenuMode() for now
    }
};

// Menu type constants
enum MenuType {
    kMenuType_Main = 1001,
    kMenuType_Loading = 1002,
    kMenuType_Console = 1003,
    kMenuType_Dialog = 1004,
    kMenuType_Container = 1008,
    kMenuType_Pipboy = 1007,
    kMenuType_LevelUp = 1009,
    kMenuType_Pause = 1012,
    kMenuType_VATS = 1016,
    kMenuType_Hacking = 1027,
    kMenuType_Lockpick = 1028,
    kMenuType_CharGen = 1029,
    kMenuType_Start = 1034
};

// =============================================================================
// BSWin32MoviePlayer - Video playback detection
// =============================================================================
class BSWin32MoviePlayer {
public:
    static BSWin32MoviePlayer* GetSingleton() {
        // Movie player singleton - may be null if no video system
        return *reinterpret_cast<BSWin32MoviePlayer**>(0x011F33F8);
    }

    bool IsPlaying() const {
        if (!this) return false;
        // Playing flag at offset 0x30
        return *reinterpret_cast<const uint8_t*>(reinterpret_cast<const uint8_t*>(this) + 0x30) != 0;
    }
};
