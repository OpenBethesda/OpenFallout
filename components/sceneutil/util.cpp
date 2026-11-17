#include "util.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <utility>
#include <vector>

#include <osg/FrameBufferObject>
#include <osg/Image>
#include <osg/Node>
#include <osg/NodeVisitor>
#include <osgUtil/CullVisitor>
#include <osgUtil/RenderStage>
#include <osgViewer/Renderer>

#include <components/resource/imagemanager.hpp>
#include <components/resource/scenemanager.hpp>
#include <components/sceneutil/texturetype.hpp>
#include <components/vfs/pathutil.hpp>

#include "gldialect.hpp"

namespace SceneUtil
{
    namespace
    {
        std::array<VFS::Path::Normalized, 32> generateGlowTextureNames()
        {
            constexpr VFS::Path::NormalizedView prefix("textures/magicitem");
            std::array<VFS::Path::Normalized, 32> result;
            for (std::size_t i = 0; i < result.size(); ++i)
            {
                std::stringstream stream;
                stream << "caust";
                stream << std::setw(2);
                stream << std::setfill('0');
                stream << i;
                stream << ".dds";
                result[i] = prefix / VFS::Path::Normalized(std::move(stream).str());
            }
            return result;
        }

        const std::array<VFS::Path::Normalized, 32> glowTextureNames = generateGlowTextureNames();

        struct FindLowestUnusedTexUnitVisitor : public osg::NodeVisitor
        {
            FindLowestUnusedTexUnitVisitor()
                : osg::NodeVisitor(TRAVERSE_ALL_CHILDREN)
            {
            }

            void apply(osg::Node& node) override
            {
                if (osg::StateSet* stateset = node.getStateSet())
                    mLowestUnusedTexUnit
                        = std::max(mLowestUnusedTexUnit, int(stateset->getTextureAttributeList().size()));

                traverse(node);
            }

            int mLowestUnusedTexUnit = 0;
        };
    }

    GlowUpdater::GlowUpdater(int texUnit, const osg::Vec4f& color,
        const std::vector<osg::ref_ptr<osg::Texture2D>>& textures, osg::Node* node, float duration,
        Resource::ResourceSystem* resourcesystem)
        : mTexUnit(texUnit)
        , mColor(color)
        , mOriginalColor(color)
        , mTextures(textures)
        , mNode(node)
        , mDuration(duration)
        , mOriginalDuration(duration)
        , mStartingTime(0)
        , mResourceSystem(resourcesystem)
        , mColorChanged(false)
        , mDone(false)
    {
    }

    void GlowUpdater::setDefaults(osg::StateSet* stateset)
    {
        if (mDone)
            removeTexture(stateset);
        else
        {
            stateset->addUniform(new osg::Uniform("envMapColor", mColor));
        }
    }

    void GlowUpdater::removeTexture(osg::StateSet* stateset)
    {
        stateset->removeTextureAttribute(mTexUnit, osg::StateAttribute::TEXTURE);
        stateset->removeUniform("envMapColor");

        osg::StateSet::TextureAttributeList& list = stateset->getTextureAttributeList();
        while (list.size() && list.rbegin()->empty())
            list.pop_back();
    }

    void GlowUpdater::apply(osg::StateSet* stateset, osg::NodeVisitor* nv)
    {
        if (mColorChanged)
        {
            this->reset();
            setDefaults(stateset);
            mColorChanged = false;
        }
        if (mDone)
            return;

        // Set the starting time to measure glow duration from if this is a temporary glow
        if ((mDuration >= 0) && mStartingTime == 0)
            mStartingTime = nv->getFrameStamp()->getSimulationTime();

        double time = nv->getFrameStamp()->getSimulationTime();
        int index = static_cast<int>(time * 16) % mTextures.size();
        stateset->setTextureAttribute(
            mTexUnit, mTextures[index], osg::StateAttribute::ON | osg::StateAttribute::OVERRIDE);

        if ((mDuration >= 0)
            && (time - mStartingTime > mDuration)) // If this is a temporary glow and it has finished its duration
        {
            if (mOriginalDuration >= 0) // if this glowupdater was a temporary glow since its creation
            {
                removeTexture(stateset);
                this->reset();
                mDone = true;
                // normally done in StateSetUpdater::operator(), but needs doing here so the shader visitor sees the
                // right StateSet
                mNode->setStateSet(stateset);
                mResourceSystem->getSceneManager()->recreateShaders(mNode);
            }
            if (mOriginalDuration < 0) // if this glowupdater was originally a permanent glow
            {
                mDuration = mOriginalDuration;
                mStartingTime = 0;
                mColor = mOriginalColor;
                this->reset();
                setDefaults(stateset);
            }
        }
    }

    bool GlowUpdater::isPermanentGlowUpdater()
    {
        return (mDuration < 0);
    }

    bool GlowUpdater::isDone()
    {
        return mDone;
    }

    void GlowUpdater::setColor(const osg::Vec4f& color)
    {
        mColor = color;
        mColorChanged = true;
    }

    void GlowUpdater::setDuration(float duration)
    {
        mDuration = duration;
    }

    osg::Vec4f colourFromRGB(unsigned int clr)
    {
        osg::Vec4f colour(
            ((clr >> 0) & 0xFF) / 255.0f, ((clr >> 8) & 0xFF) / 255.0f, ((clr >> 16) & 0xFF) / 255.0f, 1.f);
        return colour;
    }

    osg::Vec4f colourFromRGBA(unsigned int value)
    {
        return osg::Vec4f(makeOsgColorComponent(value, 0), makeOsgColorComponent(value, 8),
            makeOsgColorComponent(value, 16), makeOsgColorComponent(value, 24));
    }

    float makeOsgColorComponent(unsigned int value, unsigned int shift)
    {
        return float((value >> shift) & 0xFFu) / 255.0f;
    }

    bool hasUserDescription(const osg::Node* node, std::string_view pattern)
    {
        if (node == nullptr)
            return false;

        const osg::UserDataContainer* udc = node->getUserDataContainer();
        if (udc && udc->getNumDescriptions() > 0)
        {
            for (auto& descr : udc->getDescriptions())
            {
                if (descr == pattern)
                    return true;
            }
        }

        return false;
    }

    osg::ref_ptr<GlowUpdater> addEnchantedGlow(osg::ref_ptr<osg::Node> node, Resource::ResourceSystem* resourceSystem,
        const osg::Vec4f& glowColor, float glowDuration)
    {
        std::vector<osg::ref_ptr<osg::Texture2D>> textures;
        for (const VFS::Path::Normalized& name : glowTextureNames)
        {
            osg::ref_ptr<osg::Image> image = resourceSystem->getImageManager()->getImage(name);
            osg::ref_ptr<osg::Texture2D> tex(new osg::Texture2D(image));
            tex->setWrap(osg::Texture::WRAP_S, osg::Texture2D::REPEAT);
            tex->setWrap(osg::Texture::WRAP_T, osg::Texture2D::REPEAT);
            resourceSystem->getSceneManager()->applyFilterSettings(tex);
            textures.push_back(tex);
        }

        FindLowestUnusedTexUnitVisitor findLowestUnusedTexUnitVisitor;
        node->accept(findLowestUnusedTexUnitVisitor);
        int texUnit = findLowestUnusedTexUnitVisitor.mLowestUnusedTexUnit;

        osg::ref_ptr<GlowUpdater> glowUpdater
            = new GlowUpdater(texUnit, glowColor, textures, node, glowDuration, resourceSystem);
        node->addUpdateCallback(glowUpdater);

        // set a texture now so that the ShaderVisitor can find it
        osg::ref_ptr<osg::StateSet> writableStateSet = nullptr;
        if (!node->getStateSet())
            writableStateSet = node->getOrCreateStateSet();
        else
        {
            writableStateSet = new osg::StateSet(*node->getStateSet(), osg::CopyOp::SHALLOW_COPY);
            node->setStateSet(writableStateSet);
        }
        writableStateSet->setTextureAttribute(texUnit, textures.front(), osg::StateAttribute::ON);
        writableStateSet->setTextureAttribute(texUnit, new TextureType("envMap"), osg::StateAttribute::ON);
        writableStateSet->addUniform(new osg::Uniform("envMapColor", glowColor));
        resourceSystem->getSceneManager()->recreateShaders(std::move(node));

        return glowUpdater;
    }

    void attachAlphaToCoverageFriendlyFramebufferToCamera(osg::Camera* camera, osg::Camera::BufferComponent buffer,
        osg::Texture* texture, unsigned int level, unsigned int face, bool mipMapGeneration,
        bool addMSAAIntermediateTarget)
    {
        unsigned int samples = 0;
        unsigned int colourSamples = 0;
        if (addMSAAIntermediateTarget)
        {
            // Alpha-to-coverage requires a multisampled framebuffer.
            // OSG will set that up automatically and resolve it to the specified single-sample texture for us.
            // For some reason, two samples are needed, at least with some drivers.
            samples = 2;
            colourSamples = 1;
        }
        camera->attach(buffer, texture, level, face, mipMapGeneration, samples, colourSamples);
    }

    OperationSequence::OperationSequence(bool keep)
        : Operation("OperationSequence", keep)
        , mOperationQueue(new osg::OperationQueue())
    {
    }

    void OperationSequence::operator()(osg::Object* object)
    {
        mOperationQueue->runOperations(object);
    }

    void OperationSequence::add(osg::Operation* operation)
    {
        mOperationQueue->add(operation);
    }

    bool mipmapsFit(const osg::Image& image, std::size_t dataSize)
    {
        for (unsigned int level = 0; level < image.getNumMipmapLevels(); ++level)
        {
            const int width = std::max(image.s() >> level, 1);
            const int rows = std::max(image.t() >> level, 1) * std::max(image.r() >> level, 1);
            const int rowLength = image.getRowLength() != 0 ? image.getRowLength() : width;
            const std::size_t size = image.isCompressed()
                ? osg::Image::computeImageSizeInBytes(std::max(image.s() >> level, 1), std::max(image.t() >> level, 1),
                      std::max(image.r() >> level, 1), image.getPixelFormat(), image.getDataType(), image.getPacking())
                : static_cast<std::size_t>(osg::Image::computeRowWidthInBytes(
                      rowLength, image.getPixelFormat(), image.getDataType(), image.getPacking()))
                    * rows;
            const std::size_t offset = image.getMipmapOffset(level);
            if (offset > dataSize || size > dataSize - offset)
                return false;
        }
        return true;
    }

    bool toRgbOrder(osg::Image& image)
    {
        const GLenum format = image.getPixelFormat();
        if ((format != GL_BGR && format != GL_BGRA) || image.getDataType() != GL_UNSIGNED_BYTE
            || image.data() == nullptr)
            return false;
        const unsigned int step = format == GL_BGR ? 3 : 4;
        for (unsigned int level = 0; level < image.getNumMipmapLevels(); ++level)
        {
            const int width = std::max(image.s() >> level, 1);
            const int rows = std::max(image.t() >> level, 1) * std::max(image.r() >> level, 1);
            // A row length, when the image gives one, is the length of every row, as OpenSceneGraph uploads it
            const int rowLength = image.getRowLength() != 0 ? image.getRowLength() : width;
            const unsigned int rowStep
                = osg::Image::computeRowWidthInBytes(rowLength, format, GL_UNSIGNED_BYTE, image.getPacking());
            unsigned char* row = image.getMipmapData(level);
            for (int i = 0; i < rows; ++i, row += rowStep)
                for (unsigned char* pixel = row; pixel < row + width * step; pixel += step)
                    std::swap(pixel[0], pixel[2]);
        }
        const GLenum rgb = format == GL_BGR ? GL_RGB : GL_RGBA;
        image.setPixelFormat(rgb);
        if (image.getInternalTextureFormat() == static_cast<GLint>(format))
            image.setInternalTextureFormat(rgb);
        return true;
    }

    namespace
    {
        // Calls f(row, width) for each row of each mipmap of the image
        template <class F>
        void forEachRow(osg::Image& image, F&& f)
        {
            for (unsigned int level = 0; level < image.getNumMipmapLevels(); ++level)
            {
                const int width = std::max(image.s() >> level, 1);
                const int rows = std::max(image.t() >> level, 1) * std::max(image.r() >> level, 1);
                const int rowLength = image.getRowLength() != 0 ? image.getRowLength() : width;
                const unsigned int rowStep = osg::Image::computeRowWidthInBytes(
                    rowLength, image.getPixelFormat(), image.getDataType(), image.getPacking());
                unsigned char* row = image.getMipmapData(level);
                for (int i = 0; i < rows; ++i, row += rowStep)
                    f(row, width);
            }
        }

        // The 8-bit value of a channel of `bits` bits, rounded as a float would be
        unsigned char widen(unsigned int value, unsigned int bits)
        {
            const unsigned int max = (1u << bits) - 1;
            return static_cast<unsigned char>((value * 255 + max / 2) / max);
        }

        // 16-bit BGRA (1-5-5-5 or 4-4-4-4, first channel in the lowest bits) to 8-bit RGBA, every mipmap
        void widenPackedBgra(osg::Image& image)
        {
            const bool fiveBits = image.getDataType() == GL_UNSIGNED_SHORT_1_5_5_5_REV;
            const unsigned int colourBits = fiveBits ? 5 : 4;
            const unsigned int alphaBits = fiveBits ? 1 : 4;
            const unsigned int mask = (1u << colourBits) - 1;

            std::vector<unsigned int> offsets;
            std::size_t size = 0;
            for (unsigned int level = 0; level < image.getNumMipmapLevels(); ++level)
            {
                if (level > 0)
                    offsets.push_back(static_cast<unsigned int>(size));
                size += static_cast<std::size_t>(std::max(image.s() >> level, 1)) * std::max(image.t() >> level, 1)
                    * std::max(image.r() >> level, 1) * 4;
            }
            unsigned char* data = new unsigned char[size];
            unsigned char* to = data;
            forEachRow(image, [&](const unsigned char* row, int width) {
                for (int x = 0; x < width; ++x, to += 4)
                {
                    std::uint16_t value;
                    std::memcpy(&value, row + 2 * x, 2);
                    to[0] = widen((value >> (2 * colourBits)) & mask, colourBits);
                    to[1] = widen((value >> colourBits) & mask, colourBits);
                    to[2] = widen(value & mask, colourBits);
                    to[3] = widen(value >> (3 * colourBits), alphaBits);
                }
            });
            const GLint internalFormat = image.getInternalTextureFormat();
            image.setImage(image.s(), image.t(), image.r(), internalFormat, GL_RGBA, GL_UNSIGNED_BYTE, data,
                osg::Image::USE_NEW_DELETE, 1);
            image.setMipmapLevels(offsets);
        }

        // 2-10-10-10 pixels: BGRA order (blue in the lowest bits) becomes RGBA order, and an unsized internal format
        // the sized 10-bit one, the only one OpenGL ES takes them under; every bit is kept
        bool toRgb10A2(osg::Image& image)
        {
            if (image.getDataType() != GL_UNSIGNED_INT_2_10_10_10_REV
                || (image.getPixelFormat() != GL_BGRA && image.getPixelFormat() != GL_RGBA))
                return false;
            bool changed = false;
            if (image.getPixelFormat() == GL_BGRA)
            {
                forEachRow(image, [](unsigned char* row, int width) {
                    for (int x = 0; x < width; ++x)
                    {
                        std::uint32_t value;
                        std::memcpy(&value, row + 4 * x, 4);
                        value = (value & 0xc00ffc00u) | ((value & 0x3ffu) << 20) | ((value >> 20) & 0x3ffu);
                        std::memcpy(row + 4 * x, &value, 4);
                    }
                });
                image.setPixelFormat(GL_RGBA);
                changed = true;
            }
            const GLint internalFormat = image.getInternalTextureFormat();
            if (internalFormat == GL_RGBA || internalFormat == GL_BGRA || internalFormat == 4)
            {
                image.setInternalTextureFormat(GL_RGB10_A2);
                changed = true;
            }
            return changed;
        }
    }

    void setDepthTextureFilter(osg::Texture& texture, osg::Texture::FilterMode filter)
    {
        if (GLDialect::sOpenGLES && !texture.getShadowComparison())
            filter = osg::Texture::NEAREST;
        texture.setFilter(osg::Texture::MIN_FILTER, filter);
        texture.setFilter(osg::Texture::MAG_FILTER, filter);
    }

    bool toOpenGLESFormat(osg::Image& image)
    {
        if (image.isCompressed() || image.data() == nullptr)
            return false;
        if (image.getPixelFormat() == GL_DEPTH_COMPONENT && image.getDataType() == GL_FLOAT
            && image.getInternalTextureFormat() == GL_DEPTH_COMPONENT)
        {
            // OpenGL ES takes float depth pixels only under the sized float depth format
            image.setInternalTextureFormat(GL_DEPTH_COMPONENT32F);
            return true;
        }
        bool changed = false;
        if (image.getPixelFormat() == GL_BGRA
            && (image.getDataType() == GL_UNSIGNED_SHORT_1_5_5_5_REV
                || image.getDataType() == GL_UNSIGNED_SHORT_4_4_4_4_REV))
        {
            widenPackedBgra(image);
            changed = true;
        }
        changed = toRgb10A2(image) || changed;
        changed = toRgbOrder(image) || changed;

        const GLint internalFormat = image.getInternalTextureFormat();
        if (image.getPixelFormat() == GL_RGBA && image.getDataType() == GL_UNSIGNED_BYTE
            && (internalFormat == GL_RGB || internalFormat == GL_RGB8 || internalFormat == 3))
        {
            // Desktop OpenGL drops the fourth byte and samples an alpha of one
            forEachRow(image, [](unsigned char* row, int width) {
                for (int x = 0; x < width; ++x)
                    row[4 * x + 3] = 255;
            });
            image.setInternalTextureFormat(internalFormat == GL_RGB8 ? GL_RGBA8 : GL_RGBA);
            changed = true;
        }
        return changed;
    }

    GLenum computeUnsizedPixelFormat(GLenum format)
    {
        switch (format)
        {
            // Try compressed formats first, they're more likely to be used

            // Generic
            case GL_COMPRESSED_ALPHA_ARB:
                return GL_ALPHA;
            case GL_COMPRESSED_INTENSITY_ARB:
                return GL_INTENSITY;
            case GL_COMPRESSED_LUMINANCE_ALPHA_ARB:
                return GL_LUMINANCE_ALPHA;
            case GL_COMPRESSED_LUMINANCE_ARB:
                return GL_LUMINANCE;
            case GL_COMPRESSED_RGB_ARB:
                return GL_RGB;
            case GL_COMPRESSED_RGBA_ARB:
                return GL_RGBA;

            // S3TC
            case GL_COMPRESSED_RGB_S3TC_DXT1_EXT:
            case GL_COMPRESSED_SRGB_S3TC_DXT1_EXT:
                return GL_RGB;
            case GL_COMPRESSED_RGBA_S3TC_DXT1_EXT:
            case GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT:
            case GL_COMPRESSED_RGBA_S3TC_DXT3_EXT:
            case GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT:
            case GL_COMPRESSED_RGBA_S3TC_DXT5_EXT:
            case GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT:
                return GL_RGBA;

            // RGTC
            case GL_COMPRESSED_RED_RGTC1_EXT:
            case GL_COMPRESSED_SIGNED_RED_RGTC1_EXT:
                return GL_RED;
            case GL_COMPRESSED_RED_GREEN_RGTC2_EXT:
            case GL_COMPRESSED_SIGNED_RED_GREEN_RGTC2_EXT:
                return GL_RG;

            // PVRTC
            case GL_COMPRESSED_RGB_PVRTC_4BPPV1_IMG:
            case GL_COMPRESSED_RGB_PVRTC_2BPPV1_IMG:
                return GL_RGB;
            case GL_COMPRESSED_RGBA_PVRTC_4BPPV1_IMG:
            case GL_COMPRESSED_RGBA_PVRTC_2BPPV1_IMG:
                return GL_RGBA;

            // ETC
            case GL_COMPRESSED_R11_EAC:
            case GL_COMPRESSED_SIGNED_R11_EAC:
                return GL_RED;
            case GL_COMPRESSED_RG11_EAC:
            case GL_COMPRESSED_SIGNED_RG11_EAC:
                return GL_RG;
            case GL_ETC1_RGB8_OES:
            case GL_COMPRESSED_RGB8_ETC2:
            case GL_COMPRESSED_SRGB8_ETC2:
                return GL_RGB;
            case GL_COMPRESSED_RGB8_PUNCHTHROUGH_ALPHA1_ETC2:
            case GL_COMPRESSED_SRGB8_PUNCHTHROUGH_ALPHA1_ETC2:
            case GL_COMPRESSED_RGBA8_ETC2_EAC:
            case GL_COMPRESSED_SRGB8_ALPHA8_ETC2_EAC:
                return GL_RGBA;

            // ASTC
            case GL_COMPRESSED_RGBA_ASTC_4x4_KHR:
            case GL_COMPRESSED_RGBA_ASTC_5x4_KHR:
            case GL_COMPRESSED_RGBA_ASTC_5x5_KHR:
            case GL_COMPRESSED_RGBA_ASTC_6x5_KHR:
            case GL_COMPRESSED_RGBA_ASTC_6x6_KHR:
            case GL_COMPRESSED_RGBA_ASTC_8x5_KHR:
            case GL_COMPRESSED_RGBA_ASTC_8x6_KHR:
            case GL_COMPRESSED_RGBA_ASTC_8x8_KHR:
            case GL_COMPRESSED_RGBA_ASTC_10x5_KHR:
            case GL_COMPRESSED_RGBA_ASTC_10x6_KHR:
            case GL_COMPRESSED_RGBA_ASTC_10x8_KHR:
            case GL_COMPRESSED_RGBA_ASTC_10x10_KHR:
            case GL_COMPRESSED_RGBA_ASTC_12x10_KHR:
            case GL_COMPRESSED_RGBA_ASTC_12x12_KHR:
            case GL_COMPRESSED_SRGB8_ALPHA8_ASTC_4x4_KHR:
            case GL_COMPRESSED_SRGB8_ALPHA8_ASTC_5x4_KHR:
            case GL_COMPRESSED_SRGB8_ALPHA8_ASTC_5x5_KHR:
            case GL_COMPRESSED_SRGB8_ALPHA8_ASTC_6x5_KHR:
            case GL_COMPRESSED_SRGB8_ALPHA8_ASTC_6x6_KHR:
            case GL_COMPRESSED_SRGB8_ALPHA8_ASTC_8x5_KHR:
            case GL_COMPRESSED_SRGB8_ALPHA8_ASTC_8x6_KHR:
            case GL_COMPRESSED_SRGB8_ALPHA8_ASTC_8x8_KHR:
            case GL_COMPRESSED_SRGB8_ALPHA8_ASTC_10x5_KHR:
            case GL_COMPRESSED_SRGB8_ALPHA8_ASTC_10x6_KHR:
            case GL_COMPRESSED_SRGB8_ALPHA8_ASTC_10x8_KHR:
            case GL_COMPRESSED_SRGB8_ALPHA8_ASTC_10x10_KHR:
            case GL_COMPRESSED_SRGB8_ALPHA8_ASTC_12x10_KHR:
            case GL_COMPRESSED_SRGB8_ALPHA8_ASTC_12x12_KHR:
                return GL_RGBA;

            // Plug in some holes computePixelFormat has, you never know when these could come in handy
            case GL_INTENSITY4:
            case GL_INTENSITY8:
            case GL_INTENSITY12:
            case GL_INTENSITY16:
                return GL_INTENSITY;

            case GL_LUMINANCE4:
            case GL_LUMINANCE8:
            case GL_LUMINANCE12:
            case GL_LUMINANCE16:
                return GL_LUMINANCE;

            case GL_LUMINANCE4_ALPHA4:
            case GL_LUMINANCE6_ALPHA2:
            case GL_LUMINANCE8_ALPHA8:
            case GL_LUMINANCE12_ALPHA4:
            case GL_LUMINANCE12_ALPHA12:
            case GL_LUMINANCE16_ALPHA16:
                return GL_LUMINANCE_ALPHA;
        }

        return osg::Image::computePixelFormat(format);
    }

    const std::string& getTextureType(const osg::StateSet& stateset, const osg::Texture& texture, unsigned int texUnit)
    {
        const osg::StateAttribute* type = stateset.getTextureAttribute(texUnit, SceneUtil::TextureType::AttributeType);
        if (type)
            return static_cast<const SceneUtil::TextureType*>(type)->getName();

        return texture.getName();
    }

    void disableFFPStateForRenderer(osgViewer::Renderer* renderer)
    {
        auto disableFFPState = [](osgUtil::SceneView* sceneView) {
            sceneView->setDefaults(osgUtil::SceneView::NO_SCENEVIEW_LIGHT);
            sceneView->getGlobalStateSet()->removeMode(GL_LIGHT0);
            sceneView->getGlobalStateSet()->removeAttribute(osg::StateAttribute::LIGHTMODEL);
            sceneView->getGlobalStateSet()->removeAttribute(osg::StateAttribute::MATERIAL);
            sceneView->getGlobalStateSet()->removeAttribute(osg::StateAttribute::ALPHAFUNC);
        };

        disableFFPState(renderer->getSceneView(0));
        disableFFPState(renderer->getSceneView(1));
    }
}
