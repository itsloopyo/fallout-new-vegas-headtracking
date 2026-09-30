#include "runtime_discovery.h"
#include <cameraunlock/logging/file_log.h>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace {
std::string logText;
HeadTracking::RuntimeBindings fixture;
bool valid = true;
void Check(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}
}
namespace cameraunlock::logging {
void Line(const char* format, ...) {
    char line[1024];
    va_list args;
    va_start(args, format);
    std::vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    logText += line;
}
}
namespace HeadTracking {
extern const BuildProfile kSteamProfile_20110701;
extern const BuildProfile kGamePassProfile_20160121;
bool DiscoverRuntime(const uint8_t*, size_t, uint32_t, RuntimeBindings& output, std::string& diagnostic) {
    output = {};
    if (!valid) { diagnostic = "missing fixture anchor"; return false; }
    output = fixture;
    return true;
}
}
int main() {
    using namespace HeadTracking;
    for (const auto* known : {&kSteamProfile_20110701, &kGamePassProfile_20160121}) {
        fixture = {};
        fixture.profile = *known;
        fixture.layout.playerThirdPerson = 0x28;
        fixture.playerVtables = {0x500010};
        valid = true;
        Check(ResolveMappedImage(nullptr, 0, 0x400000) != nullptr, "cross-check known discovered addresses");
        Check(ActiveLayout().playerThirdPerson == 0x28, "publish discovered fields");
        alignas(16) uint32_t object[16]{};
        object[0] = 0x12345678;
        Check(RuntimeObject(object,sizeof(object),object[0],"fixture object"),"validate live readable owner");
        Check(!RuntimeObject(object,sizeof(object),0x11223344,"wrong fixture owner") && RuntimeValidationFailed(),"reject live vtable mismatch");
        Check(!RuntimeObject(object,sizeof(object),object[0],"disabled fixture"),"live rejection remains sticky");
        Check(RuntimeObject(object,sizeof(object),object[0],"restoration owner",true),"validate safe restoration after another owner fails");
        Check(!RuntimeObject(object,sizeof(object),0x11223344,"invalid restoration owner",true),"restoration cannot bypass object identity");
        Check(ResolveMappedImage(nullptr,0,0x400000) && !RuntimeValidationFailed(),"fresh selection resets live rejection");
        fixture.profile.calcCullingPlanes += 16;
        Check(!ResolveMappedImage(nullptr, 0, 0x400000), "reject a historical disagreement");
        Check(DiscoveryDiagnostic().find("calcCullingPlanes") != std::string::npos, "name disagreement");
        Check(ActiveRuntime().profile.playerBase == 0 && ActiveRuntime().playerVtables.empty(), "clear rejected historical result");
        ++fixture.profile.timeDateStamp;
        Check(ResolveMappedImage(nullptr, 0, 0x400000) != nullptr, "unlisted fingerprint uses discovered result");
        Check(ActiveProfile().calcCullingPlanes == fixture.profile.calcCullingPlanes, "never substitute historical address");
        valid = false;
        Check(!ResolveMappedImage(nullptr, 0, 0x400000), "reject discovery after successful selection");
        Check(ActiveRuntime().profile.playerBase == 0 && ActiveLayout().playerThirdPerson == 0 && ActiveRuntime().playerVtables.empty(), "no stale bindings after rejection");
        logText.clear();
        LogBuildIdentification();
        Check(logText.find("missing fixture anchor") != std::string::npos && logText.find("validated") == std::string::npos, "retain rejection diagnostic");
        fixture.profile = *known;
        Check(!ResolveMappedImage(nullptr, 0, 0x400000), "known fingerprint cannot bypass failed discovery");
    }
    std::puts("Build registry selection contracts passed");
}
