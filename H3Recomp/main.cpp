// Entry point of the runtime. Sets up the host side (guest memory, window,
// GPU and XMA workers, the folders that stand in for the 360's disc and hard
// drive), loads default.xex, and runs the game's entry point on its own
// thread while the host main thread runs the SDL event loop.
//
// See docs/ARCHITECTURE.md for how the pieces fit together.

#include <ppc_recomp_shared.h>

#include "memory.h"
#include "config/settings.h"
#include "gpu/command_processor.h"
#include "apu/xma_decoder.h"
#include "gpu/renderer.h"
#include "kernel/module.h"
#include "kernel/modules.h"
#include "kernel/thread.h"
#include "kernel/vfs.h"
#include "video/window.h"

#include <SDL3/SDL.h>
#ifdef __ANDROID__
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_system.h>
#include <android/log.h>
#endif

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>

#include <pthread.h>
#include <unistd.h>

// The recompiler's function lookup table lives right after the image and
// must fit inside the 4 GiB guest range.
static_assert(PPC_IMAGE_BASE + PPC_IMAGE_SIZE + PPC_CODE_SIZE * 2 <= h3::kGuestMemorySize);

namespace
{
    // Halo 3's XEX header asks for a 64 KiB main thread stack. We give it at
    // least this much: the headroom costs nothing (pages are committed
    // lazily), and an overflow would silently corrupt neighbouring memory.
    constexpr uint32_t kMinMainStackSize = 1024 * 1024;

    // The 360 hard drive has two cache partitions (cache0:, cache1:) that
    // Halo 3 streams map data into.
    constexpr int kCachePartitionCount = 2;

#ifdef __ANDROID__
    // Android discards a plain process's stdout/stderr. Send both through a
    // pipe to logcat so the in-app EXPORT LOG button can capture boot
    // messages even when the game never reaches its own file log.
    void LogcatReader(int fd)
    {
        FILE* stream = fdopen(fd, "r");
        if (!stream)
            return;
        char line[1024];
        while (fgets(line, sizeof(line), stream))
        {
            size_t length = strlen(line);
            while (length && (line[length - 1] == '\n' || line[length - 1] == '\r'))
                line[--length] = 0;
            if (length)
                __android_log_write(ANDROID_LOG_INFO, "H3Recomp", line);
        }
        fclose(stream);
    }

    void RedirectStdioToLogcat()
    {
        int fds[2];
        if (pipe(fds) != 0)
            return;
        dup2(fds[1], STDOUT_FILENO);
        dup2(fds[1], STDERR_FILENO);
        close(fds[1]);
        setvbuf(stdout, nullptr, _IOLBF, 0);
        setvbuf(stderr, nullptr, _IONBF, 0);
        std::thread(LogcatReader, fds[0]).detach();
    }
#endif
}

// Usage: H3Recomp [path to default.xex]   (default: private/default.xex)
int main(int argc, char* argv[])
{
#ifdef __ANDROID__
    RedirectStdioToLogcat();
    // SDLActivity owns the Android context. Initializing video here makes
    // SDL_GetAndroidExternalStoragePath() usable before CreateMainWindow.
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMEPAD);

    const char* storage = SDL_GetAndroidInternalStoragePath();
    std::filesystem::path root = std::filesystem::path(storage ? storage : ".") / "Halo3";
    std::filesystem::path xexPath = root / "game" / "default.xex";
    std::filesystem::path contentRoot = root / "game";
    std::filesystem::path userRoot = root / "hdd";
    std::filesystem::path settingsPath = root / h3::config::kFileName;
    std::filesystem::path logPath = root / h3::config::kLogFileName;
    std::filesystem::path shaderCache = root / "shadercache";
    std::error_code error;
    std::filesystem::create_directories(contentRoot, error);
    std::filesystem::create_directories(userRoot, error);
#else
    // Point this at default_patched.xex when recompiling with a title update.
    std::filesystem::path xexPath = argc > 1 ? argv[1] : "private/default.xex";

    // game:\ is the disc contents: private/game/ next to the XEX (see
    // private/README.md), or the XEX's own directory if it's in there. The
    // hard drive (cache partitions, saves) lives in hdd/ next to the XEX.
    std::filesystem::path xexDir = xexPath.parent_path();
    std::filesystem::path contentRoot = std::filesystem::is_directory(xexDir / "game") ? xexDir / "game" : xexDir;
    std::filesystem::path userRoot = xexDir / "hdd";
    std::filesystem::path settingsPath = xexDir / h3::config::kFileName;
    std::filesystem::path logPath = xexDir / h3::config::kLogFileName;
    std::filesystem::path shaderCache = xexDir / "shadercache";
    std::filesystem::create_directories(userRoot);
#endif
    h3::kernel::SetContentRoot(contentRoot);
    h3::kernel::SetUserDataRoot(userRoot);

    // The launcher's settings (window, render scale, profile, controls),
    // next to the XEX. Without the file everything is at its default.
    {
        h3::config::Settings settings;
        bool loaded = h3::config::Load(settingsPath, settings);
        // Debug options: the log file, then the H3_* switches (before
        // anything reads them; a variable set in the real environment wins).
        if (settings.debug.logToFile && std::getenv("H3_FORCE_LOGCAT") == nullptr)
        {
            if (freopen(logPath.string().c_str(), "w", stdout))
            {
                dup2(fileno(stdout), fileno(stderr));
                setvbuf(stdout, nullptr, _IOLBF, 0);
            }
        }
        for (const auto& [name, value] : settings.debug.environment)
            setenv(name.c_str(), value.c_str(), 0);
        if (loaded)
            printf("Settings: %s\n", settingsPath.string().c_str());
        h3::config::SetActive(settings);
    }

    // On the console Halo 3 reads its cache header from the raw partition,
    // mounts its own cache file system and links cache0:/cache1: to it. We
    // don't emulate the raw partition, so back the caches with host folders
    // and link them up front.
    for (int i = 0; i < kCachePartitionCount; i++)
    {
        std::string cache = "Cache" + std::to_string(i);
        std::filesystem::create_directories(userRoot / cache);
        h3::kernel::AddSymbolicLink("cache" + std::to_string(i) + ":", "\\Device\\Harddisk0\\" + cache);
    }

    // Compiled host shaders and converted ASTC textures, kept between runs.
    h3::gpu::renderer::SetShaderCacheDirectory(shaderCache);
    h3::gpu::renderer::SetTextureCacheDirectory(shaderCache.parent_path() / "texturecache");

    if (!h3::InitGuestMemory())
    {
        fprintf(stderr, "Failed to reserve guest memory\n");
        return 1;
    }

    // The window owns the SDL_GPU device the renderer draws with, so it comes
    // before the GPU. The XMA decoder must be ready before the game first
    // reads its registers.
    if (!h3::video::CreateMainWindow())
        return 1;

    h3::gpu::Initialize();
    h3::apu::InitXma();

    if (!h3::kernel::LoadExecutable(xexPath.string().c_str()))
        return 1;

    // The guest's main thread runs beside the window's event loop, which SDL
    // wants on the host main thread.
    std::thread([]
    {
        pthread_setname_np(pthread_self(), "H3GuestMain");
        PPCContext ctx{};
        h3::kernel::AdoptHostThread(ctx, std::max(h3::kernel::g_module.stackSize, kMinMainStackSize));

#if !defined(__ANDROID__)
        if (std::getenv("H3_WATCHDOG") != nullptr)
        {
            std::thread([] {
                std::this_thread::sleep_for(std::chrono::seconds(8));
                h3::kernel::DumpAllHostThreads();
            }).detach();
        }
#endif
        printf("Entering guest at _xstart\n");
        fflush(stdout);
        _xstart(ctx, h3::g_base);
        printf("Guest returned\n");
        h3::video::RequestQuit(0);
    }).detach();

    if (std::getenv("H3_DUMP_THREADS") != nullptr)
    {
        std::thread([] {
            pthread_setname_np(pthread_self(), "H3Watchdog");
            for (int i = 0; i < 4; i++)
            {
                std::this_thread::sleep_for(std::chrono::seconds(45));
                h3::kernel::DumpAllHostThreads();
            }
        }).detach();
    }

    // Guest threads can't be stopped cleanly, so leave without running
    // destructors once the window closes.
    int exitCode = h3::video::RunEventLoop();
    fflush(stdout);
    fflush(stderr);
    std::_Exit(exitCode);
}
