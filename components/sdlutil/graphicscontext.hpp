#ifndef OPENFALLOUT_COMPONENTS_SDLUTIL_GRAPHICSCONTEXT_H
#define OPENFALLOUT_COMPONENTS_SDLUTIL_GRAPHICSCONTEXT_H

namespace SDLUtil
{
    /// Chooses the graphics libraries SDL and ANGLE load. Call it once at start-up, before SDL_Init: it sets
    /// environment variables, which must not change while SDL's threads run. The OpenGL build keeps SDL's defaults. The
    /// Metal build points SDL at the copy of ANGLE the engine is linked to (in the packaged app Contents/Frameworks, in
    /// a development tree the one the build's run path names), so that nobody has to set an environment variable and
    /// only one copy of ANGLE is ever loaded, and keeps ANGLE on its Metal backend.
    void selectGraphicsLibraries();

    /// Asks SDL for the graphics context this build draws with. Call it once SDL's video is up and before the first
    /// window with SDL_WINDOW_OPENGL is made. The OpenGL build keeps SDL's default, desktop OpenGL. The Metal build
    /// asks for OpenGL ES 3.0 (from the libraries selectGraphicsLibraries chose).
    void requestGraphicsContext();

    /// Sets what a new context must start with to draw like desktop OpenGL. Call it with the new context current.
    void initGraphicsContext();
}

#endif
