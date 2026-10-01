#pragma once

#include <Windows.h>

namespace HeadTracking {
namespace Proxy {

// Called from DllMain on DLL_PROCESS_ATTACH. No-op unless this DLL was loaded
// under the proxy's filename.
void OnProcessAttach(HMODULE self);

// True once the proxy has identified the build, initialised the plugin and
// handed per-frame work to the render hook. False in the launcher process, on
// an unrecognised build, and during startup.
bool DriverArmed();

// One call per rendered frame, from the D3D9 Present hook. No-op until the
// driver is armed.
void OnFrame();

}  // namespace Proxy
}  // namespace HeadTracking
