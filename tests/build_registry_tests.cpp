#include "build_profile.h"
#include <cameraunlock/logging/file_log.h>
#include <Windows.h>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace {
std::string logText;

void Check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "%s\n", message);
        std::exit(1);
    }
}
}

namespace cameraunlock::logging {
void Line(const char* format, ...) {
    char line[1024];
    va_list args;
    va_start(args, format);
    const int length = std::vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    Check(length >= 0 && static_cast<size_t>(length) < sizeof(line), "registry diagnostic fits capture");
    logText.append(line, static_cast<size_t>(length));
    logText += '\n';
}
}

int main() {
    using namespace HeadTracking;
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(GetModuleHandleW(nullptr));
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS32*>(reinterpret_cast<unsigned char*>(dos) + dos->e_lfanew);
    const auto original = *nt;
    DWORD protection;
    Check(VirtualProtect(nt, sizeof(*nt), PAGE_READWRITE, &protection) != 0,
          "make this test process's PE header writable");

    const uint32_t fingerprints[][3] = {
        {0x4E0D50ED, 0x0107B000, 0x00FCB4FE},
        {0x56A157BC, 0x01006000, 0x00F5CA94}
    };
    const char* names[] = {"steam-win32-20110701", "gamepass-win32-20160121"};
    for (size_t i = 0; i < 2; ++i) {
        for (size_t field = 0; field < 3; ++field) {
            nt->FileHeader.TimeDateStamp = fingerprints[i][0];
            nt->OptionalHeader.SizeOfImage = fingerprints[i][1];
            nt->OptionalHeader.CheckSum = fingerprints[i][2];
            const auto* selected = ResolveRunningBuild();
            Check(selected && selected->name == std::string(names[i]), "select exact historical fingerprint");
            Check(&ActiveProfile() == selected, "publish selected profile");

            if (field == 0) ++nt->FileHeader.TimeDateStamp;
            if (field == 1) ++nt->OptionalHeader.SizeOfImage;
            if (field == 2) ++nt->OptionalHeader.CheckSum;
            Check(ResolveRunningBuild() == nullptr, "reject an unlisted fingerprint");
            logText.clear();
            LogBuildIdentification();
            Check(logText.find("Unsupported game fingerprint") != std::string::npos,
                  "a rejected selection must clear the previously active profile");
            Check(logText.find("supported profile selected") == std::string::npos,
                  "a rejected selection must not report a stale match");
        }
        nt->FileHeader.TimeDateStamp = fingerprints[i][0];
        nt->OptionalHeader.SizeOfImage = fingerprints[i][1];
        nt->OptionalHeader.CheckSum = fingerprints[i][2];
        Check(ResolveRunningBuild() != nullptr, "reselect after rejection");
        nt->Signature = 0;
        Check(ResolveRunningBuild() == nullptr, "reject invalid PE signature");
        nt->Signature = original.Signature;
        logText.clear();
        LogBuildIdentification();
        Check(logText.find("Unsupported game fingerprint") != std::string::npos,
              "a header read failure must clear the previously active profile");
    }
    *nt = original;
    DWORD ignored;
    Check(VirtualProtect(nt, sizeof(*nt), protection, &ignored) != 0, "restore PE header protection");
    std::puts("Build registry tests passed");
}
