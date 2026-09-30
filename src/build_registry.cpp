#include "build_profile.h"
#include "runtime_discovery.h"

#include <Windows.h>
#include <cameraunlock/logging/file_log.h>
#include <algorithm>
#include <cstring>
#include <vector>

namespace HeadTracking {
extern const BuildProfile kSteamProfile_20110701;
extern const BuildProfile kGamePassProfile_20160121;
namespace {
RuntimeBindings g_runtime;
const BuildProfile* g_active = nullptr;
std::string g_diagnostic;
bool g_liveFailure = false;

bool ReadMemory(const void* address, void* output, size_t size) {
    SIZE_T read = 0;
    return ReadProcessMemory(GetCurrentProcess(), address, output, size, &read) && read == size;
}

bool CrossCheck(const BuildProfile& measured, uint32_t base) {
    for (const auto* known : {&kSteamProfile_20110701, &kGamePassProfile_20160121}) {
        if (measured.timeDateStamp != known->timeDateStamp || measured.sizeOfImage != known->sizeOfImage ||
            measured.checkSum != known->checkSum) continue;
#define CHECK(member) \
        if (static_cast<int64_t>(measured.member) - base != static_cast<int64_t>(known->member) - 0x400000) { \
            g_diagnostic = std::string(known->name) + ": discovery disagrees with historical " #member; return false; \
        }
        CHECK(playerBase); CHECK(sceneGraphBase); CHECK(interfaceManager); CHECK(calcCullingPlanes);
        CHECK(tileSetFloatValue); CHECK(consoleOpen); CHECK(menuVisibility); CHECK(defaultWorldFov);
        CHECK(hudMainMenu); CHECK(tesWorld); CHECK(castRay); CHECK(gameMain); CHECK(renderAccumulator);
        CHECK(setupSkyGeometry); CHECK(currentRenderPass); CHECK(currentAccumulator); CHECK(setCameraFov);
        CHECK(updateCameraProjection); CHECK(updateFirstPerson);
#undef CHECK
    }
    return true;
}
}

const BuildProfile* ResolveMappedImage(const uint8_t* image, size_t size, uint32_t imageBase) {
    g_active = nullptr;
    g_runtime = {};
    g_diagnostic.clear();
    g_liveFailure = false;
    RuntimeBindings resolved;
    if (!DiscoverRuntime(image, size, imageBase, resolved, g_diagnostic) || !CrossCheck(resolved.profile, imageBase)) return nullptr;
    g_runtime = std::move(resolved);
    g_active = &g_runtime.profile;
    return g_active;
}

const BuildProfile* ResolveRunningBuild() {
    g_liveFailure = false;
    g_active = nullptr;
    g_runtime = {};
    g_diagnostic.clear();
    const auto* base = reinterpret_cast<const uint8_t*>(GetModuleHandleW(nullptr));
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS32 nt{};
    if (!ReadMemory(base, &dos, sizeof(dos)) || dos.e_magic != IMAGE_DOS_SIGNATURE ||
        dos.e_lfanew < static_cast<LONG>(sizeof(dos)) || dos.e_lfanew > 0x100000 ||
        !ReadMemory(base + dos.e_lfanew, &nt, sizeof(nt)) || nt.Signature != IMAGE_NT_SIGNATURE ||
        nt.FileHeader.Machine != IMAGE_FILE_MACHINE_I386 || nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC ||
        nt.OptionalHeader.SizeOfImage < 4096 || nt.OptionalHeader.SizeOfImage > 512u * 1024 * 1024 ||
        static_cast<uint64_t>(reinterpret_cast<uintptr_t>(base)) + nt.OptionalHeader.SizeOfImage > UINT32_MAX) {
        g_diagnostic = "invalid running PE32 image header";
        return nullptr;
    }
    std::vector<uint8_t> snapshot(nt.OptionalHeader.SizeOfImage);
    size_t offset = 0;
    while (offset < snapshot.size()) {
        MEMORY_BASIC_INFORMATION memory{};
        if (!VirtualQuery(base + offset, &memory, sizeof(memory)) || memory.AllocationBase != base) {
            g_diagnostic = "running image allocation does not cover its PE extent";
            return nullptr;
        }
        const size_t count = std::min<size_t>(snapshot.size() - offset, memory.RegionSize);
        if (memory.State == MEM_COMMIT && !(memory.Protect & (PAGE_NOACCESS | PAGE_GUARD)) &&
            !ReadMemory(base + offset, snapshot.data() + offset, count)) {
            g_diagnostic = "running image changed while reading discovery snapshot";
            return nullptr;
        }
        offset += count;
    }
    return ResolveMappedImage(snapshot.data(), snapshot.size(), static_cast<uint32_t>(reinterpret_cast<uintptr_t>(base)));
}

const BuildProfile& ActiveProfile() { return *g_active; }
const RuntimeBindings& ActiveRuntime() { return g_runtime; }
const std::string& DiscoveryDiagnostic() { return g_diagnostic; }

bool RuntimeValidationFailed() { return g_liveFailure; }
void RejectRuntimeContract(const char* reason) {
    if (!g_liveFailure) cameraunlock::logging::Line("Runtime contract rejected: %s; dependent writes disabled",reason);
    g_liveFailure = true;
}

bool RuntimeObject(const void* object, size_t size, uint32_t vtable, const char* identity, bool restoring) {
    if (!object || (g_liveFailure && !restoring)) return false;
    const uintptr_t address = reinterpret_cast<uintptr_t>(object);
    bool valid = address % 4 == 0 && size >= 4 && size <= UINT32_MAX - address;
    size_t offset = 0;
    while (valid && offset < size) {
        MEMORY_BASIC_INFORMATION memory{};
        valid = VirtualQuery(reinterpret_cast<const void*>(address + offset), &memory, sizeof(memory)) &&
            memory.State == MEM_COMMIT && !(memory.Protect & (PAGE_NOACCESS | PAGE_GUARD)) &&
            (memory.Protect & (PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY));
        if (valid) {
            const size_t available = memory.RegionSize - (address + offset - reinterpret_cast<uintptr_t>(memory.BaseAddress));
            offset += std::min<size_t>(size - offset, available);
        }
    }
    if (valid && vtable) valid = *reinterpret_cast<const uint32_t*>(object) == vtable;
    if (!valid) {
        g_liveFailure = true;
        cameraunlock::logging::Line("Runtime object validation failed: %s at %p; dependent writes disabled", identity, object);
    }
    return valid;
}

uint8_t* RuntimePlayer() {
    auto* player = *reinterpret_cast<uint8_t**>(ActiveProfile().playerBase);
    const auto& l = ActiveLayout();
    const size_t extent = std::max({l.playerCombat + 1, l.playerFirstPersonRoot + 4, l.playerProcess + 4,
                                  l.playerCell + 4, l.playerRotation + 12, l.playerThirdPerson + 1});
    return RuntimeObject(player, extent, ActiveRuntime().playerVtables.front(), "PlayerCharacter") ? player : nullptr;
}

bool RuntimeObject(const void* object, size_t size, const std::vector<uint32_t>& vtables, const char* identity) {
    if (!RuntimeObject(object,size,0,identity)) return false;
    const auto table = *reinterpret_cast<const uint32_t*>(object);
    if (std::find(vtables.begin(),vtables.end(),table) != vtables.end()) return true;
    return RuntimeObject(object,size,vtables.front(),identity);
}

uint8_t* RuntimeCamera(bool restoring) {
    auto* scene = *reinterpret_cast<uint8_t**>(ActiveProfile().sceneGraphBase);
    const auto& l = ActiveLayout();
    if (!RuntimeObject(scene, l.sceneCamera + 4, ActiveRuntime().sceneVtable, "SceneGraph", restoring)) return nullptr;
    auto* camera = *reinterpret_cast<uint8_t**>(scene + l.sceneCamera);
    return RuntimeObject(camera, l.cameraFrustum + 28, ActiveRuntime().cameraVtable, "world NiCamera", restoring) ? camera : nullptr;
}

void LogBuildIdentification() {
    if (!g_active) {
        cameraunlock::logging::Line("Runtime discovery rejected: %s; head tracking stays inactive", g_diagnostic.c_str());
        return;
    }
    cameraunlock::logging::Line("Runtime discovery validated: TimeDateStamp=0x%08X SizeOfImage=0x%08X CheckSum=0x%08X",
        g_active->timeDateStamp, g_active->sizeOfImage, g_active->checkSum);
    cameraunlock::logging::Line("Resolved culling=%p FOV=%p projection=%p weapon=%p sky=%p rig=%p",
        reinterpret_cast<void*>(g_active->calcCullingPlanes), reinterpret_cast<void*>(g_active->setCameraFov),
        reinterpret_cast<void*>(g_active->updateCameraProjection), reinterpret_cast<void*>(g_active->renderAccumulator),
        reinterpret_cast<void*>(g_active->setupSkyGeometry), reinterpret_cast<void*>(g_active->updateFirstPerson));
}
}
