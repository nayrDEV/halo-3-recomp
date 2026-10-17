#!/usr/bin/env bash
# Cross-builds the patched FFmpeg 7.1 for Android arm64. Same decoder as
# tools/build_ffmpeg.sh, installed to build/android/ffmpeg-arm64/.
#
# Usage: ANDROID_NDK_ROOT=... tools/build_ffmpeg_android.sh [api-level]
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VERSION="7.1"
SHA256="40973d44970dbc83ef302b0609f2e74982be2d85916dd2ee7472d30678a7abe6"
API="${1:-30}"

: "${ANDROID_NDK_ROOT:?Set ANDROID_NDK_ROOT to the NDK (r29 or newer)}"
TOOLCHAIN="$ANDROID_NDK_ROOT/toolchains/llvm/prebuilt/linux-x86_64"
CC="$TOOLCHAIN/bin/aarch64-linux-android${API}-clang"
CXX="$TOOLCHAIN/bin/aarch64-linux-android${API}-clang++"
AR="$TOOLCHAIN/bin/llvm-ar"
RANLIB="$TOOLCHAIN/bin/llvm-ranlib"
STRIP="$TOOLCHAIN/bin/llvm-strip"
SYSROOT="$TOOLCHAIN/sysroot"

WORK="$ROOT/build/android/ffmpeg-src"
PREFIX="$ROOT/build/android/ffmpeg-arm64"
TARBALL="$WORK/ffmpeg-$VERSION.tar.xz"

mkdir -p "$WORK"
if [[ ! -f "$TARBALL" ]]; then
    echo "== Downloading FFmpeg $VERSION"
    curl -fL -o "$TARBALL.part" "https://ffmpeg.org/releases/ffmpeg-$VERSION.tar.xz"
    mv "$TARBALL.part" "$TARBALL"
fi
echo "$SHA256  $TARBALL" | sha256sum -c --quiet

SRC="$WORK/ffmpeg-$VERSION"
rm -rf "$SRC"
tar -xf "$TARBALL" -C "$WORK"
patch -d "$SRC" -p1 --quiet < "$ROOT/tools/patches/ffmpeg-$VERSION-xmaframes.patch"

echo "== Configuring for arm64-v8a (API $API)"
(cd "$SRC" && ./configure --prefix="$PREFIX" \
    --target-os=android --arch=aarch64 --cpu=armv8-a \
    --enable-cross-compile --cc="$CC" --cxx="$CXX" --ar="$AR" --ranlib="$RANLIB" --strip="$STRIP" \
    --sysroot="$SYSROOT" \
    --disable-everything --enable-decoder=xmaframes \
    --disable-programs --disable-doc --disable-network --disable-autodetect \
    --disable-avdevice --disable-avformat --disable-avfilter --disable-swscale --disable-swresample \
    --enable-static --disable-shared --enable-pic --disable-asm >/dev/null)

echo "== Building"
make -C "$SRC" -j"$(nproc)" >/dev/null
make -C "$SRC" install >/dev/null
echo "Done: $PREFIX"
