# Changelog

## [0.2.0] - 2026-08-20

### Added

- drop mod-side centring, split smoothing, always write a log

## [Unreleased]

### Fixed

- `LICENSE` was not the MIT licence. The grant clause read "and to furnish to
  do so" where MIT reads "and to permit persons to whom the Software is
  furnished to do so", so every release so far shipped a mangled text while the
  README, the launcher manifest and the notices all declared MIT. Restored the
  canonical wording.
- Both release ZIPs now carry `THIRD-PARTY-NOTICES.md`, and the Nexus ZIP now
  carries `LICENSE` and `README.md` as well. The Nexus ZIP previously shipped
  as a bare DLL and INI with no licence file of any kind, which does not
  satisfy the MIT notice obligation of `cameraunlock-core`, a separate
  copyright holder statically linked into `HeadTracking.dll`. The packager now
  throws on a missing notice file instead of skipping it silently, and
  `validate-release.ps1` gates on the same files before a tag is cut.

### Changed

- Consolidated `THIRD_PARTY_LICENSES.md` into `THIRD-PARTY-NOTICES.md`. The old
  file named licences without reproducing them and asserted an xNVSE licence
  ("zlib for `common/`") that upstream does not declare; the xNVSE repository
  carries no `LICENSE` file and the GitHub API reports its licence as null.
  The notices now reproduce the full MIT text of `cameraunlock-core`, state the
  verified xNVSE position, and record that the statically linked Visual C++
  runtime is covered by the Visual Studio distributable-code terms.
- Moved the plugin ABI declarations from `nvse/nvse/` to `src/nvse_abi/` and
  rewrote their file headers. The old path mirrored the upstream xNVSE
  repository layout and the headers described themselves as "based on xNVSE
  source", which read as a vendored copy of an unlicensed project. They are our
  own declarations of struct field order, enumerator values and function-pointer
  signatures, which is what ABI interoperability requires and contains no
  upstream implementation code. Nothing about the build output changes.


### Changed

- The mod now always writes `Data/NVSE/Plugins/HeadTracking.log`. Previously
  file logging was a compile-time option that shipped disabled, so a
  released build produced no log at all and a "no head tracking" report
  could not be diagnosed. The log records the DLL attaching, the NVSE
  version check, config load, UDP bind and port, the first packet received
  and its sender, the camera mode, and whether the D3D9 hook installed.
  It truncates on every launch and keeps one previous run as
  `HeadTracking.prev.log`.
- The mod no longer keeps a centre of its own and applies the tracker pose as
  absolute. Every tracker app centres itself, so a mod-side centre sat in series
  with the tracker's own and the two drifted apart: pressing Center in opentrack
  left the view parked at the negated drift. Centre the view in your tracker app
  instead.
- Removed the recenter hotkey (`Home` / `Ctrl+Shift+T`) and the `[Hotkeys]
  Recenter` INI key, along with the automatic recentre that fired when aiming
  down sights and the response to a tracker app's centre request.
- Replaced `[Smoothing] Amount` with `[Smoothing] LocalSmoothing` (default
  `0.0`) and `[Smoothing] RemoteSmoothing` (default `0.15`). The mod picks
  between them per connection from the packet's source address, and each
  covers rotation and position together.
- Removed the hidden 0.15 baseline smoothing floor. A tracker running on the
  same machine now gets zero-latency tracking by default instead of being
  silently smoothed against the user's setting.

### Fixed

- Every reason `Config::Load` can fail now names itself in `HeadTracking.log`,
  as does the retired-`Smoothing`-key warning. They were routed only to the
  in-game console printer, which is null in a shipping build, so a bad value
  such as `Sensitivity Yaw=0` left the mod dormant with nothing in the log but
  "failed to load config - head tracking is inactive"
- The log is no longer skipped when the plugin DLL path is longer than 260
  characters. The path lookup grows its buffer, and a genuine failure to
  resolve the path is reported to the debugger output rather than returning
  in silence
- Editing a smoothing value while the game is running now reaches position
  tracking as well as rotation. The live config reload only pushed the new
  values onto the camera controller, so position kept whatever was read at
  startup and the two halves of the pipeline ran at different smoothing for
  the rest of the session.

## [0.1.0] - 2026-08-03

### Added

- recenter head tracking when the tracker app requests it
