# Third-Party Notices

## CameraUnlock Core

- **Version:** submodule pinned commit (see `cameraunlock-core/`)
- **License:** MIT
- **Upstream:** https://github.com/itsloopyo/cameraunlock-core
- **Usage:** Shared head-tracking primitives (UDP receiver, pose interpolation, smoothing, projection math) consumed as a git submodule and compiled into this mod's DLL.
- **Bundled:** yes (compiled into the mod DLL shipped in the release ZIP).

---

## xNVSE (New Vegas Script Extender)

- **Version:** pinned in `install.cmd` CONFIG BLOCK (see `vendor/xnvse/README.md` for the human-readable record)
- **License:** No unified project-wide license (zlib for `common/`, varies elsewhere)
- **Upstream:** https://github.com/xNVSE/NVSE
- **Usage:** Required script extender that loads this mod as an NVSE plugin. Plugin API headers in this repo (`nvse/nvse/PluginAPI.h`, `nvse/nvse/GameAPI.h`) are hand-written re-implementations of the struct layouts needed to interface with the script extender, based on the publicly available xNVSE source.
- **Bundled:** no. `install.cmd` downloads the pinned xNVSE release from GitHub and verifies its SHA-256 at install time. An existing user-installed xNVSE is detected and left intact.

---

## OpenTrack

- **Version:** protocol only (no code bundled)
- **License:** ISC
- **Upstream:** https://github.com/opentrack/opentrack
- **Usage:** UDP wire format (3 doubles position + 3 doubles rotation, little-endian) consumed by the receiver. No OpenTrack code is linked or shipped.
- **Bundled:** no.

---
