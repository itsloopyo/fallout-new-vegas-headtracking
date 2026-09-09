#include "startup.h"
#include <cstdio>
#include <cstdlib>

static void Check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "%s\n", message);
        std::exit(1);
    }
}

int main() {
    const DWORD pid = GetCurrentProcessId();
    HWND launcher = CreateWindowExW(0, L"STATIC", L"Fallout: New Vegas", WS_OVERLAPPEDWINDOW,
                                   0, 0, 640, 480, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    Check(launcher != nullptr, "create launcher title fixture");
    Check(!HeadTracking::FindGameWindow(pid), "a launcher with the same title must not start game hooks");
    WNDCLASSW wc{};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"Fallout: New Vegas";
    Check(RegisterClassW(&wc) != 0, "register game window fixture");
    HWND game = CreateWindowExW(0, wc.lpszClassName, L"Fallout: New Vegas", WS_OVERLAPPEDWINDOW,
                               0, 0, 640, 480, nullptr, nullptr, wc.hInstance, nullptr);
    Check(game != nullptr, "create game window fixture");
    Check(HeadTracking::FindGameWindow(pid) == game, "find this process's game window");
    Check(!HeadTracking::FindGameWindow(0), "another process's game window must not start hooks");

    void* page = VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    Check(page != nullptr, "allocate code readiness fixture");
    const auto address = reinterpret_cast<uintptr_t>(page);
    Check(!HeadTracking::IsCodeExecutable(address), "mapped data is not executable game code");
    DWORD old = 0;
    Check(VirtualProtect(page, 4096, PAGE_EXECUTE_READ, &old) != 0, "make code fixture executable");
    Check(HeadTracking::IsCodeExecutable(address), "accept executable code");
    Check(VirtualProtect(page, 4096, PAGE_EXECUTE_READ | PAGE_GUARD, &old) != 0, "guard code fixture");
    Check(!HeadTracking::IsCodeExecutable(address), "guarded code is not ready to hook");
    Check(VirtualFree(page, 0, MEM_RELEASE) != 0, "release code fixture");
    Check(!HeadTracking::IsCodeExecutable(address), "unmapped code is not ready to hook");
    DestroyWindow(game);
    DestroyWindow(launcher);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    std::puts("Startup tests passed");
}
