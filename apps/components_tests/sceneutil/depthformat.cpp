#include <components/sceneutil/depth.hpp>
#include <components/sceneutil/gldialect.hpp>

#include <string_view>
#include <vector>

#include <gtest/gtest.h>

namespace
{
    using Formats = std::vector<GLenum>;

    // Neither Apple's OpenGL nor ANGLE's OpenGL ES names the desktop float depth extensions
    bool noExtension(std::string_view)
    {
        return false;
    }

    TEST(SceneUtilDepthFormatTest, a_standard_depth_buffer_is_24_bit_depth_with_stencil)
    {
        EXPECT_EQ(SceneUtil::requestedDepthFormats(false, noExtension), Formats{ GL_DEPTH24_STENCIL8 });
        EXPECT_EQ(SceneUtil::requestedDepthFormats(false, [](std::string_view) { return true; }),
            Formats{ GL_DEPTH24_STENCIL8 });
    }

    // A reversed depth buffer gains its precision from float depth. OpenGL ES 3.0 has 32-bit float depth with stencil
    // in its core; desktop OpenGL has it by extension
    TEST(SceneUtilDepthFormatTest, a_reversed_depth_buffer_asks_for_float_depth_first)
    {
        const Formats withoutExtension = SceneUtil::requestedDepthFormats(true, noExtension);
        if constexpr (SceneUtil::GLDialect::sOpenGLES)
            EXPECT_EQ(withoutExtension, (Formats{ GL_DEPTH32F_STENCIL8, GL_DEPTH24_STENCIL8 }));
        else
            EXPECT_EQ(withoutExtension, Formats{ GL_DEPTH24_STENCIL8 });

        EXPECT_EQ(SceneUtil::requestedDepthFormats(
                      true, [](std::string_view name) { return name == "GL_ARB_depth_buffer_float"; }),
            (Formats{ GL_DEPTH32F_STENCIL8, GL_DEPTH24_STENCIL8 }));
    }

    TEST(SceneUtilDepthFormatTest, the_desktop_nvidia_float_depth_extension_is_asked_for_only_without_the_core_format)
    {
        const Formats formats = SceneUtil::requestedDepthFormats(
            true, [](std::string_view name) { return name == "GL_NV_depth_buffer_float"; });
        if constexpr (SceneUtil::GLDialect::sOpenGLES)
            EXPECT_EQ(formats, (Formats{ GL_DEPTH32F_STENCIL8, GL_DEPTH24_STENCIL8 }));
        else
            EXPECT_EQ(formats, (Formats{ GL_DEPTH32F_STENCIL8_NV, GL_DEPTH24_STENCIL8 }));
    }
}
