#include <components/sceneutil/screencapture.hpp>

#include <osg/Image>

#include <algorithm>
#include <iterator>

#include <gtest/gtest.h>

namespace
{
    // OpenGL ES reads the frame buffer back only as RGBA; a picture the engine keeps is RGB in both builds
    TEST(SceneUtilScreenCaptureTest, an_rgba_picture_loses_its_alpha)
    {
        osg::ref_ptr<osg::Image> rgba = new osg::Image;
        rgba->allocateImage(2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE);
        const unsigned char pixels[] = { 10, 20, 30, 0, 40, 50, 60, 128 };
        std::copy(std::begin(pixels), std::end(pixels), rgba->data());

        const osg::ref_ptr<const osg::Image> rgb = SceneUtil::withoutAlpha(*rgba);
        ASSERT_EQ(rgb->getPixelFormat(), static_cast<GLenum>(GL_RGB));
        ASSERT_EQ(rgb->s(), 2);
        ASSERT_EQ(rgb->t(), 1);
        const unsigned char expected[] = { 10, 20, 30, 40, 50, 60 };
        EXPECT_TRUE(std::equal(std::begin(expected), std::end(expected), rgb->data()));
    }

    TEST(SceneUtilScreenCaptureTest, a_picture_with_no_alpha_is_kept_as_it_is)
    {
        osg::ref_ptr<osg::Image> rgb = new osg::Image;
        rgb->allocateImage(3, 2, 1, GL_RGB, GL_UNSIGNED_BYTE);
        EXPECT_EQ(SceneUtil::withoutAlpha(*rgb).get(), rgb.get());
    }

    TEST(SceneUtilScreenCaptureTest, rows_are_copied_with_their_own_padding)
    {
        osg::ref_ptr<osg::Image> rgba = new osg::Image;
        rgba->allocateImage(1, 2, 1, GL_RGBA, GL_UNSIGNED_BYTE);
        rgba->data(0, 0)[0] = 1;
        rgba->data(0, 1)[0] = 2;
        const osg::ref_ptr<const osg::Image> rgb = SceneUtil::withoutAlpha(*rgba);
        EXPECT_EQ(rgb->data(0, 0)[0], 1);
        EXPECT_EQ(rgb->data(0, 1)[0], 2);
    }
}
