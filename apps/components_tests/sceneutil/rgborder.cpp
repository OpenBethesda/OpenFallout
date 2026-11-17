#include <components/sceneutil/util.hpp>

#include <osg/Image>

#include <algorithm>
#include <iterator>

#include <gtest/gtest.h>

namespace
{
    // OpenGL ES uploads no BGR or BGRA pixels; the Metal build turns such images round when they load
    TEST(SceneUtilRgbOrderTest, a_bgr_image_becomes_rgb)
    {
        osg::ref_ptr<osg::Image> image = new osg::Image;
        image->allocateImage(2, 1, 1, GL_BGR, GL_UNSIGNED_BYTE);
        const unsigned char pixels[] = { 30, 20, 10, 60, 50, 40 };
        std::copy(std::begin(pixels), std::end(pixels), image->data());

        EXPECT_TRUE(SceneUtil::toRgbOrder(*image));
        EXPECT_EQ(image->getPixelFormat(), static_cast<GLenum>(GL_RGB));
        const unsigned char expected[] = { 10, 20, 30, 40, 50, 60 };
        EXPECT_TRUE(std::equal(std::begin(expected), std::end(expected), image->data()));
    }

    TEST(SceneUtilRgbOrderTest, every_mipmap_of_a_bgra_image_is_turned_round)
    {
        osg::ref_ptr<osg::Image> image = new osg::Image;
        image->allocateImage(2, 1, 1, GL_BGRA, GL_UNSIGNED_BYTE);
        // Level 0 is 2x1, level 1 is 1x1
        unsigned char* data = new unsigned char[12]{ 3, 2, 1, 4, 7, 6, 5, 8, 11, 10, 9, 12 };
        image->setImage(2, 1, 1, GL_BGRA, GL_BGRA, GL_UNSIGNED_BYTE, data, osg::Image::USE_NEW_DELETE);
        image->setMipmapLevels({ 8 });

        EXPECT_TRUE(SceneUtil::toRgbOrder(*image));
        EXPECT_EQ(image->getPixelFormat(), static_cast<GLenum>(GL_RGBA));
        EXPECT_EQ(image->getInternalTextureFormat(), static_cast<GLint>(GL_RGBA));
        const unsigned char expected[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12 };
        EXPECT_TRUE(std::equal(std::begin(expected), std::end(expected), image->data()));
    }

    TEST(SceneUtilRgbOrderTest, a_sized_internal_format_is_kept)
    {
        osg::ref_ptr<osg::Image> image = new osg::Image;
        image->allocateImage(1, 1, 1, GL_BGRA, GL_UNSIGNED_BYTE);
        image->setInternalTextureFormat(GL_RGBA8);
        EXPECT_TRUE(SceneUtil::toRgbOrder(*image));
        EXPECT_EQ(image->getInternalTextureFormat(), static_cast<GLint>(GL_RGBA8));
    }

    TEST(SceneUtilRgbOrderTest, rows_with_padding_keep_their_padding)
    {
        osg::ref_ptr<osg::Image> image = new osg::Image;
        image->allocateImage(1, 2, 1, GL_BGR, GL_UNSIGNED_BYTE, 4);
        ASSERT_EQ(image->getRowStepInBytes(), 4u);
        const unsigned char pixels[] = { 3, 2, 1, 99, 6, 5, 4, 99 };
        std::copy(std::begin(pixels), std::end(pixels), image->data());

        EXPECT_TRUE(SceneUtil::toRgbOrder(*image));
        const unsigned char expected[] = { 1, 2, 3, 99, 4, 5, 6, 99 };
        EXPECT_TRUE(std::equal(std::begin(expected), std::end(expected), image->data()));
    }

    TEST(SceneUtilRgbOrderTest, other_images_are_left_alone)
    {
        osg::ref_ptr<osg::Image> rgba = new osg::Image;
        rgba->allocateImage(1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE);
        EXPECT_FALSE(SceneUtil::toRgbOrder(*rgba));

        osg::ref_ptr<osg::Image> wide = new osg::Image;
        wide->allocateImage(1, 1, 1, GL_BGRA, GL_UNSIGNED_SHORT);
        EXPECT_FALSE(SceneUtil::toRgbOrder(*wide));
        EXPECT_EQ(wide->getPixelFormat(), static_cast<GLenum>(GL_BGRA));
    }
}
