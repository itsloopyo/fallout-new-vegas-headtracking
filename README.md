# Fallout: New Vegas Head Tracking

![Fallout: New Vegas running with this mod](https://raw.githubusercontent.com/itsloopyo/fallout-new-vegas-headtracking/main/assets/readme-clip.gif)

An unofficial head tracking mod for Fallout: New Vegas that moves the view with your head while your mouse or controller keeps aiming, driven by a webcam, phone, or any OpenTrack compatible tracker, with no VR headset required.

## Features

- **Decoupled look and aim** - head tracking moves the camera; aim stays on your mouse/controller
- **6DOF positional tracking** - lean and peek with head position
- **Works with any OpenTrack compatible tracker** - free options available for PC, iOS and Android

## Requirements

- Fallout: New Vegas for Windows. Supported executable profiles: Steam
  (2011-07-01) and Game Pass English (2016-01-21).
- A tracker sending OpenTrack UDP pose data.

Other executable fingerprints are left untouched and identified in the log.
This mod loads through `dsound.dll` and does not require xNVSE.

## Installation

Install or update the package through [Lopari](https://lopari.app), then launch
from Steam or the Xbox app as usual. Existing root-level configuration is preserved.

### Manual installation

Use the `-installer.zip` from the release:

1. Copy `plugins/HeadTracking.dll` next to `FalloutNV.exe`, renaming it to
   `dsound.dll`. Keep a backup if another mod already owns that filename.
2. Copy `plugins/HeadTracking.ini` to the same directory if you do not already
   have a `HeadTracking.ini` there.
3. For an upgrade from the NVSE version, copy your old
   `Data/NVSE/Plugins/HeadTracking.ini` to the game root to retain your settings,
   then remove the old `Data/NVSE/Plugins/HeadTracking.dll`.
4. Configure your tracker for UDP port `4242` and launch the game normally.

The launcher package also updates the legacy NVSE DLL path with a compatibility
copy. That copy stays dormant when the root proxy is loaded, preventing two
versions from changing the camera at once.

A manager that deploys only into `Data` cannot install this proxy: `dsound.dll`
must be beside the executable. This release has no Data-only Nexus archive.

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

| Action | Nav-cluster | Chord |
|--------|-------------|-------|
| Toggle tracking | `End` | `Ctrl+Shift+Y` |
| Cycle rotation and position | `Page Up` | `Ctrl+Shift+G` |
| Toggle hip-fire reticle | `Page Down` | `Ctrl+Shift+H` |
| Cycle ADS mode | `Insert` | `Ctrl+Shift+U` |
| Toggle yaw mode | `Delete` | `Ctrl+Shift+J` |

Page Up cycles full tracking, rotation only, then position only. Delete switches
between horizon-locked yaw and camera-local yaw. Existing `YawModeKey=0x2D`
settings move to Delete because Insert now owns ADS.

`Insert` / `Ctrl+Shift+U` cycles what happens when you aim down sights. All
three ease the view onto the sight line when you raise the weapon:

1. **Tracking paused** (default): yaw, pitch and leaning fade out while aiming.
2. **Tracking on, with an aim marker**: tracking continues relative to your
   head pose at ADS entry. A small white crosshair marks the projected clean
   aim point. Use this marker when head movement separates it from an optic's
   built-in reticle.
3. **Tracking on, no aim marker**: the same relative tracking without the marker.

The first-person weapon moves with the view so it stays pointed along your
mouse/controller aim. Leaning moves your eye away from the iron sights.

Roll remains absolute in all three modes. Entry takes 150 ms; lowering the
weapon blends back to normal tracking over 250 ms. The choice is saved in
`[Camera] ads_mode`. The selected mode is named in `HeadTracking.log`.

## Configuration

`HeadTracking.ini` lives next to `FalloutNV.exe`. Changes are reloaded while
running. See [the default configuration](config/HeadTracking.ini) for all keys.

`LocalSmoothing=0.0` and `RemoteSmoothing=0.15` under `[Smoothing]` select
smoothing by connection source. Both cover rotation and position; zero disables
smoothing. Configure sensitivity, deadzones, inversion and centring in your
tracker. Old `[Sensitivity]`, `[Deadzone]` and `[Camera] Mode` entries are ignored.
Head tracking changes the rendered view without writing player aim.

## Troubleshooting

Check `HeadTracking.log` beside the game executable. The previous launch is
kept as `HeadTracking.prev.log`. A supported profile, render-hook initialization,
and the first received UDP packet are logged separately.

If tracking does not respond, check the logged error, the tracker output port,
and whether another game is already using that port. Press End to enable
tracking. For an unsupported build, include the fingerprint line in a bug report.

If the view needs centring, use the centre control in your tracker. If yaw feels
awkward at steep viewing angles, try Delete to switch yaw mode.

## Updating and removing

Use Lopari to update or remove its installed package. For a manual install,
replace the DLL to update; remove this mod's `dsound.dll` to uninstall and restore
any DLL you backed up. Retain your INI for later use. Remove the compatibility
`Data/NVSE/Plugins/HeadTracking.dll` too if present. Other mods may still need xNVSE.

## Building from source

Requires Windows, Visual Studio with the C++ desktop workload, and pixi.
The x86 build uses checked-in source and the cameraunlock-core submodule;
no game files are build references.

```powershell
git clone --recurse-submodules https://github.com/itsloopyo/fallout-new-vegas-headtracking.git
cd fallout-new-vegas-headtracking
pixi run package
```

`pixi run package` configures, builds, runs the tests and writes the installer ZIP
under `release/`. `pixi run install` is a developer task that deploys the build
to detected local game installations.

## Community & Support

- Discord: [Loop's Head Tracking Hangout](https://discord.com/invite/dxyZdyFNT9) - setup help, bug reports, and new-release announcements
- [Lopari](https://lopari.app) - free Windows launcher with one-click install and launch for the released head-tracking mods
- [Headcam](https://headcam.app) - free app that turns your iPhone or Android phone into the head tracker

## License

MIT. See [LICENSE](LICENSE).

Third-party notices ship in [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).

## Credits

- [Obsidian Entertainment](https://www.obsidian.net/) - Fallout: New Vegas
- [Bethesda Softworks](https://bethesda.net/) - publisher
- [xNVSE Team](https://github.com/xNVSE/NVSE) - script extender
- [OpenTrack](https://github.com/opentrack/opentrack) - head tracking software
- [cameraunlock-core](https://github.com/itsloopyo/cameraunlock-core) - shared head tracking library

## Disclaimer

This mod is not affiliated with, endorsed by, or supported by Obsidian Entertainment or Bethesda Softworks. "Fallout: New Vegas" is a trademark of Bethesda Softworks LLC. Use this mod at your own risk; no warranty is provided. Back up your save files before installing any mods.
