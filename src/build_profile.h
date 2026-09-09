#pragma once
#include <cstdint>
namespace HeadTracking {
struct BuildProfile {
    const char* name;
    uint32_t timeDateStamp;
    uint32_t sizeOfImage;
    uint32_t checkSum;
    uintptr_t playerBase;
    uintptr_t sceneGraphBase;
    uintptr_t interfaceManager;
    uintptr_t calcCullingPlanes;
    uintptr_t tileSetFloatValue;
    uintptr_t consoleOpen;
    uintptr_t menuVisibility;
    uintptr_t defaultWorldFov;
    uintptr_t hudMainMenu;
    uintptr_t tesWorld;
    uintptr_t castRay;
    uintptr_t gameMain;
    uintptr_t renderAccumulator;
    uintptr_t setupSkyGeometry;
    uintptr_t currentRenderPass;
    uintptr_t currentAccumulator;
    uintptr_t setCameraFov;
    uintptr_t updateCameraProjection;
};
const BuildProfile* ResolveRunningBuild();
const BuildProfile& ActiveProfile();
void LogBuildIdentification();
}
