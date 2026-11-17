#include <components/sdlutil/graphicscontext.hpp>

#include <SDL.h>
#include <SDL_hints.h>

#include <cstdlib>
#include <iterator>
#include <optional>
#include <string>

#ifdef OPENFALLOUT_METAL
#include <EGL/egl.h>
#include <GLES3/gl3.h>

#include <dlfcn.h>
#endif

#include <gtest/gtest.h>

namespace
{
    std::optional<std::string> environment(const char* name)
    {
        const char* value = std::getenv(name);
        return value ? std::optional<std::string>(value) : std::nullopt;
    }

    // Puts the environment and SDL's hints back as they were, so that no other test sees what one test chose
    struct SDLUtilGraphicsLibrariesTest : testing::Test
    {
        static constexpr const char* sNames[]
            = { "SDL_VIDEO_EGL_DRIVER", "SDL_VIDEO_GL_DRIVER", "ANGLE_DEFAULT_PLATFORM", SDL_HINT_OPENGL_ES_DRIVER };
        std::optional<std::string> mSaved[std::size(sNames)];

        SDLUtilGraphicsLibrariesTest()
        {
            for (std::size_t i = 0; i < std::size(sNames); ++i)
                mSaved[i] = environment(sNames[i]);
        }

        ~SDLUtilGraphicsLibrariesTest() override
        {
            for (std::size_t i = 0; i < std::size(sNames); ++i)
            {
                if (mSaved[i])
                    setenv(sNames[i], mSaved[i]->c_str(), 1);
                else
                    unsetenv(sNames[i]);
                SDL_ResetHint(sNames[i]);
            }
        }
    };

    // Chosen before SDL starts: SDL's threads must never see the environment change under them
    TEST_F(SDLUtilGraphicsLibrariesTest, are_chosen_without_starting_sdl)
    {
        SDLUtil::selectGraphicsLibraries();
        EXPECT_EQ(SDL_WasInit(0), 0u);
    }

#ifdef OPENFALLOUT_METAL
    std::string loadedFrom(const void* function)
    {
        Dl_info info;
        EXPECT_NE(dladdr(function, &info), 0);
        return info.dli_fname != nullptr ? info.dli_fname : "";
    }

    TEST_F(SDLUtilGraphicsLibrariesTest, the_metal_build_points_sdl_at_the_angle_it_is_linked_to)
    {
        setenv("SDL_VIDEO_EGL_DRIVER", "/elsewhere/libEGL.dylib", 1);
        unsetenv("ANGLE_DEFAULT_PLATFORM");
        SDLUtil::selectGraphicsLibraries();

        const std::string egl = loadedFrom(reinterpret_cast<const void*>(&eglGetProcAddress));
        const std::string gles = loadedFrom(reinterpret_cast<const void*>(&glGetString));
        EXPECT_EQ(environment("SDL_VIDEO_EGL_DRIVER"), egl);
        EXPECT_EQ(environment("SDL_VIDEO_GL_DRIVER"), gles);
        EXPECT_EQ(std::string(SDL_GetHint("SDL_VIDEO_EGL_DRIVER")), egl);
        EXPECT_EQ(std::string(SDL_GetHint("SDL_VIDEO_GL_DRIVER")), gles);
        EXPECT_EQ(environment("ANGLE_DEFAULT_PLATFORM"), "metal");
        EXPECT_TRUE(SDL_GetHintBoolean(SDL_HINT_OPENGL_ES_DRIVER, SDL_FALSE));
        // The one already loaded: SDL opening it again gets the same copy
        EXPECT_NE(dlopen(egl.c_str(), RTLD_NOLOAD | RTLD_LAZY), nullptr);
        EXPECT_NE(dlopen(gles.c_str(), RTLD_NOLOAD | RTLD_LAZY), nullptr);
    }
#else
    TEST_F(SDLUtilGraphicsLibrariesTest, the_opengl_build_leaves_sdl_its_defaults)
    {
        unsetenv("SDL_VIDEO_EGL_DRIVER");
        unsetenv("SDL_VIDEO_GL_DRIVER");
        unsetenv("ANGLE_DEFAULT_PLATFORM");
        unsetenv(SDL_HINT_OPENGL_ES_DRIVER);
        SDLUtil::selectGraphicsLibraries();
        EXPECT_EQ(environment("SDL_VIDEO_EGL_DRIVER"), std::nullopt);
        EXPECT_EQ(environment("SDL_VIDEO_GL_DRIVER"), std::nullopt);
        EXPECT_EQ(environment("ANGLE_DEFAULT_PLATFORM"), std::nullopt);
        EXPECT_EQ(SDL_GetHint(SDL_HINT_OPENGL_ES_DRIVER), nullptr);
    }
#endif
}
