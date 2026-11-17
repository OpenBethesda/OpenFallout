#ifndef OPENFALLOUT_COMPONENTS_SCENEUTIL_UTIL_H
#define OPENFALLOUT_COMPONENTS_SCENEUTIL_UTIL_H

#include <osg/Camera>
#include <osg/Matrixf>
#include <osg/Texture2D>
#include <osg/Vec4f>

#include <components/resource/resourcesystem.hpp>

#include "statesetupdater.hpp"

namespace osgViewer
{
    class Renderer;
}

namespace SceneUtil
{
    class GlowUpdater : public SceneUtil::StateSetUpdater
    {
    public:
        GlowUpdater(int texUnit, const osg::Vec4f& color, const std::vector<osg::ref_ptr<osg::Texture2D>>& textures,
            osg::Node* node, float duration, Resource::ResourceSystem* resourcesystem);

        void setDefaults(osg::StateSet* stateset) override;

        void removeTexture(osg::StateSet* stateset);
        void apply(osg::StateSet* stateset, osg::NodeVisitor* nv) override;

        bool isPermanentGlowUpdater();

        bool isDone();

        void setColor(const osg::Vec4f& color);

        void setDuration(float duration);

    private:
        int mTexUnit;
        osg::Vec4f mColor;
        osg::Vec4f
            mOriginalColor; // for restoring the color of a permanent glow after a temporary glow on the object finishes
        std::vector<osg::ref_ptr<osg::Texture2D>> mTextures;
        osg::Node* mNode;
        float mDuration;
        float mOriginalDuration; // for recording that this is originally a permanent glow if it is changed to a
                                 // temporary one
        double mStartingTime;
        Resource::ResourceSystem* mResourceSystem;
        bool mColorChanged;
        bool mDone;
    };

    // Transform a bounding sphere by a matrix
    // based off private code in osg::Transform
    // TODO: patch osg to make public
    template <typename VT>
    inline void transformBoundingSphere(const osg::Matrixf& matrix, osg::BoundingSphereImpl<VT>& bsphere)
    {
        VT xdash = bsphere._center;
        xdash.x() += bsphere._radius;
        xdash = xdash * matrix;

        VT ydash = bsphere._center;
        ydash.y() += bsphere._radius;
        ydash = ydash * matrix;

        VT zdash = bsphere._center;
        zdash.z() += bsphere._radius;
        zdash = zdash * matrix;

        bsphere._center = bsphere._center * matrix;

        xdash -= bsphere._center;
        typename VT::value_type sqrlenXdash = xdash.length2();

        ydash -= bsphere._center;
        typename VT::value_type sqrlenYdash = ydash.length2();

        zdash -= bsphere._center;
        typename VT::value_type sqrlenZdash = zdash.length2();

        bsphere._radius = sqrlenXdash;
        if (bsphere._radius < sqrlenYdash)
            bsphere._radius = sqrlenYdash;
        if (bsphere._radius < sqrlenZdash)
            bsphere._radius = sqrlenZdash;
        bsphere._radius = sqrtf(bsphere._radius);
    }

    osg::Vec4f colourFromRGB(unsigned int clr);

    osg::Vec4f colourFromRGBA(unsigned int value);

    float makeOsgColorComponent(unsigned int value, unsigned int shift);

    bool hasUserDescription(const osg::Node* node, std::string_view pattern);

    osg::ref_ptr<GlowUpdater> addEnchantedGlow(osg::ref_ptr<osg::Node> node, Resource::ResourceSystem* resourceSystem,
        const osg::Vec4f& glowColor, float glowDuration = -1);

    // Alpha-to-coverage requires a multisampled framebuffer, so we need to set that up for RTTs
    void attachAlphaToCoverageFriendlyFramebufferToCamera(osg::Camera* camera, osg::Camera::BufferComponent buffer,
        osg::Texture* texture, unsigned int level, unsigned int face, bool mipMapGeneration,
        bool addMSAAIntermediateTarget);

    class OperationSequence : public osg::Operation
    {
    public:
        OperationSequence(bool keep);

        void operator()(osg::Object* object) override;

        void add(osg::Operation* operation);

    protected:
        osg::ref_ptr<osg::OperationQueue> mOperationQueue;
    };

    // Compute the unsized format equivalent to the given pixel format
    // Unlike osg::Image::computePixelFormat, this also covers compressed formats
    GLenum computeUnsizedPixelFormat(GLenum format);

    // Turns an 8-bit BGR or BGRA image, every mipmap of it, into RGB or RGBA in place, and says whether it did. OpenGL
    // ES uploads no BGR or BGRA pixels, so the Metal build does this to images as they load.
    bool toRgbOrder(osg::Image& image);

    // Turns an uncompressed image OpenGL ES 3.0 would refuse to upload into one it takes and samples as desktop OpenGL
    // samples the original, in place, and says whether it did: BGR and BGRA become RGB and RGBA (toRgbOrder), the
    // 16-bit BGRA packings (1-5-5-5 and 4-4-4-4) become 8-bit RGBA, 2-10-10-10 pixels become RGBA order under the sized
    // 10-bit format, 4-byte pixels under an RGB internal format take an opaque alpha and an RGBA internal format, and
    // float depth pixels under the unsized depth format take the sized float depth format. The Metal build does this to
    // images as they load and to images made in code.
    bool toOpenGLESFormat(osg::Image& image);

    // Recover the presumed texture type for the given texture unit
    // It may be set as a state attribute or it may come from the used texture's name
    const std::string& getTextureType(const osg::StateSet& stateset, const osg::Texture& texture, unsigned int texUnit);

    void disableFFPStateForRenderer(osgViewer::Renderer* renderer);
}

#endif
