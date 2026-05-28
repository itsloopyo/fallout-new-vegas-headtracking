cmake_minimum_required(VERSION 3.20)
project(HeadTracking VERSION 0.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

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
add_subdirectory(${CMAKE_SOURCE_DIR}/cameraunlock-core/cpp cameraunlock-core)

# NVSE SDK include path
include_directories(${CMAKE_SOURCE_DIR}/nvse)
include_directories(${CMAKE_SOURCE_DIR}/src)

# Source files
set(SOURCES
    src/main.cpp
    src/plugin.cpp
    src/config.cpp
    src/udp_receiver.cpp
    src/camera_controller.cpp
    src/game_state.cpp
    src/hotkey_handler.cpp
    src/d3d9_hook.cpp
    src/d3d9_crosshair.cpp
    src/d3d9_culling.cpp
    src/window_centering.cpp
)

# Header files (for IDE project generation)
set(HEADERS
    src/plugin.h
    src/version.h
    src/config.h
    src/tracking_data.h
    src/game_offsets.h
    src/udp_receiver.h
    src/camera_controller.h
    src/game_state.h
    src/hotkey_handler.h
    src/d3d9_hook.h
    src/d3d9_internal.h
    src/debug_log.h
    src/window_centering.h
)

# NVSE SDK headers
set(NVSE_HEADERS
    nvse/nvse/PluginAPI.h
    nvse/nvse/GameAPI.h
)

# Create DLL
add_library(HeadTracking SHARED ${SOURCES} ${HEADERS} ${NVSE_HEADERS})

# Link CameraUnlock library, Winsock and D3D9
target_link_libraries(HeadTracking
    cameraunlock
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
source_group("NVSE SDK" FILES ${NVSE_HEADERS})

# Print configuration summary
message(STATUS "")
message(STATUS "HeadTracking NVSE Plugin Configuration:")
message(STATUS "  Version: ${PROJECT_VERSION}")
message(STATUS "  C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  Using CameraUnlock shared library")
message(STATUS "")
