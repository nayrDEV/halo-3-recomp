package org.h3recomp;

import org.libsdl.app.SDLActivity;

/**
 * SDL3 game activity. SDL3 is linked statically into libmain.so, so only that
 * one library is loaded; it contains SDL_main and the JNI entry points.
 */
public class H3GameActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] { "main" };
    }
}
