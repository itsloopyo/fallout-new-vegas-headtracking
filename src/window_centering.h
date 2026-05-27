#pragma once

#include <Windows.h>
#include <d3d9.h>

namespace HeadTracking {

// Resize the window to the device's backbuffer size (if it's currently
// larger) and center it on its current monitor's work area. Runs at most
// once per process; subsequent calls are no-ops so the user can drag or
// resize the window afterwards without us yanking it back.
void CenterWindowOnce(IDirect3DDevice9* device);

}  // namespace HeadTracking
