#!/usr/bin/env bash
# Builds the Android arm64 APK. The game code is generated from the user's
# own default.xex on the build machine; it is never downloaded.
#
# Usage:
#   export ANDROID_HOME=$HOME/Android/Sdk
#   export ANDROID_NDK_ROOT=$ANDROID_HOME/ndk/29.0.14206865
#   tools/build_android.sh [debug|release]
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VARIANT="${1:-debug}"
API="${ANDROID_API:-30}"

: "${ANDROID_HOME:?Set ANDROID_HOME to the Android SDK}"
: "${ANDROID_NDK_ROOT:?Set ANDROID_NDK_ROOT to the NDK (r29 or newer)}"

if [[ ! -f "$ROOT/private/default.xex" && ! -f "$ROOT/private/default_patched.xex" ]]; then
    echo "warning: private/default.xex is missing." >&2
    echo "The APK will contain the Slint menu and the SDL stub, not the game." >&2
    echo "See docs/DUMPING.md and docs/ANDROID.md." >&2
fi

if ! grep -q 'dataSize' "$ROOT/thirdparty/XenonRecomp/XenonUtils/image.h"; then
    git -C "$ROOT/thirdparty/XenonRecomp" apply "$ROOT/tools/patches/xenonrecomp-halo3.patch"
fi

if [[ ! -f "$ROOT/build/android/shaderc-arm64/lib/libshaderc.a" ]]; then
    "$ROOT/tools/build_shaderc_android.sh" "$API"
fi
if [[ ! -f "$ROOT/build/android/ffmpeg-arm64/lib/libavcodec.a" ]]; then
    "$ROOT/tools/build_ffmpeg_android.sh" "$API"
fi

if [[ -f "$ROOT/private/default.xex" && ! -f "$ROOT/H3RecompLib/ppc/ppc_config.h" ]]; then
    "$ROOT/tools/recompile.sh" release
fi

rustup target add aarch64-linux-android >/dev/null 2>&1 || true

cd "$ROOT/android"
if [[ "$VARIANT" == "release" ]]; then
    ./gradlew :app:assembleRelease
    echo "APK: android/app/build/outputs/apk/release/app-release-unsigned.apk"
else
    ./gradlew :app:assembleDebug
    echo "APK: android/app/build/outputs/apk/debug/app-debug.apk"
fi
