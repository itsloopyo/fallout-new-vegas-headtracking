#pragma once

#include <cstdint>
#include "build_profile.h"

class PlayerCharacter {
public:
    static PlayerCharacter* GetSingleton() {
        return *reinterpret_cast<PlayerCharacter**>(HeadTracking::ActiveProfile().playerBase);
    }
    bool IsThirdPerson() const {
        return *reinterpret_cast<const uint8_t*>(reinterpret_cast<const uint8_t*>(this) + 0x64C) != 0;
    }
    bool IsFirstPerson() const { return !IsThirdPerson(); }
    bool IsInCombat() const {
        return *reinterpret_cast<const uint8_t*>(reinterpret_cast<const uint8_t*>(this) + 0xDF0) != 0;
    }
};

class InterfaceManager {
public:
    static InterfaceManager* GetSingleton() {
        return *reinterpret_cast<InterfaceManager**>(HeadTracking::ActiveProfile().interfaceManager);
    }
    bool IsMenuMode() const {
        return (*reinterpret_cast<const uint8_t*>(reinterpret_cast<const uint8_t*>(this) + 0x0C) & 2) != 0;
    }
    bool IsConsoleOpen() const {
        return *reinterpret_cast<const uint8_t*>(HeadTracking::ActiveProfile().consoleOpen) != 0;
    }
    bool IsMenuVisible(uint32_t menuType) const {
        return reinterpret_cast<const uint8_t*>(HeadTracking::ActiveProfile().menuVisibility)[menuType] != 0;
    }
};

enum MenuType {
    kMenuType_Loading = 1007,
    kMenuType_Dialog = 1009,
    kMenuType_Pipboy = 1002,
    kMenuType_Pause = 1013,
    kMenuType_CharGen = 1048,
    kMenuType_VATS = 1056
};
