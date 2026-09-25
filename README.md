# Fallout: New Vegas Head Tracking

![Fallout: New Vegas running with this mod](https://raw.githubusercontent.com/itsloopyo/fallout-new-vegas-headtracking/main/assets/readme-clip.gif)

An unofficial head tracking mod for Fallout: New Vegas that moves the view with your head while your mouse or controller keeps aiming, driven by a webcam, phone, or any OpenTrack compatible tracker, with no VR headset required.

## Features

- **Decoupled look and aim** - head tracking moves the camera; aim stays on your mouse/controller
- **6DOF positional tracking** - lean and peek with head position
- **Works with any OpenTrack compatible tracker** - free options available for PC, iOS and Android

## Requirements

- Fallout: New Vegas for Windows. Supported executable profiles: Steam
  (2011-07-01) and Xbox Game Pass English (2016-01-21).
- A tracker sending OpenTrack UDP pose data.

Other executable fingerprints are left untouched and identified in the log.
This mod loads through `dsound.dll` and does not require xNVSE.

## Installation

### Lopari

Download [Lopari](https://lopari.app), choose **Fallout: New Vegas**, and click
**Play with head tracking**.

Existing root-level configuration is preserved.
In windowed mode, the game window is centred on its monitor's work area at startup.

### Manual installation

Download the `-installer.zip` from the release, extract it anywhere, and run
`install.cmd`. It finds the game, backs up any `dsound.dll` already beside
`FalloutNV.exe`, deploys the mod under that name, and writes `HeadTracking.ini`
only if you do not have one. `uninstall.cmd` reverses it and puts your backup
back. Both accept the game folder as an argument if detection picks the wrong
copy:

```
install.cmd "D:\Games\Fallout New Vegas"
```

To place the files yourself instead:

1. Copy `plugins/dsound.dll` next to `FalloutNV.exe`. Keep a backup if another
   mod already owns that filename.
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

| Action | Default keys |
|--------|--------------|
| Toggle tracking | `End`, `Ctrl+Shift+Y` |
| Cycle rotation and position | `Page Up`, `Ctrl+Shift+G` |
| Toggle yaw mode | `Page Down`, `Ctrl+Shift+H` |

Each action takes every key its line under `[Hotkeys]` in `HeadTracking.ini`
lists, the chord included, so you can change or remove any of them. A key listed
without Ctrl and Shift does not fire while both are held. Hotkeys act only while
the game is the window in front.

Page Up cycles full tracking, rotation only, then position only. Page Down
switches between horizon-locked yaw and camera-local yaw. Both are saved to
`HeadTracking.ini` as you press them and come back the next time the game
starts. End turns head tracking on or off for the current session only; whether
it is on when the game starts is `EnableOnStartup`.

A `HeadTracking.ini` from an earlier version keeps the yaw mode key it had: the
key its `YawModeKey` named, or `Delete` where it named none, with `Ctrl+Shift+J`
beside it. The defaults above are what a new file holds.

The gold hip-fire reticle marks your mouse/controller aim as you move your head.
The game's own sights take over while you aim, so the reticle is hidden then.

### Aiming down sights

Head tracking stays on while you aim. The weapon stays where your mouse or
controller points it, so with your head turned it sits off to one side with its
sights still lined up, and your rounds land where those sights point. When the
game narrows its field of view as you zoom, the mod scales head yaw, pitch and
lean down in proportion.

## Configuration

`HeadTracking.ini` lives next to `FalloutNV.exe`. Changes are reloaded while
running, except `[Network] UdpPort`, which requires a game restart.

`LocalSmoothing` and `RemoteSmoothing` under `[Smoothing]` select smoothing by
connection source. Both cover rotation and position; zero disables smoothing.
Configure sensitivity, deadzones, inversion and centring in your tracker.
Head tracking changes the rendered view without writing player aim.
Positional leaning is limited by the game's world collision to keep the camera
away from walls.

<!-- cameraunlock:config -->
The mod reads its settings from `HeadTracking.ini` in the game folder, at one of these paths depending on the store the game came from:

- `HeadTracking.ini`
- `Fallout New Vegas English\HeadTracking.ini`

It creates the file when it starts and finds none. Edit it with any text editor.

Earlier versions of the mod used an older layout for this file. The first time this version starts, it converts the file once into the layout below and keeps the file as it was beside it as `HeadTracking.ini.pre-canonical`. `HeadTracking.ini.pre-canonical.last`, when present, is the file as it was before the most recent conversion: the mod converts the file again when it finds the older layout later, for example after an older version of the mod rewrote it.

Comments, and keys the mod never read, are not carried over. Nor are these, where your old file had them:

- Reticle settings, and a key that toggled the reticle.
- A sensitivity, scale, deadzone, response curve or axis inversion you changed from its default. Set these in your tracker instead.
- The setting for a feature that earlier versions shipped switched off while it was untested. It now follows the mod's default.

An older version of the mod may not read the new layout correctly. It reads a key that moved as its own default, and it can misread a hotkey or another value that is now written as a name. To go back to an older version, first copy `HeadTracking.ini.pre-canonical` back over `HeadTracking.ini`, which restores the old file.

With every setting at its default, the file reads:

```ini
; Fallout: New Vegas head tracking settings.
; Comments start with ; and go on their own line. Text after a value is part of the value.
; Hotkeys are key names such as End, PageUp or Ctrl+Shift+Y. Separate several with commas; leave empty for none.

[CameraUnlock]
; Written by the mod. Leave this section in place.
ConfigFormat=1

[Network]
; UDP port the mod receives tracker data on (OpenTrack protocol).
; Restart the game after changing it.
UdpPort=4242

[General]
; true: head tracking is on when the game starts. ToggleKey turns it on and off.
EnableOnStartup=true
; true: yaw turns around the world's up axis. false: around the camera's own up axis.
WorldSpaceYaw=true
; true: turning your head turns the view.
; Tracking mode at startup, with PositionEnabled. The mode hotkey changes both.
RotationEnabled=true

[Smoothing]
; Smoothing when the tracker runs on this PC. 0 is the least, 1 the most.
LocalSmoothing=0.0
; Smoothing when the tracker is another device on the network, such as a phone.
; 0 is the least, 1 the most.
RemoteSmoothing=0.15

[Position]
; true: moving your head moves the view.
; Tracking mode at startup, with RotationEnabled. The mode hotkey changes both.
PositionEnabled=true

[Hotkeys]
; Turns head tracking on and off.
ToggleKey=End, Ctrl+Shift+Y
; Changes the tracking mode: rotation and position, rotation only, position only.
CycleTrackingModeKey=PageUp, Ctrl+Shift+G
; Switches yaw between the world's up axis and the camera's own (WorldSpaceYaw).
YawModeKey=PageDown, Ctrl+Shift+H

[GameState]
; true: head tracking also works in the third-person camera.
TrackInThirdPerson=true
; true: head tracking stays on in VATS. false: it pauses there.
TrackInVATS=false
; true: head tracking pauses while you are in combat.
PauseDuringCombat=false

[Input]
; When the hotkeys are ignored. They never work on a loading screen or in character creation.
; Never: they work everywhere else.
; MenusOnly: not while a menu is open or the game is paused.
; AllDialogue: not in menus or conversations either.
; AllOverlays: not in menus, conversations, the console, VATS or the Pip-Boy either.
InputBlockMode=Never
; Milliseconds after a hotkey fires before it can fire again, 50 to 2000.
HotkeyDebounceMs=200
```
<!-- /cameraunlock:config -->

## Troubleshooting

Check `HeadTracking.log` beside the game executable. The previous launch is
kept as `HeadTracking.prev.log`. A supported profile, render-hook initialization,
and the first received UDP packet are logged separately.

If tracking does not respond, check the logged error, the tracker output port,
and whether another game is already using that port. Press End to enable
tracking. For an unsupported build, include the fingerprint line in a bug report.

If the view needs centring, use the centre control in your tracker. If yaw feels
awkward at steep viewing angles, switch yaw mode with the key `YawModeKey` lists
(Page Down in a new install).

**The weapon is off to one side when I aim down sights.** Your head is turned:
the weapon stays on your aim and you are looking past it. Turn back to it, or
move your aim to where you are looking.

### Known limitation

Camera alignment for foliage and effects has improved, but occasional incorrect
frames are still reported during head tracking. The intermittent flicker is not
fully resolved.

## Updating and removing

Use Lopari to update or remove its installed package. For a manual install, run
`install.cmd` again to update, and `uninstall.cmd` to remove: it takes out this
mod's `dsound.dll`, restores any DLL it backed up, and removes the logs. It
leaves `HeadTracking.ini` in place, with the `HeadTracking.ini.pre-canonical`
copies an update may have made beside it, so your settings are still there if you
install again. If you placed the files by hand, remove them the same way, and
remove the compatibility `Data/NVSE/Plugins/HeadTracking.dll` too if present.
Other mods may still need xNVSE.

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
- [xNVSE Team](https://github.com/xNVSE/NVSE) - plugin ABI and engine layout references
- [OpenTrack](https://github.com/opentrack/opentrack) - head tracking software
- [cameraunlock-core](https://github.com/itsloopyo/cameraunlock-core) - shared head tracking library

## Disclaimer

This mod is not affiliated with, endorsed by, or supported by Obsidian Entertainment or Bethesda Softworks. "Fallout: New Vegas" is a trademark of Bethesda Softworks LLC. Use this mod at your own risk; no warranty is provided. Back up your save files before installing any mods.
