#include "graphicscontext.hpp"

#include <SDL_hints.h>
#include <SDL_video.h>

#include <components/debug/debuglog.hpp>
#include <components/sceneutil/gldialect.hpp>

#include <osg/GL>

#ifdef OPENFALLOUT_METAL
#include <EGL/egl.h>

#include <cstdlib>
#include <dlfcn.h>
#include <stdexcept>
#include <string>
#endif

namespace SDLUtil
{
#ifdef OPENFALLOUT_METAL
    namespace
    {
        // The file the loaded library that holds this function came from
        std::string loadedFrom(const void* function, const char* library)
        {
            Dl_info info;
            if (dladdr(function, &info) == 0 || info.dli_fname == nullptr)
                throw std::runtime_error(std::string("Cannot find where ") + library + " was loaded from");
            return info.dli_fname;
        }

        // SDL 2 reads the library names from the environment; sdl2-compat over SDL 3 takes them as hints. Both are set,
        // and set over anything already there, so that SDL loads the copy of ANGLE the engine is linked to.
        void setLibrary(const char* name, const std::string& path)
        {
            setenv(name, path.c_str(), 1);
            SDL_SetHintWithPriority(name, path.c_str(), SDL_HINT_OVERRIDE);
        }
    }

    void requestGraphicsContext()
    {
        const std::string egl = loadedFrom(reinterpret_cast<const void*>(&eglGetProcAddress), "libEGL");
        const std::string gles = loadedFrom(reinterpret_cast<const void*>(&glGetString), "libGLESv2");
        setLibrary("SDL_VIDEO_EGL_DRIVER", egl);
        setLibrary("SDL_VIDEO_GL_DRIVER", gles);
        // The ANGLE the project builds has only the Metal backend; this keeps any other build of it on Metal as well
        setenv("ANGLE_DEFAULT_PLATFORM", "metal", 1);
        SDL_SetHint(SDL_HINT_OPENGL_ES_DRIVER, "1");
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
        Log(Debug::Info) << "Graphics: OpenGL ES 3.0 through ANGLE's Metal backend, " << egl << ", " << gles;
    }
#else
    void requestGraphicsContext() {}
#endif

    void initGraphicsContext()
    {
        if constexpr (SceneUtil::GLDialect::sOpenGLES)
        {
            // A mesh without colours takes the "current" colour. In desktop OpenGL that starts white; OpenGL ES has
            // only the generic vertex attribute OpenSceneGraph gives the colour (location 3), which starts black.
            glVertexAttrib4f(3, 1.f, 1.f, 1.f, 1.f);
        }
    }
}
