#include "window_centering.h"

#include "debug_log.h"

#include <atomic>

namespace HeadTracking {

static std::atomic<bool> s_centered{false};

void CenterWindowOnce(IDirect3DDevice9* device) {
    if (!device) {
        return;
    }
    bool expected = false;
    if (!s_centered.compare_exchange_strong(expected, true)) {
        return;
    }

    IDirect3DSwapChain9* swap = nullptr;
    if (FAILED(device->GetSwapChain(0, &swap)) || !swap) {
        HT_LOG_PLUGIN("window: GetSwapChain failed");
        return;
    }
    D3DPRESENT_PARAMETERS pp{};
    HRESULT hr = swap->GetPresentParameters(&pp);
    swap->Release();
    if (FAILED(hr)) {
        HT_LOG_PLUGIN("window: GetPresentParameters failed");
        return;
    }

    HWND hwnd = pp.hDeviceWindow;
    if (!hwnd) {
        D3DDEVICE_CREATION_PARAMETERS params{};
        if (SUCCEEDED(device->GetCreationParameters(&params))) {
            hwnd = params.hFocusWindow;
        }
    }
    if (!hwnd) {
        HT_LOG_PLUGIN("window: no usable HWND from device");
        return;
    }
    HWND root = GetAncestor(hwnd, GA_ROOT);
    if (root) {
        hwnd = root;
    }
    HT_LOG_PLUGIN("window: target hwnd=%p (root=%p)", hwnd, root);
    LONG bbW = static_cast<LONG>(pp.BackBufferWidth);
    LONG bbH = static_cast<LONG>(pp.BackBufferHeight);
    if (bbW <= 0 || bbH <= 0) {
        HT_LOG_PLUGIN("window: invalid backbuffer size %ldx%ld", bbW, bbH);
        return;
    }

    HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(monitor, &info)) {
        HT_LOG_PLUGIN("window: GetMonitorInfoW failed");
        return;
    }
    const RECT& work = info.rcWork;
    LONG workW = work.right - work.left;
    LONG workH = work.bottom - work.top;

    DWORD style = static_cast<DWORD>(GetWindowLongW(hwnd, GWL_STYLE));
    DWORD exStyle = static_cast<DWORD>(GetWindowLongW(hwnd, GWL_EXSTYLE));
    RECT desired{0, 0, bbW, bbH};
    if (!AdjustWindowRectEx(&desired, style, FALSE, exStyle)) {
        HT_LOG_PLUGIN("window: AdjustWindowRectEx failed");
        return;
    }
    LONG winW = desired.right - desired.left;
    LONG winH = desired.bottom - desired.top;

    if (winW > workW) winW = workW;
    if (winH > workH) winH = workH;

    LONG newX = work.left + (workW - winW) / 2;
    LONG newY = work.top + (workH - winH) / 2;
    if (!SetWindowPos(hwnd, HWND_TOP, newX, newY, winW, winH,
                      SWP_NOZORDER | SWP_NOACTIVATE)) {
        HT_LOG_PLUGIN("window: SetWindowPos failed (err=%lu)", GetLastError());
        return;
    }
    HT_LOG_PLUGIN("window: resized+centered to %ldx%ld at (%ld, %ld) on work area %ldx%ld (backbuffer %ldx%ld)",
                  winW, winH, newX, newY, workW, workH, bbW, bbH);
}

}  // namespace HeadTracking
