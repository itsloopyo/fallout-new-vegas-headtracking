#include "build_profile.h"

#include <Windows.h>

#include <cameraunlock/logging/file_log.h>

namespace HeadTracking {

namespace culog = cameraunlock::logging;

extern const BuildProfile kSteamProfile_20110701;

extern const BuildProfile kGamePassProfile_20160121;

const BuildProfile* const kKnownProfiles[] = {
    &kGamePassProfile_20160121,
    &kSteamProfile_20110701,
};

constexpr size_t kKnownProfileCount = sizeof(kKnownProfiles) / sizeof(kKnownProfiles[0]);

namespace {

struct RunningImage {
    uint32_t timeDateStamp;
    uint32_t sizeOfImage;
    uint32_t checkSum;
};

// Safe at DLL_PROCESS_ATTACH on every build, Steam included: the PE header is
// plaintext even while the SteamStub in .bind has yet to unpack .text.
bool ReadRunningImage(RunningImage* out) {
    auto base = reinterpret_cast<const unsigned char*>(GetModuleHandleW(nullptr));
    if (!base) {
        return false;
    }
    auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        return false;
    }
    auto nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) {
        return false;
    }
    out->timeDateStamp = nt->FileHeader.TimeDateStamp;
    out->sizeOfImage = nt->OptionalHeader.SizeOfImage;
    out->checkSum = nt->OptionalHeader.CheckSum;
    return true;
}

const BuildProfile* g_active = nullptr;

}  // namespace

const BuildProfile* ResolveRunningBuild() {
    RunningImage image = {};
    if (!ReadRunningImage(&image)) {
        return nullptr;
    }
    for (size_t i = 0; i < kKnownProfileCount; ++i) {
        const BuildProfile* profile = kKnownProfiles[i];
        if (profile->timeDateStamp == image.timeDateStamp &&
            profile->sizeOfImage == image.sizeOfImage &&
            profile->checkSum == image.checkSum) {
            g_active = profile;
            return profile;
        }
    }
    return nullptr;
}

const BuildProfile& ActiveProfile() {
    return *g_active;
}

void LogBuildIdentification() {
    RunningImage image = {};
    if (!ReadRunningImage(&image)) {
        culog::Line("ERROR: could not read the running game's PE header - "
                    "head tracking will stay inactive");
        return;
    }

    culog::Line("game build: TimeDateStamp=0x%08X SizeOfImage=0x%08X CheckSum=0x%08X",
                image.timeDateStamp, image.sizeOfImage, image.checkSum);

    if (g_active) {
        culog::Line("game build recognised as %s - supported profile selected", g_active->name);
        return;
    }

    culog::Line("Unsupported game fingerprint; head tracking stays inactive. "
                "Include the build line above when reporting this version.");
}

}  // namespace HeadTracking
