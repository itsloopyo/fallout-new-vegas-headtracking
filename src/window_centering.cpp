#include "window_centering.h"
#include "startup.h"

#include <cameraunlock/logging/file_log.h>
#include <atomic>

namespace HeadTracking {
namespace culog = cameraunlock::logging;
static std::atomic<bool> s_centered{false};

void CenterWindowOnce(IDirect3DDevice9* device) {
    if (s_centered.load()) return;
    D3DDEVICE_CREATION_PARAMETERS params{};
    HRESULT hr = device->GetCreationParameters(&params);
    if (FAILED(hr)) {
        if (!s_centered.exchange(true))
            culog::Line("ERROR: window GetCreationParameters failed: 0x%08lX", hr);
        return;
    }
    HWND hwnd = GetAncestor(params.hFocusWindow, GA_ROOT);
    if (!hwnd || hwnd != FindGameWindow(GetCurrentProcessId())) return;
    if (s_centered.exchange(true)) return;

    IDirect3DSwapChain9* swap = nullptr;
    hr = device->GetSwapChain(0, &swap);
    if (FAILED(hr)) {
        culog::Line("ERROR: window GetSwapChain failed: 0x%08lX", hr);
        return;
    }
    D3DPRESENT_PARAMETERS pp{};
    hr = swap->GetPresentParameters(&pp);
    swap->Release();
    if (FAILED(hr)) {
        culog::Line("ERROR: window GetPresentParameters failed: 0x%08lX", hr);
        return;
    }
    if (!pp.Windowed) {
        culog::Line("Window: fullscreen, centring skipped");
        return;
    }
    const LONG bbW = static_cast<LONG>(pp.BackBufferWidth);
    const LONG bbH = static_cast<LONG>(pp.BackBufferHeight);
    HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(monitor, &info)) {
        culog::Line("ERROR: window GetMonitorInfo failed: %lu", GetLastError());
        return;
    }
    const RECT& work = info.rcWork;
    const LONG workW = work.right - work.left;
    const LONG workH = work.bottom - work.top;
    const DWORD style = static_cast<DWORD>(GetWindowLongW(hwnd, GWL_STYLE));
    const DWORD exStyle = static_cast<DWORD>(GetWindowLongW(hwnd, GWL_EXSTYLE));
    RECT desired{0, 0, bbW, bbH};
    if (!AdjustWindowRectEx(&desired, style, FALSE, exStyle)) {
        culog::Line("ERROR: window AdjustWindowRectEx failed: %lu", GetLastError());
        return;
    }
    const LONG winW = (desired.right - desired.left < workW) ? desired.right - desired.left : workW;
    const LONG winH = (desired.bottom - desired.top < workH) ? desired.bottom - desired.top : workH;
    const LONG newX = work.left + (workW - winW) / 2;
    const LONG newY = work.top + (workH - winH) / 2;
    if (!SetWindowPos(hwnd, nullptr, newX, newY, winW, winH, SWP_NOZORDER | SWP_NOACTIVATE)) {
        culog::Line("ERROR: window SetWindowPos failed: %lu", GetLastError());
        return;
    }
    culog::Line("Window centred: process=%lu window=%p position=(%ld,%ld) size=%ldx%ld work=(%ld,%ld,%ld,%ld)",
                GetCurrentProcessId(), hwnd, newX, newY, winW, winH,
                work.left, work.top, work.right, work.bottom);
}
}
