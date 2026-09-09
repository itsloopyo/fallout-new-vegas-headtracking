#pragma once

#include <Windows.h>
#include <cstdint>
#include <cwchar>

namespace HeadTracking {

inline HWND FindGameWindow(DWORD processId) {
    struct Search { DWORD processId; HWND window; } search{processId, nullptr};
    EnumWindows([](HWND window, LPARAM param) -> BOOL {
        auto& state = *reinterpret_cast<Search*>(param);
        DWORD owner = 0;
        GetWindowThreadProcessId(window, &owner);
        if (owner != state.processId) return TRUE;
        wchar_t name[64]{};
        GetClassNameW(window, name, 64);
        if (wcscmp(name, L"Fallout: New Vegas") != 0) return TRUE;
        state.window = window;
        return FALSE;
    }, reinterpret_cast<LPARAM>(&search));
    return search.window;
}

inline bool IsCodeExecutable(uintptr_t address) {
    MEMORY_BASIC_INFORMATION memory{};
    return VirtualQuery(reinterpret_cast<void*>(address), &memory, sizeof(memory)) &&
        memory.State == MEM_COMMIT && !(memory.Protect & PAGE_GUARD) &&
        (memory.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ |
                           PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY));
}

}
