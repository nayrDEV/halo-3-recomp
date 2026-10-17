cmake_minimum_required(VERSION 3.22)
project(H3Android LANGUAGES C CXX ASM)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(H3_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")

if (NOT ANDROID)
    message(FATAL_ERROR "android/CMakeLists.txt is only for Android builds")
endif()

# Slint's C++ Android integration builds the Rust runtime for the NDK ABI.
find_package(Slint QUIET)
if (NOT Slint_FOUND)
    include(FetchContent)
    FetchContent_Declare(
        Slint
        GIT_REPOSITORY https://github.com/slint-ui/slint.git
        GIT_TAG v1.17.1
        SOURCE_SUBDIR api/cpp
    )
    FetchContent_MakeAvailable(Slint)
endif()

# SDL3 is linked statically into libmain.so so the APK ships one game
# library and the Java SDLActivity only has to load "main".
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(${H3_ROOT}/thirdparty/SDL3 ${CMAKE_CURRENT_BINARY_DIR}/SDL3)

set(H3_SHADERC_DIR "${H3_ROOT}/build/android/shaderc-arm64" CACHE PATH "shaderc built for Android")
set(H3_FFMPEG_DIR "${H3_ROOT}/build/android/ffmpeg-arm64" CACHE PATH "FFmpeg built for Android")

add_subdirectory(menu)

# XenonUtils/disasm are static helpers; Slint's own targets above keep their
# own shared/static choices.
set(BUILD_SHARED_LIBS OFF)
add_subdirectory(game)
