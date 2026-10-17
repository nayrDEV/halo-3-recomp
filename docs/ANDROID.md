# Android arm64 port

Status: experimental. The launcher, game-file import, settings, SDL game
activity, touch overlay and the Android runtime layer build and run. The
actual game code is generated from **your own** `default.xex` at build time,
so the APK must be built on a PC; the phone only imports the disc data.

## What is in the APK

- `H3SlintActivity` — Slint launcher (Slint 1.17 C++, Skia): import ISO or
  folder, 50/60 Hz, 360p/480p/720p/1080p output, OFF/FXAA/SMAA/FSR, touch
  controls on/off, Play.
- `H3GameActivity` — SDL3 activity. SDL3 is linked statically into
  `libmain.so`.
- `libmain.so` — the recompiled runtime plus `H3RecompLib/ppc`. If no
  recompiled code exists yet, this is a small stub that explains what is
  missing instead of failing to load.
- `libslintmenu.so` + `libslint_cpp.so` — launcher only.

Game files are imported to:

```
/sdcard/Android/data/org.h3recomp/files/Halo3/
    game/          default.xex + maps/ (extracted from your ISO or folder)
    hdd/           cache partitions and the saved profile
    shadercache/   compiled SPIR-V
    settings.ini
    h3recomp.log   (optional)
```

The ISO importer is an XDVDFS (XGD2/XGD1/XGD3/plain XISO) reader ported from
`tools/xdvdfs_extract.py`; it copies the picked content to `game/`.

## Build

Requirements: Linux x86-64 (or another host with the NDK), Android SDK 35,
NDK r29, JDK 17, Rust (for Slint), CMake/Ninja, and your own legally dumped
Halo 3 files.

```sh
export ANDROID_HOME=$HOME/Android/Sdk
export ANDROID_NDK_ROOT=$ANDROID_HOME/ndk/29.0.14206865

# The public XenonRecomp checkout lacks the Image::dataSize used by
# H3Recomp/kernel/modules.cpp. Apply the small project patch once:
git -C thirdparty/XenonRecomp apply ../../tools/patches/xenonrecomp-halo3.patch

# 1. shaderc + FFmpeg 7.1 (xmaframes) for arm64
tools/build_shaderc_android.sh 30
tools/build_ffmpeg_android.sh 30

# 2. Your dump. private/default.xex is used for the recompiled code; the
#    modules (WavesLib etc.) need private/game/ as well.
#    See docs/DUMPING.md.
tools/recompile.sh release

# 3. The APK
./android/gradlew -p android :app:assembleDebug
# or: tools/build_android.sh debug
```

The first Gradle build compiles Slint and its Skia renderer for arm64 and
takes several minutes. `android/local.properties` needs:

```
sdk.dir=/absolute/path/to/Android/Sdk
```

The debug APK is at `android/app/build/outputs/apk/debug/app-debug.apk`.
Release builds are unsigned by default; add a signing config or use debug.

Without `private/default.xex`, the build still succeeds but `libmain.so` is
the stub. Add the XEX, rerun `tools/recompile.sh release`, and rebuild.

## Settings

`settings.ini` is written by the launcher and read by the game:

| Menu | INI key | Notes |
|---|---|---|
| 50/60 Hz | `frame_rate` | game simulation tick rate; 50 is not the shipped rate |
| 360/480/720/1080p | `output_height` | presentation target; the game's internal target stays at 720p for now |
| OFF/FXAA/SMAA/FSR | `anti_aliasing` | SMAA and FSR currently run the FXAA pass as a placeholder; selected output scaling is still applied |
| On-screen controls | `touch_controls` | overlay + touch-to-XInput mapping |

The Android menu writes these when Play is pressed. The desktop launcher's
existing settings keep working.

## Runtime port notes

- **Guest memory** (`H3Recomp/memory.cpp`): 4 GiB `MAP_NORESERVE`
  reservation, three aliased physical views backed by a memfd. Android uses
  `SYS_memfd_create` directly (Bionic's wrapper is newer than the API
  floor). There is a fallback reservation without `MAP_NORESERVE`.
- **Fibers** (`hooks/fiber_arm64.S`): Bionic has no
  `getcontext/makecontext/swapcontext`, so the runtime carries a small
  AArch64 context switch (callee-saved x19-x30, d8-d15, FPCR).
- **Platform guards**: the profiler, write watch and `execinfo` backtrace
  paths are disabled on Android; `content_hash_avx2.cpp` is x86-only and
  `ContentHash` falls back to XXH3's NEON/scalar path.
- **Audio**: SDL3 AAudio/OpenSL output; XMA uses the patched FFmpeg 7.1
  `xmaframes` decoder cross-built by `tools/build_ffmpeg_android.sh`.
- **Shaders**: shaderc is built from the NDK's bundled
  shaderc/glslang/SPIRV-Tools by `tools/build_shaderc_android.sh`.
- **Window**: Android is always fullscreen; the window's SDL renderer
  letterboxes the game frame and draws the touch pad.
- **Touch**: `H3Recomp/input/touch.cpp` draws the Xbox 360 pad and merges
  its buttons/sticks/triggers with the gamepad and keyboard state.

## Known limitations

- No full gameplay test has been run from this checkout: `private/` contains
  no game dump and no arm64 device was attached. The runtime compiles and
  links for arm64 with a synthetic function table; that only proves the
  platform layer, not the game.
- SMAA and FSR need their dedicated shaders; the UI options currently map to
  FXAA plus the output-resolution blit.
- The ISO importer copies the selected file/folder into app storage, so you
  need free space for both the source and the extracted copy.
- Mobile GPU compatibility (storage buffers in vertex shaders, compute
  memory export, driver bugs) and performance are unverified.
- The recompiled function table and module stubs are generated from the XEX
  and stay local (`.gitignore`); they are never committed.
