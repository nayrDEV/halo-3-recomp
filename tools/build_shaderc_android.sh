#!/usr/bin/env bash
# Builds the shaderc runtime that H3Recomp uses to turn Xenos GLSL into
# SPIR-V. The NDK ships the shaderc/glslang/SPIRV-Tools sources; this uses
# ndk-build to produce one combined static library for arm64.
#
# Usage: ANDROID_NDK_ROOT=... tools/build_shaderc_android.sh [api-level]
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
API="${1:-30}"
: "${ANDROID_NDK_ROOT:?Set ANDROID_NDK_ROOT to the NDK (r29 or newer)}"

SHADERC_SRC="$ANDROID_NDK_ROOT/sources/third_party/shaderc"
WORK="${TMPDIR:-/tmp}/h3-shaderc-build"
OUT="$ROOT/build/android/shaderc-arm64"
mkdir -p "$WORK/jni" "$OUT/include" "$OUT/lib"

cat > "$WORK/jni/Android.mk" <<EOF
LOCAL_PATH := \$(call my-dir)
include $SHADERC_SRC/Android.mk
EOF

cat > "$WORK/jni/Application.mk" <<EOF
APP_ABI := arm64-v8a
APP_PLATFORM := android-$API
APP_STL := c++_static
APP_CPPFLAGS := -std=c++17
EOF

"$ANDROID_NDK_ROOT/ndk-build" \
    NDK_PROJECT_PATH="$WORK" \
    APP_BUILD_SCRIPT="$WORK/jni/Android.mk" \
    NDK_APPLICATION_MK="$WORK/jni/Application.mk" \
    libshaderc_combined -j"$(nproc)" >/dev/null

cp "$WORK/libs/c++_static/arm64-v8a/libshaderc.a" "$OUT/lib/libshaderc.a"
cp -r "$SHADERC_SRC/libshaderc/include/shaderc" "$OUT/include/"
echo "Done: $OUT"
