#pragma once

#include <Windows.h>
#include <d3d9.h>
#include <cstdint>

namespace HeadTracking {

// Forward declarations
class CameraController;
class UdpReceiver;

// D3D9 Hook for intercepting EndScene to modify camera right before frame presents
// This runs after all game rendering setup, giving us a chance to modify the view
class D3D9Hook {
public:
    static D3D9Hook& Instance();

    // Initialize the hook system
    // Must be called after the game has created its D3D device
    bool Initialize();

    // Cleanup and restore original functions
    void Shutdown();

    // Set the camera controller that provides rotation offsets
    void SetCameraController(CameraController* controller);

    // Set the UDP receiver for tracking data (needed for recenter on unpause)
    void SetUdpReceiver(UdpReceiver* receiver);

    // Enable/disable view matrix modification
    void SetEnabled(bool enabled);
    bool IsEnabled() const { return m_enabled; }

    // Reset cached UI pointers (call after loading screen)
    void ResetUICache();

    // Get last error message
    const char* GetErrorMessage() const { return m_lastError; }

    // Get camera controller (for BeginScene hook)
    static CameraController* GetCameraController() { return s_cameraController; }

    // Signal a fatal error (called from exception handlers)
    static void SignalFatalError(const char* context);

    // Check if fatal error occurred (static for use in hooks)
    static bool IsFatalErrorSet() { return s_fatalErrorFlag; }

private:
    D3D9Hook();
    ~D3D9Hook();

    // Disable copying
    D3D9Hook(const D3D9Hook&) = delete;
    D3D9Hook& operator=(const D3D9Hook&) = delete;


    // Our hooked EndScene implementation
    static HRESULT STDMETHODCALLTYPE HookedEndScene(IDirect3DDevice9* device);

    // State
    bool m_initialized;
    bool m_hooked;
    bool m_enabled;
    bool m_fatalError;  // Set on SEH exception - disables hook permanently

    // Original function pointer
    static void* s_originalEndScene;

    // Device vtable for unhooking
    void** m_deviceVTable;

    // Camera controller reference
    static CameraController* s_cameraController;

    // UDP receiver reference (for recenter on unpause)
    static UdpReceiver* s_udpReceiver;

    // Fatal error flag (static for access from hooks)
    static bool s_fatalErrorFlag;

    // Error reporting
    char m_lastError[256];
};

}  // namespace HeadTracking
