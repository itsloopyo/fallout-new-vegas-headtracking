# Changelog

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
