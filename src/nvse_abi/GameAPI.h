#pragma once

#include <cstdint>
#include "build_profile.h"
#include "runtime_discovery.h"

class PlayerCharacter {
public:
    static PlayerCharacter* GetSingleton() {
        return reinterpret_cast<PlayerCharacter*>(HeadTracking::RuntimePlayer());
    }
    bool IsThirdPerson() const {
        return *reinterpret_cast<const uint8_t*>(reinterpret_cast<const uint8_t*>(this) + HeadTracking::ActiveLayout().playerThirdPerson) != 0;
    }
    bool IsFirstPerson() const { return !IsThirdPerson(); }
    bool IsInCombat() const {
        return *reinterpret_cast<const uint8_t*>(reinterpret_cast<const uint8_t*>(this) + HeadTracking::ActiveLayout().playerCombat) != 0;
    }
};

class InterfaceManager {
public:
    static InterfaceManager* GetSingleton() {
        auto* object = *reinterpret_cast<InterfaceManager**>(HeadTracking::ActiveProfile().interfaceManager);
        return HeadTracking::RuntimeObject(object, HeadTracking::ActiveLayout().uiMode + 4, 0, "InterfaceManager") ? object : nullptr;
    }
    bool IsMenuMode() const {
        return *reinterpret_cast<const uint32_t*>(reinterpret_cast<const uint8_t*>(this) + HeadTracking::ActiveLayout().uiMode) != HeadTracking::ActiveLayout().uiGameplayMode;
    }
    bool IsConsoleOpen() const {
        return *reinterpret_cast<const uint8_t*>(HeadTracking::ActiveProfile().consoleOpen) != 0;
    }
    bool IsMenuVisible(uint32_t menuType) const {
        return reinterpret_cast<const uint8_t*>(HeadTracking::ActiveProfile().menuVisibility)[menuType] != 0;
    }
};
