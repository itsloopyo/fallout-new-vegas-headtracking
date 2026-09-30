#pragma once

#include "build_profile.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace HeadTracking {

struct RuntimeLayout {
    uint32_t playerThirdPerson = 0;
    uint32_t playerCombat = 0;
    uint32_t sceneCamera = 0;
    uint32_t cameraTransform = 0;
    uint32_t cameraPosition = 0;
    uint32_t cameraFrustum = 0;
    uint32_t uiMode = 0;
    uint32_t uiGameplayMode = 0;
    uint32_t menuLoading = 0;
    uint32_t menuDialog = 0;
    uint32_t menuPipboy = 0;
    uint32_t menuPipboyStats = 0;
    uint32_t menuPipboyData = 0;
    uint32_t menuPause = 0;
    uint32_t menuCharGen = 0;
    uint32_t menuVats = 0;
    uint32_t tileVisible = 0;
    uint32_t accumulatorCamera = 0;
    uint32_t mainWeaponCamera = 0;
    uint32_t hudCrosshair = 0;
    uint32_t playerFirstPersonRoot = 0;
    uint32_t nodeLocalPosition = 0;
    uint32_t playerCell = 0;
    uint32_t playerProcess = 0;
    uint32_t processAds = 0;
    uint32_t processController = 0;
    uint32_t controllerPhantom = 0;
    uint32_t phantomObject = 0;
    uint32_t objectFilter = 0;
    uint32_t layerCamera = 0;
    uint32_t layerAim = 0;
    uint32_t raySize = 0;
    uint32_t rayStart = 0;
    uint32_t rayEnd = 0;
    uint32_t rayFilter = 0;
    uint32_t rayFraction = 0;
    uint32_t rayShapeKey = 0;
    uint32_t rayRootShapeKey = 0;
    float worldToHavok = 0;
    uint32_t playerRotation = 0;
    uint32_t collisionGroupMask = 0;
    uint32_t nodeParent = 0;
    uint32_t nodeWorldScale = 0;
};

struct RuntimeBindings {
    BuildProfile profile{};
    RuntimeLayout layout{};
    std::vector<uint32_t> playerVtables;
    uint32_t sceneVtable = 0;
    uint32_t cameraVtable = 0;
    uint32_t hudVtable = 0;
    uint32_t processVtable = 0;
    std::vector<uint32_t> tileVtables;
    std::vector<uint32_t> nodeVtables;
    std::vector<uint32_t> geometryVtables;
    uint32_t skyVtable = 0;
    uint32_t accumulatorVtable = 0;
    uint32_t rigTranslationCaller = 0;
};

bool DiscoverRuntime(const uint8_t* mappedImage, size_t size, uint32_t imageBase,
                     RuntimeBindings& output, std::string& diagnostic);
const BuildProfile* ResolveMappedImage(const uint8_t* image, size_t size, uint32_t imageBase);
const RuntimeBindings& ActiveRuntime();
inline const RuntimeLayout& ActiveLayout() { return ActiveRuntime().layout; }
const std::string& DiscoveryDiagnostic();
bool RuntimeObject(const void* object, size_t size, uint32_t vtable, const char* identity, bool restoring = false);
bool RuntimeObject(const void* object, size_t size, const std::vector<uint32_t>& vtables, const char* identity);
bool RuntimeValidationFailed();
void RejectRuntimeContract(const char* reason);
uint8_t* RuntimePlayer();
uint8_t* RuntimeCamera(bool restoring = false);

}
