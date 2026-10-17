# Android game library. It links the recompiled game (H3RecompLib/ppc) and
# the runtime into libmain.so. Without generated code we keep a small SDL
# stub so the launcher's Play button still has something to start.

set(H3_PPC_DIR "${H3_ROOT}/H3RecompLib/ppc")
set(H3_HAVE_GAME FALSE)
if (EXISTS "${H3_PPC_DIR}/ppc_config.h")
    set(H3_HAVE_GAME TRUE)
endif()

if (NOT H3_HAVE_GAME OR NOT ANDROID_ABI STREQUAL "arm64-v8a")
    add_library(h3main SHARED game_stub.cpp)
    set_target_properties(h3main PROPERTIES OUTPUT_NAME main)
    target_link_libraries(h3main PRIVATE SDL3::SDL3-static)
    if (NOT H3_HAVE_GAME)
        message(STATUS "Android game: no H3RecompLib/ppc; building the SDL stub")
    else()
        message(STATUS "Android game: x86_64 uses the SDL stub; the real game is arm64-v8a")
    endif()
    return()
endif()

message(STATUS "Android game: building the recompiled runtime")

# XenonRecomp's headers and the XEX loader/patcher used by the runtime.
set(THIRDPARTY_ROOT "${H3_ROOT}/thirdparty/XenonRecomp/thirdparty")
add_subdirectory("${THIRDPARTY_ROOT}/disasm" "${CMAKE_CURRENT_BINARY_DIR}/disasm")
add_subdirectory("${H3_ROOT}/thirdparty/XenonRecomp/XenonUtils" "${CMAKE_CURRENT_BINARY_DIR}/XenonUtils")

# H3RecompLib (generated code + module libraries). It expects H3_PPC_DIR and
# XenonUtils to exist.
add_subdirectory("${H3_ROOT}/H3RecompLib" "${CMAKE_CURRENT_BINARY_DIR}/H3RecompLib")

add_library(H3Shader STATIC "${H3_ROOT}/H3Recomp/gpu/shader/translator.cpp")
target_include_directories(H3Shader PUBLIC "${H3_ROOT}/H3Recomp")

add_library(H3Config STATIC "${H3_ROOT}/H3Recomp/config/settings.cpp")
target_include_directories(H3Config PUBLIC "${H3_ROOT}/H3Recomp")

add_library(H3NativeCache STATIC "${H3_ROOT}/H3Native/cache/cache_file.cpp")
target_include_directories(H3NativeCache PUBLIC "${H3_ROOT}/H3Native")

set(H3_HAVE_TAG_LAYOUTS FALSE)
if (EXISTS "${H3_ROOT}/H3Native/tags/generated/tag_convert.cpp")
    set(H3_HAVE_TAG_LAYOUTS TRUE)
    add_library(H3NativeTags STATIC "${H3_ROOT}/H3Native/tags/generated/tag_convert.cpp")
    target_include_directories(H3NativeTags PUBLIC
        "${H3_ROOT}/H3Native"
        "${H3_ROOT}/thirdparty/XenonRecomp/XenonUtils")
    target_compile_definitions(H3NativeTags PUBLIC H3_HAVE_TAG_LAYOUTS=1)
    target_compile_options(H3NativeTags PRIVATE -O1)
else()
    add_library(H3NativeTags INTERFACE)
endif()

# Native replacements for recompiled functions (H3Native/*.cpp), except the
# cache reader and generated tag code, which are libraries of their own.
file(GLOB_RECURSE H3_NATIVE_SOURCES CONFIGURE_DEPENDS "${H3_ROOT}/H3Native/*.cpp")
list(FILTER H3_NATIVE_SOURCES EXCLUDE REGEX "/H3Native/(cache/cache_file\\.cpp|tags/generated/)")

set(H3_RUNTIME_SOURCES
    "${H3_ROOT}/H3Recomp/main.cpp"
    "${H3_ROOT}/H3Recomp/memory.cpp"
    "${H3_ROOT}/H3Recomp/gpu/command_processor.cpp"
    "${H3_ROOT}/H3Recomp/gpu/frame_capture.cpp"
    "${H3_ROOT}/H3Recomp/gpu/renderer.cpp"
    "${H3_ROOT}/H3Recomp/gpu/renderer/copy.cpp"
    "${H3_ROOT}/H3Recomp/gpu/renderer/debug.cpp"
    "${H3_ROOT}/H3Recomp/gpu/renderer/draw.cpp"
    "${H3_ROOT}/H3Recomp/gpu/renderer/memexport.cpp"
    "${H3_ROOT}/H3Recomp/gpu/renderer/pipelines.cpp"
    "${H3_ROOT}/H3Recomp/gpu/renderer/postprocess.cpp"
    "${H3_ROOT}/H3Recomp/gpu/renderer/queries.cpp"
    "${H3_ROOT}/H3Recomp/gpu/renderer/resolve.cpp"
    "${H3_ROOT}/H3Recomp/gpu/renderer/shaders.cpp"
    "${H3_ROOT}/H3Recomp/gpu/renderer/targets.cpp"
    "${H3_ROOT}/H3Recomp/gpu/renderer/textures.cpp"
    "${H3_ROOT}/H3Recomp/gpu/texture.cpp"
    "${H3_ROOT}/H3Recomp/gpu/texture_astc.cpp"
    "${H3_ROOT}/H3Recomp/gpu/texture_cache.cpp"
    "${H3_ROOT}/H3Recomp/input/input.cpp"
    "${H3_ROOT}/H3Recomp/input/touch.cpp"
    "${H3_ROOT}/H3Recomp/debug/first_calls.cpp"
    "${H3_ROOT}/H3Recomp/debug/profiler.cpp"
    "${H3_ROOT}/H3Recomp/debug/state_hash.cpp"
    "${H3_ROOT}/H3Recomp/debug/write_watch.cpp"
    "${H3_ROOT}/H3Recomp/hooks/block_copy.cpp"
    "${H3_ROOT}/H3Recomp/hooks/campaign.cpp"
    "${H3_ROOT}/H3Recomp/hooks/fibers.cpp"
    "${H3_ROOT}/H3Recomp/hooks/random_seed.cpp"
    "${H3_ROOT}/H3Recomp/hooks/tick_rate.cpp"
    "${H3_ROOT}/H3Recomp/hooks/xma.cpp"
    "${H3_ROOT}/H3Recomp/kernel/imports.cpp"
    "${H3_ROOT}/H3Recomp/kernel/module.cpp"
    "${H3_ROOT}/H3Recomp/kernel/modules.cpp"
    "${H3_ROOT}/H3Recomp/kernel/objects.cpp"
    "${H3_ROOT}/H3Recomp/kernel/thread.cpp"
    "${H3_ROOT}/H3Recomp/kernel/variables.cpp"
    "${H3_ROOT}/H3Recomp/kernel/vfs.cpp"
    "${H3_ROOT}/H3Recomp/kernel/xam.cpp"
    "${H3_ROOT}/H3Recomp/kernel/xam_user.cpp"
    "${H3_ROOT}/H3Recomp/kernel/xam_content.cpp"
    "${H3_ROOT}/H3Recomp/kernel/xam_notify.cpp"
    "${H3_ROOT}/H3Recomp/kernel/xnet.cpp"
    "${H3_ROOT}/H3Recomp/apu/audio_system.cpp"
    "${H3_ROOT}/H3Recomp/apu/xma_decoder.cpp"
    "${H3_ROOT}/H3Recomp/kernel/xboxkrnl.cpp"
    "${H3_ROOT}/H3Recomp/kernel/xboxkrnl_audio.cpp"
    "${H3_ROOT}/H3Recomp/kernel/xboxkrnl_crypt.cpp"
    "${H3_ROOT}/H3Recomp/kernel/xboxkrnl_io.cpp"
    "${H3_ROOT}/H3Recomp/kernel/xboxkrnl_memory.cpp"
    "${H3_ROOT}/H3Recomp/kernel/xboxkrnl_modules.cpp"
    "${H3_ROOT}/H3Recomp/kernel/xboxkrnl_objects.cpp"
    "${H3_ROOT}/H3Recomp/kernel/xboxkrnl_rtl.cpp"
    "${H3_ROOT}/H3Recomp/kernel/xboxkrnl_strings.cpp"
    "${H3_ROOT}/H3Recomp/kernel/xboxkrnl_threading.cpp"
    "${H3_ROOT}/H3Recomp/kernel/xboxkrnl_video.cpp"
    "${H3_ROOT}/H3Recomp/video/bink_view.cpp"
    "${H3_ROOT}/H3Recomp/video/window.cpp"
    ${H3_NATIVE_SOURCES}
    "${H3_PPC_DIR}/import_stubs.cpp"
    ${H3_MODULE_REGISTRY}
)

list(APPEND H3_RUNTIME_SOURCES "${H3_ROOT}/H3Recomp/hooks/fiber_arm64.S")

add_library(h3main SHARED ${H3_RUNTIME_SOURCES})
set_target_properties(h3main PROPERTIES OUTPUT_NAME main)

target_include_directories(h3main PRIVATE
    "${H3_ROOT}/H3Recomp"
    "${H3_PPC_DIR}"
    "${H3_ROOT}/thirdparty/XenonRecomp/XenonUtils"
    "${H3_ROOT}/thirdparty/XenonRecomp/thirdparty/xxHash"
    "${H3_ROOT}/thirdparty/bcdec"
    "${H3_ROOT}/thirdparty/astc-encoder/Source"
    "${H3_ROOT}/H3Native"
    "${H3_SHADERC_DIR}/include"
    "${H3_FFMPEG_DIR}/include"
)

if (H3_HAVE_TAG_LAYOUTS)
    target_compile_definitions(h3main PRIVATE H3_HAVE_TAG_LAYOUTS=1)
endif()

if (ANDROID_ABI STREQUAL "arm64-v8a")
    target_compile_definitions(h3main PRIVATE H3_HAVE_ASTC=1)
endif()

# shaderc, built by tools/build_shaderc_android.sh.
add_library(shaderc_android STATIC IMPORTED)
set_target_properties(shaderc_android PROPERTIES
    IMPORTED_LOCATION "${H3_SHADERC_DIR}/lib/libshaderc.a"
    INTERFACE_INCLUDE_DIRECTORIES "${H3_SHADERC_DIR}/include")

add_library(avcodec_android STATIC IMPORTED)
set_target_properties(avcodec_android PROPERTIES
    IMPORTED_LOCATION "${H3_FFMPEG_DIR}/lib/libavcodec.a"
    INTERFACE_INCLUDE_DIRECTORIES "${H3_FFMPEG_DIR}/include")
add_library(avutil_android STATIC IMPORTED)
set_target_properties(avutil_android PROPERTIES
    IMPORTED_LOCATION "${H3_FFMPEG_DIR}/lib/libavutil.a"
    INTERFACE_INCLUDE_DIRECTORIES "${H3_FFMPEG_DIR}/include")

target_link_libraries(h3main PRIVATE
    H3RecompLib
    H3Shader
    H3Config
    H3NativeCache
    H3NativeTags
    XenonUtils
    SDL3::SDL3-static
    shaderc_android
    avcodec_android
    avutil_android
    android
    log
    mediandk
    atomic
    m
)

if (ANDROID_ABI STREQUAL "arm64-v8a")
    target_link_libraries(h3main PRIVATE astcenc-neon-static)
endif()
