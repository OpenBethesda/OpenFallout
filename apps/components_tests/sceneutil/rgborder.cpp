#include <components/sceneutil/util.hpp>

#include <osg/Image>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <vector>

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

    // An image may say its rows are longer than its width (the row length); the pixels past the width are not its own
    TEST(SceneUtilRgbOrderTest, rows_longer_than_the_image_keep_their_extra_pixels)
    {
        osg::ref_ptr<osg::Image> image = new osg::Image;
        unsigned char* data = new unsigned char[12]{ 3, 2, 1, 99, 98, 97, 6, 5, 4, 96, 95, 94 };
        image->setImage(1, 2, 1, GL_RGB, GL_BGR, GL_UNSIGNED_BYTE, data, osg::Image::USE_NEW_DELETE, 1, 2);
        ASSERT_EQ(image->getRowStepInBytes(), 6u);

        EXPECT_TRUE(SceneUtil::toRgbOrder(*image));
        const unsigned char expected[] = { 1, 2, 3, 99, 98, 97, 4, 5, 6, 96, 95, 94 };
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

    // Uncompressed DDS files also come as 16-bit BGRA packings and as 4-byte pixels over an RGB internal format, which
    // OpenGL ES does not upload; the Metal build makes them 8-bit RGBA that samples as desktop OpenGL would
    osg::ref_ptr<osg::Image> packedImage(int width, int height, GLenum type, GLint internalFormat,
        const std::vector<std::uint16_t>& pixels, const std::vector<unsigned int>& mipmaps = {})
    {
        osg::ref_ptr<osg::Image> image = new osg::Image;
        unsigned char* data = new unsigned char[pixels.size() * 2];
        std::memcpy(data, pixels.data(), pixels.size() * 2);
        image->setImage(width, height, 1, internalFormat, GL_BGRA, type, data, osg::Image::USE_NEW_DELETE, 1);
        image->setMipmapLevels(mipmaps);
        return image;
    }

    TEST(SceneUtilOpenGLESFormatTest, a_1_5_5_5_image_becomes_rgba8)
    {
        // Blue full, green zero, red half, alpha set; then everything off
        const std::uint16_t first = 0x1f | (0x10 << 10) | 0x8000;
        osg::ref_ptr<osg::Image> image = packedImage(2, 1, GL_UNSIGNED_SHORT_1_5_5_5_REV, GL_RGBA, { first, 0 });

        EXPECT_TRUE(SceneUtil::toOpenGLESFormat(*image));
        EXPECT_EQ(image->getPixelFormat(), static_cast<GLenum>(GL_RGBA));
        EXPECT_EQ(image->getDataType(), static_cast<GLenum>(GL_UNSIGNED_BYTE));
        EXPECT_EQ(image->getInternalTextureFormat(), static_cast<GLint>(GL_RGBA));
        const unsigned char expected[] = { 132, 0, 255, 255, 0, 0, 0, 0 };
        EXPECT_TRUE(std::equal(std::begin(expected), std::end(expected), image->data()));
    }

    TEST(SceneUtilOpenGLESFormatTest, a_4_4_4_4_image_without_alpha_becomes_opaque_rgba8_with_its_mipmaps)
    {
        // 2x1 then 1x1; the internal format says RGB, so the alpha bits are not used
        const std::uint16_t red = 0x0f00;
        const std::uint16_t green = 0x00f0;
        const std::uint16_t blue = 0x000f;
        osg::ref_ptr<osg::Image> image
            = packedImage(2, 1, GL_UNSIGNED_SHORT_4_4_4_4_REV, GL_RGB, { red, green, blue }, { 4 });

        EXPECT_TRUE(SceneUtil::toOpenGLESFormat(*image));
        EXPECT_EQ(image->getInternalTextureFormat(), static_cast<GLint>(GL_RGBA));
        ASSERT_EQ(image->getNumMipmapLevels(), 2u);
        const unsigned char level0[] = { 255, 0, 0, 255, 0, 255, 0, 255 };
        EXPECT_TRUE(std::equal(std::begin(level0), std::end(level0), image->getMipmapData(0)));
        const unsigned char level1[] = { 0, 0, 255, 255 };
        EXPECT_TRUE(std::equal(std::begin(level1), std::end(level1), image->getMipmapData(1)));
    }

    TEST(SceneUtilOpenGLESFormatTest, a_packed_image_with_rows_longer_than_its_width_becomes_rgba8_of_its_own_pixels)
    {
        // 1x2, each row 2 pixels long: red, then a pixel that is not the image's, then blue and another
        const std::uint16_t red = 0xff00;
        const std::uint16_t blue = 0xf00f;
        const std::uint16_t notOwn = 0x0ff0;
        osg::ref_ptr<osg::Image> image = new osg::Image;
        unsigned char* data = new unsigned char[8];
        const std::uint16_t pixels[] = { red, notOwn, blue, notOwn };
        std::memcpy(data, pixels, sizeof(pixels));
        image->setImage(
            1, 2, 1, GL_RGBA, GL_BGRA, GL_UNSIGNED_SHORT_4_4_4_4_REV, data, osg::Image::USE_NEW_DELETE, 1, 2);

        EXPECT_TRUE(SceneUtil::toOpenGLESFormat(*image));
        EXPECT_EQ(image->getDataType(), static_cast<GLenum>(GL_UNSIGNED_BYTE));
        const unsigned char expected[] = { 255, 0, 0, 255, 0, 0, 255, 255 };
        EXPECT_TRUE(std::equal(std::begin(expected), std::end(expected), image->data()));
    }

    TEST(SceneUtilOpenGLESFormatTest, four_byte_pixels_under_an_rgb_internal_format_become_opaque_rgba)
    {
        osg::ref_ptr<osg::Image> image = new osg::Image;
        image->allocateImage(1, 1, 1, GL_BGRA, GL_UNSIGNED_BYTE);
        image->setInternalTextureFormat(GL_RGB);
        const unsigned char pixel[] = { 3, 2, 1, 0 };
        std::copy(std::begin(pixel), std::end(pixel), image->data());

        EXPECT_TRUE(SceneUtil::toOpenGLESFormat(*image));
        EXPECT_EQ(image->getPixelFormat(), static_cast<GLenum>(GL_RGBA));
        EXPECT_EQ(image->getInternalTextureFormat(), static_cast<GLint>(GL_RGBA));
        const unsigned char expected[] = { 1, 2, 3, 255 };
        EXPECT_TRUE(std::equal(std::begin(expected), std::end(expected), image->data()));
    }

    // 10-bit DDS files come as 2-10-10-10 packings: BGRA order (blue in the lowest bits), which OpenGL ES does not
    // upload, or RGBA order under an unsized internal format, which OpenGL ES takes only under the sized 10-bit format
    osg::ref_ptr<osg::Image> tenBitImage(int width, int height, GLenum pixelFormat,
        const std::vector<std::uint32_t>& pixels, const std::vector<unsigned int>& mipmaps = {})
    {
        osg::ref_ptr<osg::Image> image = new osg::Image;
        unsigned char* data = new unsigned char[pixels.size() * 4];
        std::memcpy(data, pixels.data(), pixels.size() * 4);
        image->setImage(width, height, 1, GL_RGBA, pixelFormat, GL_UNSIGNED_INT_2_10_10_10_REV, data,
            osg::Image::USE_NEW_DELETE, 1);
        image->setMipmapLevels(mipmaps);
        return image;
    }

    std::uint32_t tenBitPixel(std::uint32_t first, std::uint32_t second, std::uint32_t third, std::uint32_t alpha)
    {
        return first | (second << 10) | (third << 20) | (alpha << 30);
    }

    TEST(SceneUtilOpenGLESFormatTest, a_bgra_2_10_10_10_image_becomes_rgba_with_every_mipmap_and_all_its_bits)
    {
        // 2x1 then 1x1, each pixel written blue, green, red, alpha from the lowest bits
        osg::ref_ptr<osg::Image> image = tenBitImage(
            2, 1, GL_BGRA, { tenBitPixel(1023, 512, 1, 3), tenBitPixel(0, 1, 2, 0), tenBitPixel(7, 8, 9, 1) }, { 8 });

        EXPECT_TRUE(SceneUtil::toOpenGLESFormat(*image));
        EXPECT_EQ(image->getPixelFormat(), static_cast<GLenum>(GL_RGBA));
        EXPECT_EQ(image->getDataType(), static_cast<GLenum>(GL_UNSIGNED_INT_2_10_10_10_REV));
        EXPECT_EQ(image->getInternalTextureFormat(), static_cast<GLint>(GL_RGB10_A2));
        ASSERT_EQ(image->getNumMipmapLevels(), 2u);
        std::uint32_t level0[2];
        std::memcpy(level0, image->getMipmapData(0), sizeof(level0));
        EXPECT_EQ(level0[0], tenBitPixel(1, 512, 1023, 3));
        EXPECT_EQ(level0[1], tenBitPixel(2, 1, 0, 0));
        std::uint32_t level1;
        std::memcpy(&level1, image->getMipmapData(1), sizeof(level1));
        EXPECT_EQ(level1, tenBitPixel(9, 8, 7, 1));
    }

    TEST(SceneUtilOpenGLESFormatTest, an_rgba_2_10_10_10_image_gets_the_sized_10_bit_format_and_keeps_its_pixels)
    {
        const std::uint32_t pixel = tenBitPixel(1, 512, 1023, 2);
        osg::ref_ptr<osg::Image> image = tenBitImage(1, 1, GL_RGBA, { pixel });

        EXPECT_TRUE(SceneUtil::toOpenGLESFormat(*image));
        EXPECT_EQ(image->getPixelFormat(), static_cast<GLenum>(GL_RGBA));
        EXPECT_EQ(image->getInternalTextureFormat(), static_cast<GLint>(GL_RGB10_A2));
        std::uint32_t kept;
        std::memcpy(&kept, image->data(), sizeof(kept));
        EXPECT_EQ(kept, pixel);
    }

    // A depth image of floats (the stand-in for a shadow map where there are no shadows) comes with the unsized depth
    // format, which OpenGL ES takes only with integer pixels; it gets the sized float depth format and keeps its value
    TEST(SceneUtilOpenGLESFormatTest, float_depth_pixels_get_the_sized_float_depth_format)
    {
        osg::ref_ptr<osg::Image> image = new osg::Image;
        image->allocateImage(1, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT);
        const float depth = 0.5f;
        std::memcpy(image->data(), &depth, sizeof(depth));

        EXPECT_TRUE(SceneUtil::toOpenGLESFormat(*image));
        EXPECT_EQ(image->getInternalTextureFormat(), static_cast<GLint>(GL_DEPTH_COMPONENT32F));
        EXPECT_EQ(image->getPixelFormat(), static_cast<GLenum>(GL_DEPTH_COMPONENT));
        EXPECT_EQ(image->getDataType(), static_cast<GLenum>(GL_FLOAT));
        float kept = 0.f;
        std::memcpy(&kept, image->data(), sizeof(kept));
        EXPECT_EQ(kept, depth);
    }

    TEST(SceneUtilOpenGLESFormatTest, images_opengl_es_takes_are_left_alone)
    {
        osg::ref_ptr<osg::Image> rgba = new osg::Image;
        rgba->allocateImage(1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE);
        EXPECT_FALSE(SceneUtil::toOpenGLESFormat(*rgba));

        osg::ref_ptr<osg::Image> luminance = new osg::Image;
        luminance->allocateImage(1, 1, 1, GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE);
        EXPECT_FALSE(SceneUtil::toOpenGLESFormat(*luminance));

        osg::ref_ptr<osg::Image> rgb565 = new osg::Image;
        rgb565->allocateImage(1, 1, 1, GL_RGB, GL_UNSIGNED_SHORT_5_6_5);
        EXPECT_FALSE(SceneUtil::toOpenGLESFormat(*rgb565));

        osg::ref_ptr<osg::Image> tenBit = tenBitImage(1, 1, GL_RGBA, { tenBitPixel(1, 2, 3, 0) });
        tenBit->setInternalTextureFormat(GL_RGB10_A2);
        EXPECT_FALSE(SceneUtil::toOpenGLESFormat(*tenBit));

        osg::ref_ptr<osg::Image> depth = new osg::Image;
        depth->allocateImage(1, 1, 1, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT);
        EXPECT_FALSE(SceneUtil::toOpenGLESFormat(*depth));
        EXPECT_EQ(depth->getInternalTextureFormat(), static_cast<GLint>(GL_DEPTH_COMPONENT));
    }
}
