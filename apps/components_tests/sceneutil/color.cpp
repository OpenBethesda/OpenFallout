#include <components/sceneutil/color.hpp>

#include <gtest/gtest.h>

namespace
{
    // An empty render target is made with the source format and type given here; OpenGL ES refuses the texture (and
    // so the frame buffer) when they do not match the internal format, where desktop OpenGL lets it pass
    TEST(SceneUtilColorTest, an_rgba_target_takes_rgba_pixels)
    {
        GLenum format = 0;
        GLenum type = 0;
        SceneUtil::getColorFormatSourceFormatAndType(GL_RGBA, format, type);
        EXPECT_EQ(format, static_cast<GLenum>(GL_RGBA));
        EXPECT_EQ(type, static_cast<GLenum>(GL_UNSIGNED_BYTE));
    }

    TEST(SceneUtilColorTest, an_rgb_target_takes_rgb_pixels)
    {
        GLenum format = 0;
        GLenum type = 0;
        SceneUtil::getColorFormatSourceFormatAndType(GL_RGB8, format, type);
        EXPECT_EQ(format, static_cast<GLenum>(GL_RGB));
        EXPECT_EQ(type, static_cast<GLenum>(GL_UNSIGNED_BYTE));
    }

    TEST(SceneUtilColorTest, a_floating_point_rgba_target_takes_float_rgba_pixels)
    {
        GLenum format = 0;
        GLenum type = 0;
        SceneUtil::getColorFormatSourceFormatAndType(GL_RGBA16F, format, type);
        EXPECT_EQ(format, static_cast<GLenum>(GL_RGBA));
        EXPECT_EQ(type, static_cast<GLenum>(GL_FLOAT));
    }
}
