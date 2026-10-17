# Android arm64 port

Status: experimental. The launcher, game-file import, settings, SDL game
activity, touch overlay and the Android runtime layer build and run. The
actual game code is generated from **your own** `default.xex` at build time,
so the APK must be built on a PC; the phone only imports the disc data.
The port has been booted on a Galaxy A55: intro videos, the main menu, the
Forge lobby and Last Resort gameplay render, with audio and the on-screen
pad working. Performance is still far from 60 fps; see
[A55_RESULTS.md](A55_RESULTS.md).

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

Game files are imported into the app's **private internal storage**
(`Context.getFilesDir()`), so the whole game lives with the app:

```
/data/user/0/org.h3recomp/files/Halo3/
    game/          default.xex + maps/ (extracted from your ISO or folder)
    hdd/           cache partitions and the saved profile
    shadercache/   compiled SPIR-V
    texturecache/  converted ASTC texture blobs
    settings.ini
    h3recomp.log   (optional)
```

The importer copies every file, not just the executable. Internal storage is
usually smaller than `/sdcard`, so check free space before importing a full
disc. Use `adb shell run-as org.h3recomp ls files/Halo3` on a debug build to
inspect it.

The ISO importer is an XDVDFS (XGD2/XGD1/XGD3/plain XISO) reader ported from
`tools/xdvdfs_extract.py`; it copies the picked content to `game/`.

### Saves

The PROFILE card has **SAVE FOLDER**, **EXPORT** and **IMPORT**:

- **EXPORT** opens Android's create-document picker and writes a `.zip` of
  `hdd/` (cache partitions and `profile_settings.bin`) plus `settings.ini`.
- **IMPORT** opens the document picker, unpacks a save archive into
  `hdd/`, and keeps the previous saves in `hdd.bak` as a fallback. Start the
  game afterwards so it reads the imported profile.
- Save archives are ordinary zips; there is no proprietary wrapper.

## Build

Requirements: Linux x86-64 (or another host with the NDK), Android SDK 35,
NDK r29, JDK 17, Rust (for Slint), CMake/Ninja, and your own legally dumped
Halo 3 files.

```sh
export ANDROID_HOME=$HOME/Android/Sdk
export ANDROID_NDK_ROOT=$ANDROID_HOME/ndk/29.0.14206865

# SDL3 and the ASTC encoder
git submodule update --init --recursive

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

The release build used for the A55 was produced and signed locally with the
Android debug keystore:

```sh
export JAVA_HOME=/usr/lib/jvm/java-17-openjdk
export ANDROID_HOME=$HOME/Android/Sdk
export ANDROID_NDK_ROOT=$ANDROID_HOME/ndk/29.0.14206865
tools/build_android.sh release

APK=android/app/build/outputs/apk/release/app-release-unsigned.apk
apksigner sign --ks ~/.android/debug.keystore \
  --ks-pass pass:android --key-pass pass:android \
  --out Halo3Recomp-android-arm64.apk "$APK"

adb install -r Halo3Recomp-android-arm64.apk
```

Without `private/default.xex`, the build still succeeds but `libmain.so` is
the stub. Add the XEX, rerun `tools/recompile.sh release`, and rebuild.

## Using the Android build

1. Install the APK and open **Halo 3 Recompiled**.
2. Import the disc image (ISO) or a folder containing `default.xex`. The
   complete game is copied into app-private storage; free space is needed
   for both the source and the copy.
3. Choose frame rate, output height, anti-aliasing, **BCn/ASTC** textures,
   and whether to show the on-screen pad and performance overlay.
4. Press **Play**.
5. Use the on-screen pad, a Bluetooth/USB gamepad, or a keyboard/mouse.
   The back gesture/button returns from the game activity to the launcher.
6. **EXPORT LOG** in the launcher writes the native log for diagnostics;
   **EXPORT/IMPORT** in the Profile card handle saves.

For the first launch expect a long shader warm-up. The ASTC cache is in
`/data/user/0/org.h3recomp/files/Halo3/texturecache/`; **BCn** does not use
it.

## Settings

`settings.ini` is written by the launcher and read by the game:

| Menu | INI key | Notes |
|---|---|---|
| 50/60 Hz | `frame_rate` | game simulation tick rate; 50 is not the shipped rate |
| 360/480/720/1080p | `output_height` | requests the guest video mode/presentation height; Halo still uses large internal render targets |
| OFF/FXAA/SMAA/FSR | `anti_aliasing` | SMAA and FSR currently run the FXAA pass as a placeholder; selected output scaling is still applied |
| On-screen controls | `touch_controls` | overlay + touch-to-XInput mapping |
| Performance overlay | `performance_overlay` | in-game FPS/CPU/GPU/RAM monitor |
| Textures | `astc_block` | 0 = **BCn** native BC1-BC5 upload with no conversion; 4 = ASTC 4x4; 6 = ASTC 6x6 (converted and cached) |

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
- **ASTC textures** (`H3Recomp/gpu/texture_astc.cpp`, `texture_cache.cpp`):
  BC1-BC5 textures are decoded with bcdec and re-encoded with Arm's
  astcenc (NEON CPU encoder) when the menu selects ASTC 4x4 or 6x6. The
  ASTC blob is stored in `texturecache/`, keyed by the guest content hash
  and texture layout, and uploaded as an `SDL_GPU` ASTC texture. The first
  use converts; later launches load the cache. 3D textures and
  uncompressed/dynamic formats stay in their native host format.
- **Touch**: `H3Recomp/input/touch.cpp` draws the Xbox 360 pad and merges
  its buttons/sticks/triggers with the gamepad and keyboard state.

## Known limitations

- **Performance is not at 60 fps.** On a Galaxy A55 the menu is around 1 fps
  and the intro is 6–10 fps. The menu submits ~2,200–2,700 draws across
  ~85–87 render passes per frame. A no-draw test reaches only ~43 fps, and
  a 320×180 internal target still gives ~2.5 fps, so the bottleneck is draw
  and pass submission/synchronization, not just resolution. See
  [A55_RESULTS.md](A55_RESULTS.md).
- SMAA and FSR need their dedicated shaders; the UI options currently map to
  FXAA plus the output-resolution blit.
- `vpkd3d128` packing types 2/3 are not implemented in generated code; some
  particle/effect data can be incorrect.
- BCn requires a GPU/driver that can sample BC1/BC2/BC3/BC5 textures. If the
  device rejects them, use an ASTC mode.
- ASTC conversion covers BC1-BC5 only. It is a CPU (NEON) encoder; there is
  no GPU compute encoder yet. RGBA/R8/R16 and 3D textures remain native.
  Conversion cost is paid on the first use of each texture, and the cache
  can grow to hundreds of MB; `texturecache/` is safe to delete.
- A Mali GPU that lacks BC but also lacks the chosen ASTC block size will
  need a different block size (or a future uncompressed fallback).
- The ISO importer copies the selected file/folder into app storage, so you
  need free space for both the source and the extracted copy.
- Mobile GPU compatibility (storage buffers in vertex shaders, compute
  memory export, driver bugs) and performance are unverified.
- The recompiled function table and module stubs are generated from the XEX
  and stay local (`.gitignore`); they are never committed.
