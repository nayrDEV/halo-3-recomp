// Placeholder game library. The Android build replaces this with the real
// H3Recomp runtime once tools/recompile.sh has produced H3RecompLib/ppc.

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

extern "C" int SDL_main(int, char**)
{
    SDL_Init(SDL_INIT_VIDEO);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Halo 3 Recompiled",
        "The recompiled game is not in this APK.\n\n"
        "Put your own default.xex in private/ on the build machine and rebuild.",
        nullptr);
    return 0;
}
