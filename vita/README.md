# PS Vita port

This directory contains the HashLink C (HLC) runtime overlay, native Vita
bindings, and VitaSDK packaging configuration for Marble Blast Ultra. The
current milestone is an offline, single-player build at the Vita's native
960x544 resolution. Multiplayer, leaderboards, HTTP, and WebRTC are disabled.

## Prerequisites

- Haxe 4.3.x and the haxelibs used by the game.
- VitaSDK installed at `/usr/local/vitasdk`, including SDL2, vitashark,
  OpenAL Soft, libpng, libjpeg-turbo, libvorbis, and PCRE2. The exact vitaGL
  source used by this port is vendored and built with the game.
- A HashLink source checkout at the workspace-level `deps/hashlink`. The path
  can be changed with CMake's `HASHLINK_ROOT` cache variable.

The build script uses the workspace-level `.haxelib-repo` and refreshes the
HashLink development mapping automatically.

Run the complete HLC, cross-compile, SELF, and VPK build from the repository
root:

```sh
bash build-vita.sh
```

The installable package is written to `vita/build/mbhaxe-vita.vpk` (title ID
`MBHV00001`). Build artifacts and generated HLC sources are intentionally
ignored by Git.

Vita package metadata uses the dedicated `vita/assets/icon0.png`. It must stay
an opaque, indexed PNG-8 image at exactly 128x128 pixels; the build validates
these fields before packaging because the Vita application promoter rejects
ordinary RGB/RGBA icons with error `0x8010113D`.

## Runtime layout

- Packaged, case-preserving game assets are read from `app0:/data`.
- Settings, `console.log`, and replays are written below
  `ux0:data/MBHaxe-MBU`.
- Runtime shader compilation uses ShaccCg. The target Vita must have
  `ur0:data/libshacccg.suprx` installed (vitaShaRK spells this path
  `ur0:/data/libshacccg.suprx`).

The runtime in `vita/runtime` adapts the upstream HashLink standard library,
Heaps SDL/OpenGL bindings, OpenAL binding, GC allocation, threads, and UI/UV
stubs without modifying the external HashLink checkout. Networking remains a
separate porting milestone.

The build compiles the complete pinned vitaGL revision in
`vita/vendor/vitaGL-ed51b21`. This avoids mixing private vitaGL structures from
different revisions and includes both the upstream FBO/texture coherency fix
and the local bounds-safe GLSL preprocessor allocation. UBO, `textureSize`,
strict draw, and strict uniform support are enabled consistently for the whole
library.

Production builds use uncached GPU-facing RAM and two display scenes per
frame, matching the default-display to offscreen-target to default-display
sequence used by the Heaps renderer. The invasive per-draw GPU synchronization
can be enabled for diagnosis with CMake's
`MBHAXE_VITA_GPU_DIAGNOSTICS=ON`; it is disabled by default.

The Vita runtime also routes vitaGL's generic allocations through its private
graphics pools before falling back to newlib. Its GL binding tracks buffer
lifetimes and validates the allocator-backed capacity of index buffers before
uploads, preventing stale bindings from overwriting the audio/runtime heap.

The VPK is cross-compiled and archive-validated by the build; final graphics,
audio, input, suspend/resume, and performance behavior still require testing
on real Vita hardware or a suitable emulator.
