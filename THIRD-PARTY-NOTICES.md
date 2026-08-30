# Third-Party Notices

FalloutNVHeadTracking bundles, statically links, or credits the third-party components
listed below. Each remains the property of its authors and is used under its own
licence. Where a licence requires the copyright notice, the conditions and the
disclaimer to accompany a binary distribution, the full text is reproduced here
verbatim, and this file ships at the root of every release ZIP we publish.

Nothing in this repository is derived from, or redistributes any part of,
Fallout: New Vegas.

| Component | Version | Licence | How it ships |
|-----------|---------|---------|--------------|
| cameraunlock-core | 67a82e334bcf32979d17965eab4b0f37a48a6ad0 | MIT | Compiled into `HeadTracking.dll` |
| OpenTrack | n/a | ISC | Not bundled; UDP protocol interoperability only |

---

## cameraunlock-core

Git submodule at `cameraunlock-core/`, compiled into `HeadTracking.dll`. Our own code,
MIT licensed, reproduced here so the notices are complete.

- Pinned commit: `67a82e334bcf32979d17965eab4b0f37a48a6ad0`

```
MIT License

Copyright (c) 2026 CameraUnlock

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

---

## OpenTrack

Not bundled and not linked. This mod implements the OpenTrack UDP pose datagram
layout so that OpenTrack (https://github.com/opentrack/opentrack, ISC licence)
and compatible trackers can drive it. No OpenTrack code, headers or binaries
are copied, linked or redistributed, so its licence triggers no notice
obligation here. It is credited because the wire format is its work.

---

## Microsoft Visual C++ runtime

The release DLL is built with `/MT` (`MultiThreaded`), so the Visual C++
runtime is statically linked into `HeadTracking.dll` rather than shipped as a
separate redistributable. This is permitted for distributable code under the
Visual Studio licence terms that accompany the toolset used to build it. No
Microsoft binary, header, or source file is committed to this repository or
placed in a release ZIP. The Direct3D 9 and Winsock imports (`d3d9`, `ws2_32`)
resolve against libraries already present in Windows and are likewise not
redistributed.

---

## xNVSE (New Vegas Script Extender)

xNVSE is the script extender that loads this mod as a plugin. It is a separate
project by separate authors and it is **not redistributed by us in any form**.

- Upstream: https://github.com/xNVSE/NVSE
- Pinned version: 6.4.7 (recorded in `vendor/xnvse/README.md`, mirrored into
  the CONFIG BLOCK of `scripts/install.cmd` and into `launcher-manifest.json`)

**Licence status: none.** The upstream repository declares no licence: it
contains no `LICENSE`, `COPYING`, or equivalent file, and the GitHub API
reports its licence as null. Absent a grant, default copyright applies and we
have no permission to redistribute it. We therefore do not, and this is the
one loader in the catalogue that is deliberately not vendored:
`scripts/install.cmd` and the launcher manifest download the pinned release
directly from the authors' own GitHub release URL and verify its SHA-256
before use. An xNVSE the user already installed is detected and left untouched.
No xNVSE binary, source file, or archive is committed to this repository or
placed in any ZIP we publish.

**On `src/nvse_abi/`.** Those two headers are written by this project. They
declare only what a plugin must state to be callable across the binary
boundary: struct field order, enumerator values, and function-pointer
signatures. Those are constraints the ABI imposes on anyone who wants to
interoperate with it, not expression borrowed from the xNVSE authors, and they
contain no upstream implementation code. They are not a vendored copy of the
xNVSE SDK, and the directory is deliberately named so as not to suggest
otherwise.

---

## Fallout: New Vegas

Fallout: New Vegas and all related names, logos, characters, and marks are
trademarks of their respective owners, including Bethesda Softworks LLC. They
are used here only to identify the game this mod applies to, which is
nominative use and not a claim of any right in them. This project is an
unofficial, fan-made modification. It is not affiliated with, endorsed by, or
sponsored by Obsidian Entertainment, Bethesda Softworks, or any other rights
holder. It redistributes no game code, no game assets, and no proprietary
DLLs, and it requires a legitimately purchased copy of the game. It contains
no DRM circumvention and no licence-check bypass. The engine structure offsets
and function addresses referenced in `src/game_offsets.h` and elsewhere in
`src/` were derived by the authors through independent analysis of a
legitimately owned copy. They are factual measurements recorded as numbers. No
decompiled, disassembled, or otherwise reproduced game code is stored in this
repository.
