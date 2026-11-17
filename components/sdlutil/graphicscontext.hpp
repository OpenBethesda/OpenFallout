#ifndef OPENFALLOUT_COMPONENTS_SDLUTIL_GRAPHICSCONTEXT_H
#define OPENFALLOUT_COMPONENTS_SDLUTIL_GRAPHICSCONTEXT_H

namespace SDLUtil
{
    /// Asks SDL for the graphics context this build draws with. Call it once SDL's video is up and before the first
    /// window with SDL_WINDOW_OPENGL is made. The OpenGL build keeps SDL's default, desktop OpenGL. The Metal build
    /// asks for OpenGL ES 3.0 from ANGLE's Metal backend, and points SDL at the copy of ANGLE the engine is linked to
    /// (in the packaged app Contents/Frameworks, in a development tree the one the build's rpath names), so that nobody
    /// has to set an environment variable and only one copy of ANGLE is ever loaded.
    void requestGraphicsContext();

    /// Sets what a new context must start with to draw like desktop OpenGL. Call it with the new context current.
    void initGraphicsContext();
}

#endif
