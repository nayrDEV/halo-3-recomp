# Halo 3 Recomp — Android arm64 pre-release 0.1.0

This is the first public **Android arm64 pre-release** of the Halo 3 static
recompilation base. It is a work-in-progress port, not a finished game.

## Highlights

- Arm64 Android APK with a Slint launcher.
- ISO and folder import; game data stays in app-private storage.
- 50/60 Hz, 360p/480p/720p/1080p output, anti-aliasing options.
- **BCn** texture mode: native BC1/BC2/BC3/BC5 upload, no ASTC conversion and
  no ASTC cache use.
- Optional ASTC 4x4 / 6x6 conversion with a persistent cache.
- On-screen Xbox 360 pad with touch-to-XInput mapping.
- Save export/import and diagnostic log export.
- Toggleable in-game performance overlay: FPS, CPU, GPU busy/clock, RAM,
  target size, GPU wait, draws, passes and pipelines.

## How this port was made

The Android arm64 bring-up and renderer work were done in an OpenCode
session using **DeepSeek V4.1 Flash** as the coding agent, starting from the
public PC recompilation base
[george0815/halo-3-recomp](https://github.com/george0815/halo-3-recomp).
The work included the Slint launcher and SDL3 Android activity, ISO/folder
import, internal-storage saves, touch input, BCn/ASTC texture paths, the
performance overlay and several runtime fixes. The recompiler still consumes
the user's own `default.xex`; no game code or assets are in this repository.

## Tested on

Samsung Galaxy A55 (`SM-A556E`, Exynos 1480, Xclipse 530, 8 GB, Android 16).

Verified: intro videos, main menu, Forge lobby, Last Resort gameplay, HUD,
audio and touch controls.

## Known limitations

- **Not 60 fps.** The menu is about 1 fps, with roughly 2,200–2,700 draws
  across 85–87 render passes per frame. Skipping all draws reaches only
  about 43 fps, and a 320×180 target still gives about 2.5 fps; the current
  bottleneck is draw/pass submission and synchronization. See
  [A55_RESULTS.md](A55_RESULTS.md).
- Some particle/effect rendering is still incorrect (`vpkd3d128` packing
  types 2/3 are not implemented).
- SMAA/FSR currently use the FXAA path.
- ASTC conversion is first-use only; there is no offline import-time
  conversion.
- BCn requires device support for BC1-BC5 sampling. Use ASTC if the GPU
  rejects them.
- Audio and gameplay are functional on the A55, but the port is still
  experimental.

## Continuing the port

The maintainer will continue on the `android-arm64` branch. The next work
is renderer performance and accuracy, not launcher features:

1. Batch or merge the menu's draw submissions and reduce the 85–87 render
   passes per frame (the measured 60 fps blocker).
2. Move per-draw system constants to a storage-buffer path.
3. Add a true internal render-target downscale plus an upscaling resolve.
4. Implement `vpkd3d128` packing types 2/3 and complete effect accuracy.
5. Replace the SMAA/FSR placeholders and add import-time ASTC conversion.

Contributions and experiments are welcome, especially draw batching,
Vulkan/SDL GPU submission work and A55/Adreno/Mali profiling.

## Requirements

You must supply your own legally dumped Halo 3 Xbox 360 files. No game data
is included in this repository or the release APK. The game code is
generated from `default.xex` on the build machine; the Android app imports
the disc data.

## Credits

- Original PC recompilation/runtime base:
  [george0815/halo-3-recomp](https://github.com/george0815/halo-3-recomp)
- [XenonRecomp](https://github.com/hedge-dev/XenonRecomp) by hedge-dev
- [Unleashed Recompiled](https://github.com/hedge-dev/UnleashedRecomp)
- [Xenia](https://github.com/xenia-project/xenia)
- SDL3, Slint, shaderc, FFmpeg/xmaframes, Arm astcenc, bcdec
- Halo 3 is © Microsoft / Bungie.

## Build from source

See [ANDROID.md](ANDROID.md) and the root [README](../README.md). The APK is
built with `tools/build_android.sh release` and signed locally. The release
asset for this pre-release is:

`Halo3Recomp-android-arm64-v0.1.0-prerelease.apk`

SHA-256 starts with `c4ce151d1a7d356e`.
