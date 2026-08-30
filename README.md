# Fallout: New Vegas Head Tracking

![Fallout: New Vegas running with this mod](https://raw.githubusercontent.com/itsloopyo/fallout-new-vegas-headtracking/main/assets/readme-clip.gif)

An unofficial head tracking mod for Fallout: New Vegas that moves the view with your head while your mouse or controller keeps aiming, driven by OpenTrack over UDP, with no VR headset required.

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

The mod listens for OpenTrack pose data on UDP port `4242`, on every network
interface. One datagram is six little-endian 64-bit floats in the order
`x, y, z, yaw, pitch, roll`: position in centimetres, rotation in degrees, 48
bytes in total. Anything that sends that to that port drives the view.
OpenTrack's **UDP over network** output sends exactly this, and the steps below
set it up.

1. Install [OpenTrack](https://github.com/opentrack/opentrack/releases).
2. Pick a tracker under **Input**, using the notes below.
3. Set **Output** to **UDP over network**, host `127.0.0.1`, port `4242`.
4. Press **Start**. Tracking and the game can start in either order.

### Webcam

OpenTrack ships a `neuralnet tracker` input that reads a plain webcam. Select it
under **Input**, pick your camera in its settings, and use the output settings
above. How well it tracks depends on your camera and your lighting, so try it
before buying anything.

### Phone

A phone app can reach the mod directly, with no OpenTrack on the PC, if it sends
the datagram described above. Point it at this PC's IP address (run `ipconfig`
to find it) on port `4242`. Not every phone tracker speaks this protocol, so
check yours for an OpenTrack or UDP output option first. [Headcam](https://headcam.app)
sends it, and I wrote it so decent tracking is free for anyone who already owns
a phone.

Sending direct works when the app filters its own signal on the device. The
mod's smoothing is sized to take the edge off a clean signal rather than to
rescue a noisy one, so a raw feed sent direct will jitter. If it does, point the
app at OpenTrack's **UDP over network** *input* on some other port, say 5252,
and let OpenTrack's filters and curves clean it up before its output forwards to
`127.0.0.1:4242`.

Anything arriving from outside `127.0.0.0/8` counts as a remote connection and
is smoothed with `RemoteSmoothing` rather than `LocalSmoothing`. That includes a
tracker on this very PC that sends to the machine's own LAN address, because the
mod reads the source address and not the machine.

### Headset or other hardware

If your device has an OpenTrack input driver, select it under **Input** and use
the same output settings. OpenTrack's own **Input** list is the authority on
what it can read; the mod only ever sees what OpenTrack sends.

### Centring

Centring belongs to your tracker. The mod subtracts no centre of its own: it
applies the pose it receives exactly as it arrives, so a stream of zeros holds
the view where the game itself puts it. Press the centre control in your tracker
(OpenTrack's **Center** bind, or the CENTER button in Headcam) and the tracker
zeroes its own output, which leaves the view centred with the mod doing nothing.

That is why there is no centre hotkey here and nothing to re-centre in game. Two
centres in series would drift apart, because each side re-centres at moments the
other cannot see, and you would end up pressing twice to centre once. If the
view sits off to one side, centre it in the tracker.

## Controls

Two equivalent binding sets - use whichever your keyboard has:

| Action              | Nav-cluster | Chord           |
|---------------------|-------------|-----------------|
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
; Smoothing applied when the tracker runs on this machine (loopback).
; 0 = no smoothing, 1 = heavy. Covers rotation and position.
LocalSmoothing=0.0
; Smoothing applied when the tracker is a remote device on the network.
; 0 = no smoothing, 1 = heavy. Covers rotation and position.
RemoteSmoothing=0.15

[Deadzone]
; Deadzone thresholds in degrees (0.0-30.0)
Yaw=0.0
Pitch=0.0
Roll=0.0

[Hotkeys]
; Nav-cluster virtual key codes (hex). Each action also accepts a fixed
; Ctrl+Shift+<letter> chord (Y/G/H/U) which is not configurable.
; End=0x23, PageUp=0x21, PageDown=0x22, Insert=0x2D
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
- Check `Data/NVSE/Plugins/HeadTracking.log` for errors. It is rewritten on
  every launch and the previous run is kept as `HeadTracking.prev.log`, so
  send both when reporting a problem.

**No tracking response:**
- Check `Data/NVSE/Plugins/HeadTracking.log` for the `UDP: First UDP packet
  received` line. Without it nothing is reaching the game
- Verify OpenTrack is running and tracking is active
- Check that OpenTrack output is set to UDP `127.0.0.1:4242`
- Press **End** to make sure tracking is enabled
- Ensure no firewall is blocking UDP port 4242

**Camera stutters or jumps:**
- Install [NVTF (New Vegas Tick Fix)](https://www.nexusmods.com/newvegas/mods/66537)
- Increase the smoothing value your tracker uses in `HeadTracking.ini`:
  `RemoteSmoothing` for a phone or another device on the network,
  `LocalSmoothing` for a tracker running on this PC
- Add small deadzones to filter tracker noise

**View rotates the wrong way:**
- Set the appropriate invert option in OpenTrack's output mapping, or adjust the axis curves
- Confirm OpenTrack's output axes match your tracker orientation
- Centre the view in your tracker app while looking straight ahead

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

## Community & Support

- Discord: [Loop's Head Tracking Hangout](https://discord.com/invite/dxyZdyFNT9) - setup help, bug reports, and new-release announcements
- [Lopari](https://lopari.app) - free Windows launcher with one-click install and launch for the released head-tracking mods
- [Headcam](https://headcam.app) - free app that turns your iPhone or Android phone into the head tracker

## License

MIT. See [LICENSE](LICENSE).

Third-party components, and the reasons xNVSE is downloaded at install time
rather than bundled, are recorded in
[THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md). This mod redistributes no
Fallout: New Vegas code or assets and no xNVSE binary.

## Credits

- [Obsidian Entertainment](https://www.obsidian.net/) - Fallout: New Vegas
- [Bethesda Softworks](https://bethesda.net/) - publisher
- [xNVSE Team](https://github.com/xNVSE/NVSE) - script extender
- [OpenTrack](https://github.com/opentrack/opentrack) - head tracking software
- [cameraunlock-core](https://github.com/itsloopyo/cameraunlock-core) - shared head tracking library

## Disclaimer

This mod is not affiliated with, endorsed by, or supported by Obsidian Entertainment or Bethesda Softworks. "Fallout: New Vegas" is a trademark of Bethesda Softworks LLC. Use this mod at your own risk; no warranty is provided. Back up your save files before installing any mods.
