# Changelog

## [0.3.1] - 2026-09-16

### Added

- load through a dsound proxy and track while aiming
- install and remove the proxy with install.cmd

### Fixed

- correct the MIT text, ship notices in both ZIPs, retitle the NVSE ABI headers
- mirror the vertical limit and restore the MIT grant
- re-sync THIRD-PARTY-NOTICES.md before cutting the tag
- seed the config instead of deploying it
- rewrite install.cmd MOD_VERSION from the canonical version
- remove obsolete NVSE installation tooling
- preserve originals across shim upgrades and uninstall failures

## [Unreleased]

### Added

- Support for the Steam and Game Pass English executable profiles through a
  `dsound.dll` proxy, loaded when the game starts normally without xNVSE.
- Three ADS modes on Insert / Ctrl+Shift+U: pause yaw, pitch and leaning; keep
  tracking with a white aim marker; or keep tracking without the marker.
- Collision limits for positional leaning and automatic window centring at startup
  in windowed mode.

### Changed

- Configuration and logs live beside `FalloutNV.exe`. The launcher preserves
  existing configuration and updates the legacy NVSE DLL to prevent duplicate
  camera changes when the proxy is loaded.
- Releases contain one launcher installer ZIP, including manual installation
  instructions and licence notices. The proxy must be installed beside the game
  executable, so there is no Data-only Nexus archive.
- Yaw mode moves to Delete / Ctrl+Shift+J. Insert is reserved for ADS mode.
- ADS tracking uses the head pose at ADS entry as its reference for yaw, pitch
  and leaning. Roll remains active in every ADS mode. The first-person weapon
  follows the rendered view while player aim stays under mouse/controller control.
- Retired sensitivity, deadzone and camera-mode settings are ignored. Configure
  pose shaping and centring in your tracker.
- `HeadTracking.ini` has a new layout. The first time this version starts, it converts the file once into the new layout and keeps the file as it was beside it as `HeadTracking.ini.pre-canonical`. `HeadTracking.ini.pre-canonical.last`, when present, is the file as it was before the most recent conversion: the mod converts the file again when it finds the older layout later, for example after an older version of the mod rewrote it.
- Comments, and keys the mod never read, are not carried over. Nor are these, where your old file had them:
  - Reticle settings, and a key that toggled the reticle.
- Hotkeys are written as key names, and each hotkey lists every key that triggers it, the Ctrl+Shift chord included: `ToggleKey=End, Ctrl+Shift+Y`.
- Keys that moved or were renamed: `[Network] Port` is `UdpPort`, `[Hotkeys] Toggle` is `ToggleKey`, `CycleTrackingMode` is `CycleTrackingModeKey`, `[Camera] WorldSpaceYaw` is under `[General]`, `[Hotkeys] DebounceMs` is `[Input] HotkeyDebounceMs`, and `[GameState] InputBlockMode` is under `[Input]` and takes `Never`, `MenusOnly`, `AllDialogue` or `AllOverlays` in place of 0 to 3. True and false are written `true` and `false`.
- An older version of the mod may not read the new layout correctly. It reads a key that moved as its own default, and it can misread a hotkey or another value that is now written as a name. To go back to an older version, first copy `HeadTracking.ini.pre-canonical` back over `HeadTracking.ini`, which restores the old file.
- The tracking mode (Page Up / Ctrl+Shift+G) and the yaw mode are saved to `HeadTracking.ini` when you change them and come back the next time the game starts. End still turns head tracking on or off for the current session only; the new `[General] EnableOnStartup` (default `true`) says whether it is on when the game starts.
- A new `HeadTracking.ini` toggles the yaw mode with Page Down / Ctrl+Shift+H. A converted file keeps the yaw mode key it had (the key its `YawModeKey` named, or Delete where it named none) with Ctrl+Shift+J.
- Hotkeys act only while the game is the window in front.
- `[Camera] ads_mode` is no longer read, and Insert / Ctrl+Shift+U no longer cycle ADS modes: head tracking stays on while you aim (e3f6d3f).
- A `YawModeKey` of Insert (0x2D), which v0.3.1 moved to Delete, toggles the yaw mode on Insert again (e3f6d3f). The files v0.1.0 and v0.2.0 wrote on their first start name Insert.
- A `[Camera]` section longer than 4094 bytes no longer stops head tracking from loading (e3f6d3f).

### Removed

- The key that toggled the reticle (`[Hotkeys] ReticleToggle`, Page Down / Ctrl+Shift+H). The gold hip-fire reticle can no longer be turned off.
- `[Feedback] ShowMessages`. It had no effect: this mod shows no messages in the game.

### Fixed

- Steam startup waits for the game window and camera hooks to be ready.
- Improved camera consistency for foliage, particles and other effects across
  the frame, and reduced reticle flicker. Occasional incorrect frames remain
  reported during head tracking.
- Corrected cloud movement during positional leaning.
- The hip-fire reticle and optional white ADS marker share one drawing pass.
- Live smoothing changes reach both rotation and position.
- Corrected the MIT licence text and consolidated third-party notices in
  `THIRD-PARTY-NOTICES.md`, included in the installer ZIP.

## [0.2.0] - 2026-08-20

### Added

- drop mod-side centring, split smoothing, always write a log

## [0.1.0] - 2026-08-03

### Added

- recenter head tracking when the tracker app requests it
