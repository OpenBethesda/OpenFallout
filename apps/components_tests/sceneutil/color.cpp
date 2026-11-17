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

    // OpenGL ES makes render buffers (the multisampled targets of antialiasing) only of sized formats, where desktop
    // OpenGL also takes a base format; a multisample resolve in OpenGL ES also wants the render buffer's format to be
    // the one the texture it is resolved into has, which for a base format with byte pixels is the 8-bit sized format
    TEST(SceneUtilColorTest, base_colour_formats_become_their_8_bit_sized_formats)
    {
        EXPECT_EQ(SceneUtil::getSizedColorFormat(GL_RGBA), static_cast<GLenum>(GL_RGBA8));
        EXPECT_EQ(SceneUtil::getSizedColorFormat(GL_RGB), static_cast<GLenum>(GL_RGB8));
        EXPECT_EQ(SceneUtil::getSizedColorFormat(4), static_cast<GLenum>(GL_RGBA8));
        EXPECT_EQ(SceneUtil::getSizedColorFormat(3), static_cast<GLenum>(GL_RGB8));
    }

    TEST(SceneUtilColorTest, sized_colour_formats_are_kept)
    {
        EXPECT_EQ(SceneUtil::getSizedColorFormat(GL_RGBA8), static_cast<GLenum>(GL_RGBA8));
        EXPECT_EQ(SceneUtil::getSizedColorFormat(GL_RGB8), static_cast<GLenum>(GL_RGB8));
        EXPECT_EQ(SceneUtil::getSizedColorFormat(GL_RGBA16F), static_cast<GLenum>(GL_RGBA16F));
        EXPECT_EQ(SceneUtil::getSizedColorFormat(GL_R11F_G11F_B10F), static_cast<GLenum>(GL_R11F_G11F_B10F));
    }
}
