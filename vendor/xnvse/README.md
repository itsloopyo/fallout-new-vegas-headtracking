# xnvse (pinned, NOT vendored)

xNVSE (New Vegas Script Extender, community fork) is the script-extender loader
Fallout: NV head tracking depends on. It ships with **no upstream license**, so
we do not redistribute its binary - committing or bundling it would be
redistribution we have no license for.

Instead this mod uses the doctrine's vendoring exception: `scripts/install.cmd`
pins an exact version + SHA-256 and downloads xNVSE from the official GitHub
release at install time, verifying the hash before trusting it. Bump the pin
with `pixi run update-deps` (manual; commit the result). The release ZIP
carries no loader binary.

## Pinned snapshot

- Asset pattern: `_windows_7_legacy_version.zip`
- Version: `6.4.7`
- Download URL: https://github.com/xNVSE/NVSE/releases/download/6.4.7/xnvse_6_4_7_windows_7_legacy_version.zip
- SHA-256: `339ae6c8f9bdd6c90a4feeaae49f3b45f828849ad8f3fb1ccca533ebe895bdbc`
- Refreshed at: 2026-06-07T13:09:08.0906426+01:00

The download URL and SHA-256 above are mirrored into the CONFIG BLOCK of
`scripts/install.cmd` (`XNVSE_URL` / `XNVSE_SHA256`); this file is the
human-readable record, install.cmd is what runs.