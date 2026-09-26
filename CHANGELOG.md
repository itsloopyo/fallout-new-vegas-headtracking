# Changelog

## [Unreleased]

### Changed

- Head tracking stays on while you aim down sights (e3f6d3f). Raising the sights no
  longer moves the view, and the weapon stays on your aim. The three ADS modes are
  gone, and with them their key (Insert / Ctrl+Shift+U), the white aim marker and
  the head pose at ADS entry as a reference. `[Camera] ads_mode` is no longer read.
- While you lean, the first-person weapon is drawn from the eye position without
  the lean (e3f6d3f).
- Settings move to `CameraUnlock.ini` in the game folder, beside `FalloutNV.exe`. Earlier versions of the mod kept these settings in `HeadTracking.ini`, in the same folder. The first time this version starts and finds no `CameraUnlock.ini`, it reads your settings from `HeadTracking.ini` and writes them into `CameraUnlock.ini`. It never changes `HeadTracking.ini`, and does not read it again while `CameraUnlock.ini` exists.
- A setting that the defaults the README shows set to `default` is written as `default` when the value imported for it equals its default at that start, which is the value `Defaults.ini` gives it, or the built-in value where `Defaults.ini` gives none. It then follows `Defaults.ini`. Every other setting is written with the value imported for it.
- `RotationEnabled` and `PositionEnabled` are one setting here, the tracking mode, so both are written as `default` or neither is.
- Comments, and keys the mod never read, are not carried over. Nor are these, where your old file had them:
  - Reticle settings, and a key that toggled the reticle.
- Hotkeys are written as key names, and each hotkey lists every key that triggers it, the Ctrl+Shift chord included: `ToggleKey=End, Ctrl+Shift+Y`.
- Keys that moved or were renamed: `[Network] Port` is `UdpPort`, `[Hotkeys] Toggle` is `ToggleKey`, `CycleTrackingMode` is `CycleTrackingModeKey`, `[Camera] WorldSpaceYaw` is under `[General]`, `[Hotkeys] DebounceMs` is `[Input] HotkeyDebounceMs`, and `[GameState] InputBlockMode` is under `[Input]` and takes `Never`, `MenusOnly`, `AllDialogue` or `AllOverlays` in place of 0 to 3. True and false are written `true` and `false`.
- An older version of the mod reads `HeadTracking.ini` and never reads `CameraUnlock.ini`, so a setting you change after updating is not in `HeadTracking.ini`.
- Deleting only `CameraUnlock.ini` makes the next start read `HeadTracking.ini` again. To go back to the defaults, replace everything in `CameraUnlock.ini` with the defaults the README shows. Every setting they set to `default` then follows `Defaults.ini`.
- The tracking mode (Page Up / Ctrl+Shift+G) and the yaw mode are saved to `CameraUnlock.ini` when you change them and come back the next time the game starts. End still turns head tracking on or off for the current session only; the new `[General] EnableOnStartup` (default `true`) says whether it is on when the game starts.
- The yaw mode chord is Ctrl+Shift+H, the one the reticle toggle used. Ctrl+Shift+J no longer toggles the yaw mode. A new `CameraUnlock.ini` toggles it with Page Down / Ctrl+Shift+H, the built-in default. Settings imported from `HeadTracking.ini` keep the yaw mode key it had (the key its `YawModeKey` named, or Delete where it named none) with Ctrl+Shift+H beside it.
- Hotkeys act only while the game is the window in front.
- A `YawModeKey` of Insert (0x2D), which v0.3.1 moved to Delete, toggles the yaw mode on Insert again (e3f6d3f). The files v0.1.0 and v0.2.0 wrote on their first start name Insert.
- A `[Camera]` section longer than 4094 bytes no longer stops head tracking from loading (e3f6d3f).

### Added

- A setting set to `default` in `CameraUnlock.ini` takes its value from `Defaults.ini`, which every head tracking mod that keeps its settings in `CameraUnlock.ini` reads. Head tracking mods that keep their settings in another file do not read it, and neither do earlier versions of this mod. Writing a value in place of `default` changes that setting for this game only. When the mod saves a setting that a hotkey changed in game, it writes the new value in place of `default`, so that setting no longer follows `Defaults.ini` in this game until you set it to `default` again.
- `Defaults.ini` is `%AppData%\CameraUnlock\Defaults.ini` on Windows; `$XDG_CONFIG_HOME/CameraUnlock/Defaults.ini` on Linux, or `~/.config/CameraUnlock/Defaults.ini` where `XDG_CONFIG_HOME` is not set, under Wine and Proton too; and `~/Library/Application Support/CameraUnlock/Defaults.ini` on macOS. The mod's log, where it writes one, names the file it read.
- When the mod starts and finds no `Defaults.ini`, it creates one holding the built-in values, unless Windows runs the game as a packaged app. The mod never changes `Defaults.ini` after that.
- `[Position] CollisionEnabled` and `CollisionReleaseSmoothing` in `CameraUnlock.ini`. Both start as `default`, whose built-in values (`true` and `0.9`) are how 0.3.1 ran: leaning stops at walls, and once a wall is clear the lean eases back out over about 200 ms. `CollisionEnabled=false` lets a lean move the view through walls. Settings imported from `HeadTracking.ini` keep both as 0.3.1 ran them.

### Removed

- The key that toggled the reticle (`[Hotkeys] ReticleToggle`, Page Down / Ctrl+Shift+H). The gold hip-fire reticle can no longer be turned off. Where the mod hides the game's crosshair and then skips drawing its own (a view no wider than 800 or no taller than 600 pixels, or a frame not drawn to the back buffer), hip fire has no crosshair while head tracking is on, and turning the reticle off, which brought the game's crosshair back there, is no longer possible.
- `[Feedback] ShowMessages`. It had no effect: this mod shows no messages in the game.

## [0.3.1] - 2026-09-16

### Added

- load through a dsound proxy and track while aiming
- install and remove the proxy with install.cmd
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

### Fixed

- correct the MIT text, ship notices in both ZIPs, retitle the NVSE ABI headers
- mirror the vertical limit and restore the MIT grant
- re-sync THIRD-PARTY-NOTICES.md before cutting the tag
- seed the config instead of deploying it
- rewrite install.cmd MOD_VERSION from the canonical version
- remove obsolete NVSE installation tooling
- preserve originals across shim upgrades and uninstall failures
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
