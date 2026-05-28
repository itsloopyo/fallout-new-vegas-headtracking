# Fallout: New Vegas Head Tracking

An NVSE plugin that adds head tracking to Fallout: New Vegas, letting you look around the Mojave naturally while your weapon aim stays on your mouse or controller, with no VR headset required.

<!-- ![Mod GIF](https://raw.githubusercontent.com/itsloopyo/fallout-new-vegas-headtracking/main/assets/readme-clip.gif) -->

## Features

- **Decoupled look and aim** - head tracking moves the camera; aim stays on your mouse/controller
- **6DOF positional tracking** - lean and peek with head position

## Requirements

- [Fallout: New Vegas](https://store.steampowered.com/app/22380/Fallout_New_Vegas/) (Steam or GOG)
- [xNVSE](https://github.com/xNVSE/NVSE) 6.4.1 or newer (auto-installed if missing)
- [OpenTrack](https://github.com/opentrack/opentrack) or a compatible head tracking app (smartphone, webcam, or dedicated hardware)
- Windows (x86)

[NVTF (New Vegas Tick Fix)](https://www.nexusmods.com/newvegas/mods/66537) is strongly recommended for smooth camera movement.

## Installation

1. Download the latest release from the [Releases page](https://github.com/itsloopyo/fallout-new-vegas-headtracking/releases)
2. Extract the ZIP anywhere
3. Double-click `install.cmd`
4. Configure OpenTrack to output UDP to `127.0.0.1:4242`
5. Launch the game via `nvse_loader.exe`

The installer automatically finds your game by checking the Windows registry for your Steam/GOG installation. If xNVSE is not already present, `install.cmd` downloads a pinned version from the official [xNVSE GitHub release](https://github.com/xNVSE/NVSE/releases) and verifies its SHA-256 before installing it. xNVSE has no redistributable license, so it is fetched at install time rather than bundled in the release ZIP; an existing xNVSE install is detected and left untouched.

If it can't find the game, either:
- Set the `FalloutNVPath` environment variable to your game folder
- Run from command prompt: `install.cmd "D:\Games\Fallout New Vegas"`

### Manual Installation

If you prefer to place files by hand, or you are extracting the Nexus ZIP (which contains only the mod files):

1. Install [xNVSE](https://github.com/xNVSE/NVSE/releases) by extracting its archive into your game folder (next to `FalloutNV.exe`).
2. Copy the mod DLL (`HeadTracking.dll`) and `HeadTracking.ini` into `Data/NVSE/Plugins/` inside your game folder. Create the folder if it does not exist.
3. Launch the game via `nvse_loader.exe`.

## Setting Up OpenTrack

1. Download and install [OpenTrack](https://github.com/opentrack/opentrack/releases)
2. Configure your tracker as input
3. Set output to **UDP over network**
4. Host: `127.0.0.1`, Port: `4242`
5. Start tracking before launching the game
6. Launch Fallout: New Vegas via `nvse_loader.exe`

### VR Headset Setup

A VR headset makes an excellent high-precision tracker.

1. Connect your headset to the PC via Air Link (Quest) or Virtual Desktop.
2. Start SteamVR.
3. In OpenTrack, set the input to **SteamVR**.
4. Set output to **UDP over network** (`127.0.0.1:4242`).
5. Start tracking before launching the game.

### Webcam Setup

No special hardware needed. OpenTrack's built-in **neuralnet tracker** uses any webcam for 6DOF face tracking.

1. In OpenTrack, set the input to **neuralnet tracker**
2. Select your webcam in the tracker settings
3. Set output to **UDP over network** (`127.0.0.1:4242`)
4. Start tracking before launching the game
5. Recenter in OpenTrack via its hotkey, and press **Home** in-game to recenter the mod as needed

### Phone App Setup

This mod includes built-in smoothing to handle network jitter, so if your tracking app already provides a filtered signal, you can send directly from your phone to the mod on port 4242 without needing OpenTrack on PC.

1. Install an OpenTrack-compatible head tracking app from your phone's app store
2. Configure your phone app to send to your PC's IP address on port 4242 (run `ipconfig` to find it, e.g. `192.168.1.100`)
3. Set the protocol to OpenTrack/UDP
4. Start tracking

**With OpenTrack (optional):** If you experience jerky motion, want curve mapping, or want a visual preview, route through OpenTrack instead. The mod already listens on port 4242, so OpenTrack's input must use a different port:
1. In OpenTrack, set Input to **UDP over network** on port **5252** (or any port other than 4242)
2. Set Output to **UDP over network** at `127.0.0.1:4242`
3. In your phone app, send to your PC's IP on port **5252** (matching OpenTrack's input port)
4. Make sure port 5252 is open in your PC's firewall for incoming UDP traffic

## Controls

Two equivalent binding sets - use whichever your keyboard has:

| Action              | Nav-cluster | Chord           |
|---------------------|-------------|-----------------|
| Recenter            | `Home`      | `Ctrl+Shift+T`  |
| Toggle tracking     | `End`       | `Ctrl+Shift+Y`  |
| Cycle tracking mode | `Page Up`   | `Ctrl+Shift+G`  |
| Toggle reticle      | `Page Down` | `Ctrl+Shift+H`  |
| Toggle yaw mode     | `Insert`    | `Ctrl+Shift+U`  |

The yaw-mode toggle uses `Insert` / `Ctrl+Shift+U` rather than the catalogue-standard
`Page Down` / `Ctrl+Shift+H`, because those are already the reticle toggle in this mod.
The nav-cluster key is configurable via `YawModeKey` in `HeadTracking.ini`.

`Page Up` / `Ctrl+Shift+G` cycles tracking mode:

1. Normal head-tracked gameplay
2. Positional tracking disabled, rotational tracking enabled
3. Rotational tracking disabled, positional tracking enabled
4. Back to normal

## Configuration

The plugin is configured via `HeadTracking.ini` in `Data/NVSE/Plugins/`. A default config is created on first run. The mod auto-reloads the config file when changes are detected.

```ini
[Network]
; UDP port for OpenTrack data (default: 4242)
Port=4242

[Sensitivity]
; Multipliers for each axis (0.1-5.0)
Yaw=1.0
Pitch=1.0
Roll=1.0

[Smoothing]
; Smoothing amount (0.0 = instant, 0.99 = maximum smoothing)
Amount=0

[Deadzone]
; Deadzone thresholds in degrees (0.0-30.0)
Yaw=0.0
Pitch=0.0
Roll=0.0

[Hotkeys]
; Nav-cluster virtual key codes (hex). Each action also accepts a fixed
; Ctrl+Shift+<letter> chord (T/Y/G/H/U) which is not configurable.
; Home=0x24, End=0x23, PageUp=0x21, PageDown=0x22, Insert=0x2D
Recenter=0x24
Toggle=0x23
CycleTrackingMode=0x21
ReticleToggle=0x22
; Insert (Page Down is taken by ReticleToggle in this mod)
YawModeKey=0x2D
DebounceMs=200

[Camera]
; Mode: 0=Coupled (affects aim), 1=Decoupled (free-look), 2=BodyTracking
Mode=1
; WorldSpaceYaw: 1 = horizon-locked yaw (default), 0 = camera-local
WorldSpaceYaw=1

[GameState]
; InputBlockMode: 0=Never, 1=MenusOnly, 2=AllDialogue, 3=AllOverlays
InputBlockMode=0
TrackInThirdPerson=1
TrackInVATS=0
PauseDuringCombat=0

[Feedback]
ShowMessages=1
```

## Troubleshooting

**Plugin not loading:**
- Verify xNVSE is installed correctly (`nvse_loader.exe` exists in game folder)
- Launch via `nvse_loader.exe`, **not** `FalloutNV.exe` directly
- Check `HeadTracking_debug.log` in the game folder for errors

**No tracking response:**
- Verify OpenTrack is running and tracking is active
- Check that OpenTrack output is set to UDP `127.0.0.1:4242`
- Press **End** to make sure tracking is enabled
- Press **Home** to recenter
- Ensure no firewall is blocking UDP port 4242

**Camera stutters or jumps:**
- Install [NVTF (New Vegas Tick Fix)](https://www.nexusmods.com/newvegas/mods/66537)
- Increase smoothing in `HeadTracking.ini`
- Add small deadzones to filter tracker noise

**View rotates the wrong way:**
- Set the appropriate invert option in OpenTrack's output mapping, or adjust the axis curves
- Confirm OpenTrack's output axes match your tracker orientation
- Recenter with **Home** while looking straight ahead

**Yaw feels wrong when looking up or down at extreme angles:**
- Try toggling between world-locked and camera-local yaw with **Insert** (or `Ctrl+Shift+U`). World-locked (default) is horizon-stable; camera-local follows the camera's current up-axis.

## Known Limitations

**Sky drifts slightly with positional (6DOF) leaning:** the sky is rendered as
a backdrop centered on the un-offset camera eye, so leaning your head shifts the
view relative to it and the sky appears to slide a little. Rotational tracking is
unaffected. The effect scales with positional movement; lowering position
sensitivity reduces it.

## Updating

Download the new release and run `install.cmd` again. Your config is preserved.

## Uninstalling

Run `uninstall.cmd` from the release folder. This removes the mod DLL and INI. xNVSE is only removed if the installer put it there; it is left intact otherwise since other mods may depend on it. To force-remove xNVSE as well:

```powershell
uninstall.cmd /force
```

## Building from Source

### Prerequisites

- Visual Studio 2019 or newer (with C++ desktop workload)
- CMake 3.20 or newer
- [pixi](https://pixi.sh) task runner

### Build

```bash
git clone --recurse-submodules https://github.com/itsloopyo/fallout-new-vegas-headtracking.git
cd fallout-new-vegas-headtracking

# Build and install to game directory
pixi run install

# Or just build
pixi run build-release

# Package for release
pixi run package
```

## License

MIT. See [LICENSE](LICENSE).

## Credits

- [Obsidian Entertainment](https://www.obsidian.net/) - Fallout: New Vegas
- [Bethesda Softworks](https://bethesda.net/) - publisher
- [xNVSE Team](https://github.com/xNVSE/NVSE) - script extender
- [OpenTrack](https://github.com/opentrack/opentrack) - head tracking software
- [cameraunlock-core](https://github.com/itsloopyo/cameraunlock-core) - shared head tracking library

## Disclaimer

This mod is not affiliated with, endorsed by, or supported by Obsidian Entertainment or Bethesda Softworks. "Fallout: New Vegas" is a trademark of Bethesda Softworks LLC. Use this mod at your own risk; no warranty is provided. Back up your save files before installing any mods.
