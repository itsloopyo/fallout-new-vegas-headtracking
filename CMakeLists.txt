cmake_minimum_required(VERSION 3.20)
project(HeadTracking VERSION 0.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

enable_testing()
add_executable(fnv_weapon_view_tests tests/weapon_view_tests.cpp)
target_include_directories(fnv_weapon_view_tests PRIVATE src)
add_test(NAME fnv_weapon_view COMMAND fnv_weapon_view_tests)
add_executable(fnv_startup_tests tests/startup_tests.cpp)
target_include_directories(fnv_startup_tests PRIVATE src)
add_test(NAME fnv_startup COMMAND fnv_startup_tests)

# Debug logging option (disabled by default for release builds)
option(HEADTRACKING_DEBUG_LOGGING "Enable debug file logging (performance impact)" OFF)


# Windows-specific settings
if(WIN32)
    add_definitions(-DWIN32_LEAN_AND_MEAN -DNOMINMAX -D_CRT_SECURE_NO_WARNINGS)
    if(HEADTRACKING_DEBUG_LOGGING)
        add_definitions(-DHEADTRACKING_DEBUG_LOGGING=1)
    endif()

    # Use static runtime to avoid DLL dependencies
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# CameraUnlock shared library (submodule)
set(CAMERAUNLOCK_BUILD_HOOKS ON CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_SOURCE_DIR}/cameraunlock-core/cpp cameraunlock-core)

include_directories(${CMAKE_SOURCE_DIR}/src)

# The canonical config conversion's differential test. The oracle is v0.3.1's
# reader built against core as v0.3.1 pinned it, so it is its own executable and
# never links the current core.
add_executable(fnv_config_oracle
    tests/config_differential/oracle/oracle_main.cpp
    tests/config_differential/oracle/published_config.cpp
    tests/config_differential/oracle/core-c480d8a/src/config/ini_reader.cpp
)
target_include_directories(fnv_config_oracle PRIVATE tests/config_differential/oracle/core-c480d8a/include)
add_executable(fnv_config_differential
    tests/config_differential/differential_tests.cpp
    src/config.cpp
    src/legacy_config/legacy_config.cpp
)
target_include_directories(fnv_config_differential PRIVATE tests/config_differential)
target_compile_definitions(fnv_config_differential PRIVATE FNV_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(fnv_config_differential cameraunlock)
add_test(NAME fnv_config_differential COMMAND fnv_config_differential $<TARGET_FILE:fnv_config_oracle>)

# The committed config against the table, and the saves. `fnv_config_tests
# --render-config <path>` is what pixi run render-config writes the file with.
add_executable(fnv_config_tests
    tests/config_tests.cpp
    src/config.cpp
    src/legacy_config/legacy_config.cpp
)
target_compile_definitions(fnv_config_tests PRIVATE FNV_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(fnv_config_tests cameraunlock)
add_test(NAME fnv_config COMMAND fnv_config_tests)

# Source files
set(SOURCES
    src/main.cpp
    src/proxy_entry.cpp
    src/build_registry.cpp
    src/steam_offsets.cpp
    src/gamepass_offsets.cpp
    src/plugin.cpp
    src/config.cpp
    src/legacy_config/legacy_config.cpp
    src/udp_receiver.cpp
    src/camera_controller.cpp
    src/game_state.cpp
    src/hotkey_handler.cpp
    src/d3d9_hook.cpp
    src/d3d9_crosshair.cpp
    src/d3d9_culling.cpp
    src/world_query.cpp
    src/weapon_view.cpp
    src/sky_view.cpp
    src/window_centering.cpp
)

# Header files (for IDE project generation)
set(HEADERS
    src/plugin.h
    src/proxy_entry.h
    src/build_profile.h
    src/version.h
    src/config.h
    src/legacy_config/legacy_config.h
    src/tracking_data.h
    src/game_offsets.h
    src/udp_receiver.h
    src/camera_controller.h
    src/game_state.h
    src/hotkey_handler.h
    src/d3d9_hook.h
    src/d3d9_internal.h
    src/weapon_view.h
    src/startup.h
    src/debug_log.h
    src/window_centering.h
)

# Our own declarations of the script-extender plugin ABI and the game's
# runtime layout. Interoperability declarations, not a vendored SDK - see
# THIRD-PARTY-NOTICES.md.
set(NVSE_ABI_HEADERS
    src/nvse_abi/PluginAPI.h
    src/nvse_abi/GameAPI.h
)

# Create DLL
# The .def is what puts DirectSoundCreate8 on ordinal 11, which is the ordinal
# the game imports. A name-only export would not satisfy the import.
add_library(HeadTracking SHARED ${SOURCES} ${HEADERS} ${NVSE_ABI_HEADERS} src/dsound.def)

# Link CameraUnlock library, Winsock and D3D9
target_link_libraries(HeadTracking
    cameraunlock
    minhook
    ws2_32
    d3d9
)

# Compiler warnings
if(MSVC)
    target_compile_options(HeadTracking PRIVATE
        /W4
        /WX
        /permissive-
        /Zc:__cplusplus
    )
endif()

# Output settings - no lib prefix, specific output name
set_target_properties(HeadTracking PROPERTIES
    PREFIX ""
    OUTPUT_NAME "HeadTracking"
    SUFFIX ".dll"
)

# Set output directories for different configurations
set_target_properties(HeadTracking PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin/Debug"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin/Release"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/bin/RelWithDebInfo"
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}/bin/MinSizeRel"
)

# Generate Visual Studio filters matching directory structure
source_group("Source Files" FILES ${SOURCES})
source_group("Header Files" FILES ${HEADERS})
source_group("NVSE ABI" FILES ${NVSE_ABI_HEADERS})

# Print configuration summary
message(STATUS "")
message(STATUS "HeadTracking dsound.dll Proxy Configuration:")
message(STATUS "  Version: ${PROJECT_VERSION}")
message(STATUS "  C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  Using CameraUnlock shared library")
message(STATUS "")
